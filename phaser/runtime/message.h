// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "toolbelt/hexdump.h"
#include "phaser/runtime/payload_buffer.h"

namespace phaser {

// Message header.
// --------------
// Each message starts with a header which is:
// - The absolute offset of the FieldData for this message.
// - The presence mask - 1 bit per field.  This is variable in
//   size but is always a multiple of 32 bits.

// FieldData is a structure that contains the field numbers and offsets for a
// message. It is stored in the payload buffer.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
struct FieldData {
  uint32_t num;
  struct {
    uint32_t number;
    uint32_t offset : 24;  // Offset into message.
    uint32_t id : 8;       // Field id for presence bit mask.
  } fields[];  // Flexible array member; data lives in the payload buffer.
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// Hybrid field metadata stores a compact direct-indexed range followed by
// sorted sparse entries. The arrays immediately following HybridFieldData are:
//   FieldValue dense_fields[dense_span]
//   SparseFieldData sparse_fields[sparse_count]
// A zero dense-field offset marks a field number absent from the schema. Real
// field offsets are nonzero because every message begins with its header.
inline constexpr uint32_t kHybridFieldDataMagic = 0x50484431;

struct FieldValue {
  uint32_t offset : 24;
  uint32_t id : 8;
};
static_assert(sizeof(FieldValue) == sizeof(uint32_t));

struct SparseFieldData {
  uint32_t number;
  uint32_t offset : 24;
  uint32_t id : 8;
};
static_assert(sizeof(SparseFieldData) == 2 * sizeof(uint32_t));

struct HybridFieldData {
  uint32_t magic;
  uint32_t dense_base;
  uint32_t dense_span;
  uint32_t sparse_count;
};
static_assert(sizeof(HybridFieldData) == 4 * sizeof(uint32_t));

struct FieldLocation {
  int32_t offset = -1;
  int32_t id = -1;
};

namespace internal {

inline FieldLocation FindLegacyField(const FieldData* field_data,
                                     uint32_t field_number) {
  uint32_t left = 0;
  uint32_t right = field_data->num;
  while (left < right) {
    const uint32_t mid = left + (right - left) / 2;
    if (field_data->fields[mid].number == field_number) {
      return {
          .offset = static_cast<int32_t>(field_data->fields[mid].offset),
          .id = static_cast<int32_t>(field_data->fields[mid].id),
      };
    }
    if (field_data->fields[mid].number < field_number) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return {};
}

inline FieldLocation FindHybridField(const HybridFieldData* field_data,
                                     uint32_t field_number) {
  const auto* dense_fields =
      reinterpret_cast<const FieldValue*>(field_data + 1);
  const auto* sparse_fields = reinterpret_cast<const SparseFieldData*>(
      dense_fields + field_data->dense_span);

  if (field_number >= field_data->dense_base) {
    const uint32_t dense_index = field_number - field_data->dense_base;
    if (dense_index < field_data->dense_span) {
      const FieldValue& field = dense_fields[dense_index];
      if (field.offset == 0) {
        return {};
      }
      return {
          .offset = static_cast<int32_t>(field.offset),
          .id = static_cast<int32_t>(field.id),
      };
    }
  }

  uint32_t left = 0;
  uint32_t right = field_data->sparse_count;
  while (left < right) {
    const uint32_t mid = left + (right - left) / 2;
    if (sparse_fields[mid].number == field_number) {
      return {
          .offset = static_cast<int32_t>(sparse_fields[mid].offset),
          .id = static_cast<int32_t>(sparse_fields[mid].id),
      };
    }
    if (sparse_fields[mid].number < field_number) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return {};
}

// Overflow-safe check that a table of 'count' elements of 'elem_size' bytes
// starting at 'offset' lies entirely within a buffer of 'limit' bytes.
inline bool MetadataRangeFits(uint32_t offset, size_t count, size_t elem_size,
                              size_t limit) {
  if (offset > limit) {
    return false;
  }
  const size_t remaining = limit - offset;
  if (elem_size != 0 && count > remaining / elem_size) {
    return false;
  }
  return true;
}

// Validates that the hybrid metadata header and its dense/sparse tables fit
// within a buffer of 'limit' bytes before any entry is dereferenced.
inline bool HybridTableFits(uint32_t metadata_offset,
                            const HybridFieldData* field_data, size_t limit) {
  if (!MetadataRangeFits(metadata_offset, 1, sizeof(HybridFieldData), limit)) {
    return false;
  }
  const uint32_t dense_offset =
      metadata_offset + static_cast<uint32_t>(sizeof(HybridFieldData));
  if (!MetadataRangeFits(dense_offset, field_data->dense_span,
                         sizeof(FieldValue), limit)) {
    return false;
  }
  const uint32_t sparse_offset =
      dense_offset +
      static_cast<uint32_t>(field_data->dense_span * sizeof(FieldValue));
  return MetadataRangeFits(sparse_offset, field_data->sparse_count,
                           sizeof(SparseFieldData), limit);
}

// Validates that the legacy metadata header and its field table fit within a
// buffer of 'limit' bytes before any entry is dereferenced.
inline bool LegacyTableFits(uint32_t metadata_offset,
                            const FieldData* field_data, size_t limit) {
  if (!MetadataRangeFits(metadata_offset, 1, sizeof(uint32_t), limit)) {
    return false;
  }
  const uint32_t entries_offset =
      metadata_offset + static_cast<uint32_t>(sizeof(uint32_t));
  return MetadataRangeFits(entries_offset, field_data->num,
                           sizeof(field_data->fields[0]), limit);
}

}  // namespace internal

enum class FieldType {
  kFieldInt32,
  kFieldInt64,
  kFieldUInt32,
  kFieldUInt64,
  kFieldString,
  kFieldMessage,
  kFieldBytes,
  kFieldFloat,
  kFieldDouble,
  kFieldBool,
  kFieldEnum,
  kFieldOneof,
};

struct FieldInfo {
  FieldInfo(const std::string& n, FieldType t, int num, off_t off)
      : name(n), type(t), number(num), offset(off) {}
  std::string name;
  FieldType type;
  int number;
  off_t offset;  // Offset into source message (not binary).
};

struct PrimitiveFieldInfo : public FieldInfo {
  PrimitiveFieldInfo(const std::string& n, FieldType t, int num, off_t off,
                     bool f = false, bool /*s*/ = false, bool r = false,
                     bool p = false)
      : FieldInfo(n, t, num, off), is_fixed(f), is_repeated(r), is_packed(p) {}
  PrimitiveFieldInfo(const std::string& n, FieldType t, int num, off_t off,
                     const std::string& m, bool r = false, bool p = false)
      : FieldInfo(n, t, num, off),
        is_repeated(r),
        is_packed(p),
        message_or_enum_name(m) {}

  bool is_fixed = false;
  bool is_signed = false;
  bool is_repeated = false;
  bool is_packed = true;
  std::optional<std::string> message_or_enum_name;
};

struct UnionFieldInfo : public PrimitiveFieldInfo {
  UnionFieldInfo(const std::string& n, FieldType t, int num, off_t off, int i,
                 const std::string& m)
      : PrimitiveFieldInfo(n, t, num, off, m), id(i) {}
  UnionFieldInfo(const std::string& n, FieldType t, int num, off_t off, int i,
                 bool f = false, bool s = false)
      : PrimitiveFieldInfo(n, t, num, off, f, s), id(i) {}
  int id;  // Field id within union.
};

struct UnionInfo : public FieldInfo {
  UnionInfo(const std::string& n, off_t off)
      : FieldInfo(n, FieldType::kFieldOneof, 0, off) {}
  std::vector<std::shared_ptr<UnionFieldInfo>> fields_in_order;
};

struct MessageInfo {
  std::string full_name;
  absl::flat_hash_map<std::string, std::shared_ptr<FieldInfo>> fields_by_name;
  absl::flat_hash_map<int, std::shared_ptr<FieldInfo>> fields_by_number;
  std::vector<std::shared_ptr<FieldInfo>> fields_in_order;
};

inline constexpr uint32_t kRuntimeControlMagic = 0x52544850;  // "PHTR"
inline constexpr uint32_t kRuntimeControlVersion = 2;

// 64-bit FNV-1a hash of a message's full name, used to find the metadata
// already installed for a message type without storing or comparing names.
constexpr uint64_t MetadataKey(std::string_view full_name) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (char c : full_name) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

namespace internal {
// True for generated messages whose Clear() releases no payload memory.
template <typename T, typename = void>
struct ClearReleasesNothing : std::false_type {};
template <typename T>
struct ClearReleasesNothing<T,
                            std::void_t<decltype(T::kPhaserClearReleasesNothing)>>
    : std::bool_constant<T::kPhaserClearReleasesNothing> {};

// False for generated messages whose SyncToPayload() does nothing.
template <typename T, typename = void>
struct HasSyncToPayloadWork : std::true_type {};
template <typename T>
struct HasSyncToPayloadWork<
    T, std::void_t<decltype(T::kPhaserHasSyncToPayloadWork)>>
    : std::bool_constant<T::kPhaserHasSyncToPayloadWork> {};
}  // namespace internal

// PhaserROSFixedSize() of a message whose ROS serialized size depends on its
// contents.
inline constexpr size_t kROSVariableSize = ~size_t{0};

namespace internal {
constexpr size_t ROSFixedSum(std::initializer_list<size_t> sizes) {
  size_t total = 0;
  for (size_t size : sizes) {
    if (size == kROSVariableSize) {
      return kROSVariableSize;
    }
    total += size;
  }
  return total;
}

constexpr size_t ROSFixedProduct(size_t count, size_t size) {
  return size == kROSVariableSize ? kROSVariableSize : count * size;
}
}  // namespace internal

template <typename MessageType>
uint64_t MetadataKeyFor() {
  static const uint64_t key = MetadataKey(MessageType::FullName());
  return key;
}

struct RuntimeMetadataEntry {
  uint64_t key = 0;
  ::phaser::BufferOffset field_data = 0;
};

struct RuntimeControlBlock {
  uint32_t magic = kRuntimeControlMagic;
  uint32_t version = kRuntimeControlVersion;
  ::phaser::BufferOffset user_metadata = 0;
  uint32_t count = 0;
  uint32_t capacity = 0;
  RuntimeMetadataEntry entries[1];
};

inline size_t RuntimeControlSize(size_t capacity) {
  return offsetof(RuntimeControlBlock, entries) +
         capacity * sizeof(RuntimeMetadataEntry);
}

// 'capacity' is the number of message types the message can reach, including
// its own.  A message that reaches no other types installs its metadata only
// once, so there is nothing to look up and it gets no control block.
inline RuntimeControlBlock* InitializeRuntimeControl(
    ::phaser::PayloadBuffer** pb, size_t capacity) {
  if (capacity <= 1) {
    return nullptr;
  }
  void* memory = ::phaser::PayloadBuffer::Allocate(
      pb, static_cast<uint32_t>(RuntimeControlSize(capacity)), true);
  auto* control = new (memory) RuntimeControlBlock;
  control->capacity = static_cast<uint32_t>(capacity);
  (*pb)->metadata = (*pb)->ToOffset(control);
  return control;
}

// Mutable messages share one of these through an owning shared_ptr. Read-only
// messages copy this small runtime into the generated handle and use a
// non-owning shared_ptr alias without allocating a control block.
enum class RuntimeHandleMode {
  kBorrowedReadonly,
  kFixedMutable,
  kOwnedDynamic,
};

struct MessageRuntime {
  MessageRuntime(::phaser::PayloadBuffer* p, bool is_mutable = false)
      : pb(p),
        is_mutable_(is_mutable),
        mode_(is_mutable ? RuntimeHandleMode::kFixedMutable
                         : RuntimeHandleMode::kBorrowedReadonly) {}
  MessageRuntime(::phaser::PayloadBuffer* p, size_t size,
                 bool is_mutable = false)
      : pb(p),
        buffer_size(size),
        is_mutable_(is_mutable),
        mode_(is_mutable ? RuntimeHandleMode::kFixedMutable
                         : RuntimeHandleMode::kBorrowedReadonly) {}
  MessageRuntime(const MessageRuntime&) = default;
  MessageRuntime& operator=(const MessageRuntime&) = default;
  virtual ~MessageRuntime() = default;
  ::phaser::PayloadBuffer* pb;

  // This is the size of the buffer.  If it is zero, the size is inside
  // the payload buffer.  If it's non-zero, it's the size of the received
  // buffer.  We can't rely on the size inside the payload buffer if we
  // are looking at received data (someone could set it to anything and we
  // have no way to check it's valid).
  size_t buffer_size = 0;
  bool is_mutable_ = false;
  RuntimeHandleMode mode_ = RuntimeHandleMode::kBorrowedReadonly;

  void AddMetadata(uint64_t key, ::phaser::BufferOffset offset) {
    RuntimeControlBlock* control = GetRuntimeControl();
    if (control == nullptr) {
      return;
    }
    if (control->count == control->capacity) {
      const ::phaser::BufferOffset old_offset = pb->metadata;
      const uint32_t new_capacity = control->capacity * 2;
      void* memory = ::phaser::PayloadBuffer::Realloc(
          &pb, pb->ToAddress(old_offset),
          static_cast<uint32_t>(RuntimeControlSize(new_capacity)), true);
      control = static_cast<RuntimeControlBlock*>(memory);
      control->capacity = new_capacity;
      pb->metadata = pb->ToOffset(control);
    }
    RuntimeMetadataEntry& entry = control->entries[control->count++];
    entry.key = key;
    entry.field_data = offset;
  }
  ::phaser::BufferOffset GetMetadata(uint64_t key) const {
    const RuntimeControlBlock* control = GetRuntimeControl();
    if (control == nullptr) {
      return 0;
    }
    for (uint32_t i = 0; i < control->count; ++i) {
      if (control->entries[i].key == key) {
        return control->entries[i].field_data;
      }
    }
    return 0;
  }
  void AddMetadata(std::string_view name, ::phaser::BufferOffset offset) {
    AddMetadata(MetadataKey(name), offset);
  }
  ::phaser::BufferOffset GetMetadata(std::string_view name) const {
    return GetMetadata(MetadataKey(name));
  }

  bool IsMutable() const { return is_mutable_; }
  RuntimeHandleMode Mode() const { return mode_; }

  RuntimeControlBlock* GetRuntimeControl() const {
    if (pb == nullptr || pb->metadata == 0) {
      return nullptr;
    }
    auto* control =
        pb->ToAddress<RuntimeControlBlock>(pb->metadata, buffer_size);
    if (control == nullptr || control->magic != kRuntimeControlMagic ||
        control->version != kRuntimeControlVersion) {
      return nullptr;
    }
    return control;
  }

  // A mutable buffer was built by this process, so only received buffers need
  // their offsets validated.
  template <typename T = void>
  T* ToAddress(::phaser::BufferOffset offset) {
    if (is_mutable_) {
      return pb->UncheckedAddress<T>(offset);
    }
    return pb->ToAddress<T>(offset, buffer_size);
  }

  template <typename T = void>
  const T* ToAddress(::phaser::BufferOffset offset) const {
    if (is_mutable_) {
      return pb->UncheckedAddress<const T>(offset);
    }
    return pb->ToAddress<T>(offset, buffer_size);
  }

  // Number of bytes that are trusted to be available in the buffer.  On the
  // readonly receive path this is the size passed to CreateReadonly; otherwise
  // the buffer is one we own and full_size is authoritative.
  size_t TrustedSize() const {
    if (buffer_size != 0) {
      return buffer_size;
    }
    return pb != nullptr ? size_t(pb->full_size) : 0;
  }

  // Size-aware string reader.  Uses the trusted buffer size so a hostile length
  // word in received data cannot cause an out-of-bounds read.
  std::string_view GetStringView(::phaser::BufferOffset header_offset) const {
    if (pb == nullptr) {
      return {};
    }
    return pb->GetStringView(header_offset, buffer_size);
  }

  size_t StringSize(::phaser::BufferOffset header_offset) const {
    if (pb == nullptr) {
      return 0;
    }
    return pb->StringSize(header_offset, buffer_size);
  }

  const char* StringData(::phaser::BufferOffset header_offset) const {
    if (pb == nullptr) {
      return nullptr;
    }
    return pb->StringData(header_offset, buffer_size);
  }

  // Size-aware presence-bit read.  A hostile field id or inflated full_size
  // must not cause an out-of-bounds load of the presence mask.
  bool IsPresent(uint32_t bit, uint32_t presence_mask_offset) const {
    if (pb == nullptr || bit == static_cast<uint32_t>(-1)) {
      return false;
    }
    const uint32_t word = bit / 32;
    bit %= 32;
    const uint32_t* p = ToAddress<const uint32_t>(
        presence_mask_offset + word * static_cast<uint32_t>(sizeof(uint32_t)));
    if (p == nullptr) {
      return false;
    }
    return (*p & (1U << bit)) != 0;
  }

  // Clamps an attacker-influenced element count to the number of 'elem_size'
  // elements that actually fit in the buffer starting at 'data_offset'.
  size_t ClampElementCount(::phaser::BufferOffset data_offset, size_t claimed,
                           size_t elem_size) const {
    if (data_offset == 0 || elem_size == 0) {
      return 0;
    }
    const size_t limit = TrustedSize();
    if (data_offset >= limit) {
      return 0;
    }
    const size_t available = (limit - data_offset) / elem_size;
    return claimed < available ? claimed : available;
  }

  // Returns the capacity of a PayloadBuffer vector allocation whose data
  // starts at 'data_offset'.  The size word lives immediately before the
  // data; a hostile data_offset near the start of the buffer must not cause
  // an underflow read.
  size_t AllocatedCapacity(::phaser::BufferOffset data_offset,
                           size_t elem_size) const {
    if (elem_size == 0 || data_offset < sizeof(uint64_t) || pb == nullptr) {
      return 0;
    }
    const uint32_t* block = ToAddress<uint32_t>(data_offset);
    if (block == nullptr) {
      return 0;
    }
    return ::phaser::PayloadBuffer::DecodeSize(block) / elem_size;
  }

  template <typename T = void>
  ::phaser::BufferOffset ToOffset(const T* addr) const {
    if (is_mutable_) {
      return pb->UncheckedOffset(addr);
    }
    return pb->ToOffset(addr, buffer_size);
  }

  template <typename T = void>
  ::phaser::BufferOffset ToOffset(T* addr) {
    if (is_mutable_) {
      return pb->UncheckedOffset<T>(addr);
    }
    return pb->ToOffset(addr, buffer_size);
  }
};

// This is a message runtime for a message that is mutable.  It holds a mapping
// for each message name to the offset of the metadata in the payload buffer.
struct MutableMessageRuntime : public MessageRuntime {
  MutableMessageRuntime(::phaser::PayloadBuffer* p)
      : MessageRuntime(p, true) {}
};

// Dynamically allocated payload buffer.  Must be allocated in memory
// from malloc using the NewDynamicBuffer function.
struct DynamicMutableMessageRuntime : public MutableMessageRuntime {
  DynamicMutableMessageRuntime(::phaser::PayloadBuffer* p,
                               std::function<void(void*)> free)
      : MutableMessageRuntime(p), free_(std::move(free)) {
    mode_ = RuntimeHandleMode::kOwnedDynamic;
  }
  ~DynamicMutableMessageRuntime() override {
    if (free_ != nullptr) {
      pb->~PayloadBuffer();
      free_(pb);
    }
  }
  std::function<void(void*)> free_;
};

using RuntimeHandle = std::shared_ptr<MessageRuntime>;

namespace internal {
// Frees the payload memory owned by a field of a mutable message without
// resetting the field, for use only when the message holding the field is
// about to be freed and no handle to it outlives that.  'field' is the
// field's absolute offset, which relies on a mutable message having its
// compiled layout.  Only field types with a specialization are supported; a
// message with any other field releases through Clear() instead.
template <typename F>
struct FieldRelease : std::false_type {};

// A field that owns no payload memory.
struct NoFieldRelease : std::true_type {
  static void Release(const RuntimeHandle&, ::phaser::BufferOffset) {}
};

// The 'count' values of type T at 'offset', or nullptr if they don't fit in
// the buffer.
template <typename T>
const T* PayloadAt(const ::phaser::PayloadBuffer* pb,
                   ::phaser::BufferOffset offset, size_t count = 1) {
  if (offset == 0 || offset > pb->full_size ||
      count > (pb->full_size - offset) / sizeof(T)) {
    return nullptr;
  }
  return reinterpret_cast<const T*>(reinterpret_cast<const char*>(pb) +
                                    offset);
}

// The offset stored at 'field', or 0.
inline ::phaser::BufferOffset OffsetAt(const ::phaser::PayloadBuffer* pb,
                                       ::phaser::BufferOffset field) {
  const ::phaser::BufferOffset* p =
      PayloadAt<::phaser::BufferOffset>(pb, field);
  return p != nullptr ? *p : 0;
}

template <typename F>
void ReleaseField(const RuntimeHandle& runtime, ::phaser::BufferOffset field) {
  if constexpr (FieldRelease<F>::value) {
    FieldRelease<F>::Release(runtime, field);
  }
}

template <typename T, typename = void>
struct HasReleaseAt : std::false_type {};
template <typename T>
struct HasReleaseAt<T, std::void_t<decltype(&T::PhaserReleaseAt)>>
    : std::true_type {};

// Frees the message of type T at 'offset' and everything it owns.
template <typename T>
void ReleaseMessageAt(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset offset) {
  if constexpr (!ClearReleasesNothing<T>::value) {
    if constexpr (HasReleaseAt<T>::value) {
      T::PhaserReleaseAt(runtime, offset);
    } else {
      T(runtime, offset).Clear();
    }
  }
  runtime->pb->FreeAt(offset);
}

// Frees the elements of the message vector whose VectorHeader is at
// 'header', but not the vector's data.
template <typename T>
void ReleaseMessageElements(const RuntimeHandle& runtime,
                            ::phaser::BufferOffset header) {
  const ::phaser::VectorHeader* hdr =
      PayloadAt<::phaser::VectorHeader>(runtime->pb, header);
  if (hdr == nullptr) {
    return;
  }
  const uint32_t count = hdr->num_elements;
  const ::phaser::BufferOffset data_offset = hdr->data;
  for (uint32_t i = 0; i < count; i++) {
    // Clearing an element through the fallback can allocate and move the
    // buffer, so the data is found again for each element.
    const ::phaser::BufferOffset* data =
        PayloadAt<::phaser::BufferOffset>(runtime->pb, data_offset, count);
    if (data == nullptr) {
      return;
    }
    if (data[i] != 0) {
      ReleaseMessageAt<T>(runtime, data[i]);
    }
  }
}

// Frees the data of the vector whose VectorHeader is at 'field'.
inline void ReleaseVectorData(const RuntimeHandle& runtime,
                              ::phaser::BufferOffset field) {
  const ::phaser::VectorHeader* hdr =
      PayloadAt<::phaser::VectorHeader>(runtime->pb, field);
  if (hdr != nullptr) {
    runtime->pb->FreeAt(hdr->data);
  }
}

struct VectorDataRelease : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    ReleaseVectorData(runtime, field);
  }
};

template <typename T>
struct MessageVectorRelease : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    ReleaseMessageElements<T>(runtime, field);
    ReleaseVectorData(runtime, field);
  }
};

template <typename T>
struct IndirectMessageRelease : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    const ::phaser::BufferOffset message = OffsetAt(runtime->pb, field);
    if (message != 0) {
      ReleaseMessageAt<T>(runtime, message);
    }
  }
};
}  // namespace internal

struct InternalDefault {};

// Tuning parameters for messages.  The kPerformance tuning uses a bitmap
// allocator for block sizes up to 128 bytes.  This is about twice as fast
// for small blocks but uses more memory.  If you are sending messages
// using shared memory where size isn't important, you can use kPerformance.
// If you are sending messages over a network, then you can sacrifice
// allocation peformance for size and use kSize.
enum class Tuning {
  kPerformance,  // Use a bitmap allocator for small blocks
  kSize,         // Use a simple allocator for small blocks
};

// Payload buffers can move. All messages in a message tree must all use the
// same payload buffer. We hold a shared pointer to a pointer to the payload
// buffer.
//
//            +-------+
//            |       |
//            V       |
// +---------------+  |
// |               |  |
// | PayloadBuffer |  |
// |               |  |
// +---------------+  |
//                    |
//                    |
// +---------------+  |
// |     *         +--+
// +---------------+
//       ^ ^
//       | |
//       | +--------------------------+
//       +------------+   +--------+  |
//                    |   |        V  |
// +---------------+  |   |      +---+--------+
// |    buffer     +--+   |      |   buffer    |
// +---------------+      |      +-------------+
// |               |      |      |             |
// |   Message     |      |      |  Message    |
// |               |      |      |  Field      |
// |               +------+      |             |
// +---------------+             +-------------+

struct Message {
 private:
  MessageRuntime readonly_runtime_;

 public:
  Message() : readonly_runtime_(nullptr, size_t{0}, false) {}
  Message(const RuntimeHandle& rt, ::phaser::BufferOffset start)
      : readonly_runtime_(RuntimeToCopy(rt)),
        runtime(IsBorrowed(rt) ? BorrowRuntime(readonly_runtime_) : rt),
        absolute_binary_offset(start),
        field_locations_known_(runtime != nullptr && runtime->IsMutable()) {}
  Message(RuntimeHandle&& rt, ::phaser::BufferOffset start)
      : readonly_runtime_(RuntimeToCopy(rt)),
        runtime(IsBorrowed(rt) ? BorrowRuntime(readonly_runtime_)
                               : std::move(rt)),
        absolute_binary_offset(start),
        field_locations_known_(runtime != nullptr && runtime->IsMutable()) {}
  // For generated messages: 'field_data' is the message type's compiled field
  // metadata.  A readonly message whose metadata is identical has its fields
  // at their compiled locations.
  Message(const RuntimeHandle& rt, ::phaser::BufferOffset start,
          const void* field_data, size_t field_data_size)
      : Message(rt, start) {
    if (!field_locations_known_ && runtime != nullptr) {
      field_locations_known_ = MetadataIs(field_data, field_data_size);
    }
  }
  Message(const Message& other) : Message(other.runtime,
                                          other.absolute_binary_offset) {}
  Message(Message&& other) noexcept
      : Message(std::move(other.runtime), other.absolute_binary_offset) {}
  Message& operator=(const Message& other) {
    if (this != &other) {
      absolute_binary_offset = other.absolute_binary_offset;
      BindRuntime(other.runtime);
    }
    return *this;
  }
  Message& operator=(Message&& other) noexcept {
    if (this != &other) {
      absolute_binary_offset = other.absolute_binary_offset;
      BindRuntime(std::move(other.runtime));
    }
    return *this;
  }
  virtual ~Message() = default;

  virtual const MessageInfo* GetMessageInfo() const { return nullptr; }
  virtual std::string GetName() const { return "Message"; }
  virtual std::string GetFullName() const { return "phaser.Message"; }
  virtual void Clear() {}
  virtual void CopyFrom(const Message& /*src*/) {}
  virtual void SyncToPayload() const {}

  Message* operator->() { return this; }
  const Message* operator->() const { return this; }
  bool IsBound() const { return runtime != nullptr; }

  RuntimeHandle runtime;
  ::phaser::BufferOffset absolute_binary_offset = 0;

  // True if each field is known to be at its compiled offset with its
  // compiled presence id, so fields need not look themselves up.
  bool FieldLocationsKnown() const { return field_locations_known_; }

 private:
  // Fields are constructed after this, so it is set in the constructor.
  bool field_locations_known_ = false;

  // Comparing larger tables would cost more than the lookups it saves.
  static constexpr size_t kMaxComparedFieldDataSize = 256;

  bool MetadataIs(const void* field_data, size_t field_data_size) const {
    if (field_data_size > kMaxComparedFieldDataSize) {
      return false;
    }
    const ::phaser::BufferOffset* metadata_word =
        runtime->ToAddress<::phaser::BufferOffset>(absolute_binary_offset);
    if (metadata_word == nullptr) {
      return false;
    }
    const ::phaser::BufferOffset metadata_offset = *metadata_word;
    if (!internal::MetadataRangeFits(metadata_offset, 1, field_data_size,
                                     runtime->TrustedSize())) {
      return false;
    }
    const void* metadata = runtime->ToAddress<void>(metadata_offset);
    return metadata != nullptr &&
           memcmp(metadata, field_data, field_data_size) == 0;
  }

 public:

  // 'field' is the offset from the start of the message to the field (positive)
  // Subtract the field offset from the field to get the address of the
  // std::shared_ptr to the pointer to the ::phaser::PayloadBuffer.
  static ::phaser::PayloadBuffer* GetBuffer(const void* field,
                                              uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    return msg->runtime->pb;
  }

  static ::phaser::PayloadBuffer** GetBufferAddr(const void* field,
                                                   uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    if (!msg->runtime->IsMutable()) {
      throw std::logic_error("cannot mutate a readonly Phaser message");
    }
    return &msg->runtime->pb;
  }

  static RuntimeHandle& GetRuntime(void* field, uint32_t offset) {
    Message* msg =
        reinterpret_cast<Message*>(reinterpret_cast<char*>(field) - offset);
    return msg->runtime;
  }

  static const RuntimeHandle& GetRuntime(const void* field, uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    return msg->runtime;
  }

  static const Message* GetMessage(const void* field, uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    return msg;
  }

  static Message* GetMessage(void* field, uint32_t offset) {
    Message* msg =
        reinterpret_cast<Message*>(reinterpret_cast<char*>(field) - offset);
    return msg;
  }

  static ::phaser::BufferOffset GetMessageBinaryStart(const void* field,
                                                        uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    return msg->absolute_binary_offset;
  }

  absl::Status SetUserMetadata(::phaser::BufferOffset offset) {
    if (offset >= runtime->pb->hwm) {
      return absl::InternalError("Invalid metadata offset");
    }
    if (RuntimeControlBlock* control = runtime->GetRuntimeControl();
        control != nullptr) {
      control->user_metadata = offset;
    } else {
      runtime->pb->metadata = offset;
    }
    return absl::OkStatus();
  }

  void* GetUserMetadata() {
    ::phaser::BufferOffset offset = runtime->pb->metadata;
    if (RuntimeControlBlock* control = runtime->GetRuntimeControl();
        control != nullptr) {
      offset = control->user_metadata;
    }
    return runtime->pb->ToAddress(offset);
  }

  void* Allocate(size_t size, size_t alignment = 4, bool clear = true) {
    (void)alignment;
    return ::phaser::PayloadBuffer::Allocate(
        &runtime->pb, static_cast<uint32_t>(size), clear);
  }

  void Free(void* ptr) { runtime->pb->Free(ptr); }

  void* Realloc(void* ptr, size_t size, size_t alignment = 4,
                bool clear = true) {
    (void)alignment;
    return ::phaser::PayloadBuffer::Realloc(&runtime->pb, ptr,
                                            static_cast<uint32_t>(size), clear);
  }

  ::phaser::BufferOffset ToOffset(void* addr) {
    return runtime->pb->ToOffset(addr);
  }

  template <typename T>
  T* ToAddress(::phaser::BufferOffset offset) {
    return runtime->pb->ToAddress<T>(offset);
  }

  template <typename MessageType>
  void InstallMetadata() {
    const uint64_t key = MetadataKeyFor<MessageType>();
    auto metadata = runtime->GetMetadata(key);
    if (metadata != 0) {
      ::phaser::BufferOffset* header =
          runtime->pb->ToAddress<::phaser::BufferOffset>(
              absolute_binary_offset);
      *header = metadata;
      return;
    }

    // Allocate space for field data in the payload buffer and copy it in.
    void* fields = ::phaser::PayloadBuffer::Allocate(
        &runtime->pb, sizeof(MessageType::field_data), false);
    memcpy(fields, &MessageType::field_data, sizeof(MessageType::field_data));
    ::phaser::BufferOffset* header =
        runtime->pb->ToAddress<::phaser::BufferOffset>(
            absolute_binary_offset);
    *header = runtime->pb->ToOffset(fields);
    runtime->AddMetadata(key, *header);
  }

  // Looks for the field number in the field data. Returns the offset of the
  // field if found, -1 otherwise.
  int32_t FindFieldOffset(uint32_t field_number) const {
    return FindField(field_number).offset;
  }

  // Similar for field id for presence bit mask.
  int32_t FindFieldId(uint32_t field_number) const {
    return FindField(field_number).id;
  }

  // Resolves both values in one metadata lookup.
  FieldLocation FindField(uint32_t field_number) const {
    if (runtime == nullptr) {
      return {};
    }
    // First 4 bytes of the message are the offset to the field data.
    const ::phaser::BufferOffset* field_data =
        runtime->ToAddress<::phaser::BufferOffset>(absolute_binary_offset);
    if (field_data == nullptr) {
      return {};
    }
    const ::phaser::BufferOffset metadata_offset = *field_data;
    const void* metadata = runtime->ToAddress<void>(metadata_offset);
    if (metadata == nullptr) {
      return {};
    }

    // The metadata tables are indexed using counts stored in the buffer, so we
    // must confirm those tables lie within the trusted buffer bounds before
    // dereferencing any entry (received data cannot be trusted).
    const size_t limit = runtime->TrustedSize();
    if (!internal::MetadataRangeFits(metadata_offset, 1, sizeof(uint32_t),
                                     limit)) {
      return {};
    }
    const uint32_t first_word = *static_cast<const uint32_t*>(metadata);
    if (first_word == kHybridFieldDataMagic) {
      const auto* hybrid = static_cast<const HybridFieldData*>(metadata);
      if (!internal::HybridTableFits(metadata_offset, hybrid, limit)) {
        return {};
      }
      return internal::FindHybridField(hybrid, field_number);
    }
    const auto* legacy = static_cast<const FieldData*>(metadata);
    if (!internal::LegacyTableFits(metadata_offset, legacy, limit)) {
      return {};
    }
    return internal::FindLegacyField(legacy, field_number);
  }

  void* BinaryData() const {
    SyncToPayload();
    return runtime->pb->ToAddress(absolute_binary_offset);
  }

  void* Data() const {
    SyncToPayload();
    return reinterpret_cast<void*>(runtime->pb);
  }

  size_t Size() const {
    SyncToPayload();
    return runtime->pb->Size();
  }
  size_t ZeroCopySize() const {
    SyncToPayload();
    return runtime->pb->Size();
  }

 protected:
  static RuntimeHandle BorrowRuntime(MessageRuntime& runtime) {
    return RuntimeHandle(RuntimeHandle(), &runtime);
  }

 private:
  // A borrowed runtime has no owner, so it lives in another handle that may
  // be destroyed first; each handle keeps its own copy.
  static bool IsBorrowed(const RuntimeHandle& rt) {
    return rt != nullptr && rt.use_count() == 0;
  }
  static MessageRuntime RuntimeToCopy(const RuntimeHandle& rt) {
    return IsBorrowed(rt) ? *rt : MessageRuntime(nullptr, size_t{0}, false);
  }

  void BindRuntime(const RuntimeHandle& other) {
    if (IsBorrowed(other)) {
      readonly_runtime_ = *other;
      runtime = BorrowRuntime(readonly_runtime_);
    } else {
      runtime = other;
    }
  }
  void BindRuntime(RuntimeHandle&& other) {
    if (IsBorrowed(other)) {
      readonly_runtime_ = *other;
      runtime = BorrowRuntime(readonly_runtime_);
    } else {
      runtime = std::move(other);
    }
  }
};

::phaser::PayloadBuffer* NewDynamicBuffer(
    size_t initial_size, Tuning tuning = Tuning::kPerformance);

absl::StatusOr<::phaser::PayloadBuffer*> NewDynamicBuffer(
    size_t initial_size, std::function<absl::StatusOr<void*>(size_t)> alloc,
    std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc,
    Tuning tuning = Tuning::kPerformance);

}  // namespace phaser
