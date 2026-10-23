// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/payload_buffer.h"
#include "toolbelt/hexdump.h"

namespace phaser {
namespace internal {
[[noreturn]] inline void Fatal(const char* what) {
  (void)std::fprintf(stderr, "phaser: %s\n", what);
  std::abort();
}

inline absl::Status AllocationFailedError() {
  return absl::ResourceExhaustedError("phaser: payload buffer is full");
}
}  // namespace internal
}  // namespace phaser

// Reports a programming error such as an out-of-range index or a write to a
// readonly message.  Builds without exceptions print the error and abort.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#define PHASER_THROW(exception) throw exception
#else
#define PHASER_THROW(exception) ::phaser::internal::Fatal(#exception)
#endif

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

// A singular message field has no presence bit, so its id in field metadata
// is free to say how the message is stored: this id marks one stored inside
// the message holding it rather than through an offset.
inline constexpr int kInlineMessageFieldId = 1;

// An inline message starts at the first 8-byte boundary at or after 'end'.
constexpr uint32_t InlineMessageOffset(size_t end) {
  return static_cast<uint32_t>((end + 7) & ~size_t{7});
}

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

template <typename T, typename = void>
struct HasSyncPending : std::false_type {};
template <typename T>
struct HasSyncPending<
    T, std::void_t<decltype(std::declval<const T&>().PhaserSyncPending())>>
    : std::true_type {};

// Whether SyncToPayload() of 'message' might write anything, judged from its
// fields' handles alone.
template <typename T>
bool SyncPending(const T& message) {
  if constexpr (HasSyncPending<T>::value) {
    return message.PhaserSyncPending();
  } else {
    return HasSyncToPayloadWork<T>::value;
  }
}

// True for generated messages whose PhaserClearHandles() is all that is left
// to clear when the buffer holding them is reset.
template <typename T, typename = void>
struct ClearsHandlesAlone : std::false_type {};
template <typename T>
struct ClearsHandlesAlone<T,
                          std::void_t<decltype(T::PhaserClearsHandlesAlone())>>
    : std::bool_constant<T::PhaserClearsHandlesAlone()> {};
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
  if (memory == nullptr) {
    return nullptr;
  }
  auto* control = new (memory) RuntimeControlBlock;
  control->capacity = static_cast<uint32_t>(capacity);
  (*pb)->metadata = (*pb)->ToOffset(control);
  return control;
}

namespace internal {
template <typename T>
using FieldDataOf = std::remove_const_t<decltype(T::field_data)>;

// The field metadata of each of a list of message types, laid out as one
// block that a new buffer takes with a single copy.
template <typename... Ts>
struct MetadataTables {
  static void Record(RuntimeMetadataEntry*, ::phaser::BufferOffset) {}
};

template <typename T, typename... Rest>
struct MetadataTables<T, Rest...> {
  FieldDataOf<T> table = T::field_data;
  [[no_unique_address]] MetadataTables<Rest...> rest;

  // Fills an entry for each type whose table is in a block at 'offset'.
  static void Record(RuntimeMetadataEntry* entry,
                     ::phaser::BufferOffset offset) {
    static constexpr uint64_t kKey = MetadataKey(T::FullName());
    entry->key = kKey;
    entry->field_data = offset;
    MetadataTables<Rest...>::Record(
        entry + 1, offset + static_cast<::phaser::BufferOffset>(
                                offsetof(MetadataTables, rest)));
  }
};
}  // namespace internal

// Installing the metadata of every type a message can reach when its buffer
// is made could add tables for types it never uses, so it is only done when
// they are no bigger than this.
inline constexpr size_t kMaxEagerMetadataSize = 512;

// Sets up the metadata of a new buffer whose main message has been
// allocated.  'Root' is the main message's type and 'Reachable' the other
// message types it can reach.  Their tables are copied in as one block when
// they are small enough, so that making a message of a reachable type needs
// no allocation; otherwise each is copied in when first needed.
template <typename Root, typename... Reachable>
bool InitializeMetadata(::phaser::PayloadBuffer** pb) {
  constexpr size_t kTypeCount = 1 + sizeof...(Reachable);
  constexpr bool kEager =
      sizeof(internal::MetadataTables<Root, Reachable...>) <=
      kMaxEagerMetadataSize;
  using Tables =
      std::conditional_t<kEager, internal::MetadataTables<Root, Reachable...>,
                         internal::MetadataTables<Root>>;
  static constexpr Tables kTables{};

  if (pb == nullptr || *pb == nullptr || (*pb)->message == 0) {
    return false;
  }
  // A message that reaches no other type has no control block, so a null
  // control block is failure only when one was required.
  if constexpr (kTypeCount > 1) {
    if (InitializeRuntimeControl(pb, kTypeCount) == nullptr) {
      return false;
    }
  }
  void* block = ::phaser::PayloadBuffer::Allocate(
      pb, static_cast<uint32_t>(sizeof(kTables)), false);
  if (block == nullptr) {
    return false;
  }
  memcpy(block, &kTables, sizeof(kTables));
  const ::phaser::BufferOffset offset = (*pb)->ToOffset(block);
  if constexpr (kTypeCount > 1) {
    // Allocating the tables may have moved the buffer.
    auto* control = (*pb)->AddressOf<RuntimeControlBlock>((*pb)->metadata);
    if (control == nullptr) {
      return false;
    }
    Tables::Record(control->entries, offset);
    control->count = kEager ? static_cast<uint32_t>(kTypeCount) : 1;
  }
  // The first word of a message is the offset of its metadata.
  auto* header =
      (*pb)->template AddressOf<::phaser::BufferOffset>((*pb)->message);
  if (header == nullptr) {
    return false;
  }
  *header = offset;
  return true;
}

// A dynamic message's handles share its runtime.  Other handles copy this small
// runtime into themselves.
enum class RuntimeHandleMode : uint8_t {
  kBorrowedReadonly,
  kFixedMutable,
  kOwnedDynamic,
};

struct MessageRuntime {
  // A readonly runtime made without a size trusts the size in the buffer.
  MessageRuntime(::phaser::PayloadBuffer* p, bool is_mutable = false)
      : MessageRuntime(
            p, is_mutable || p == nullptr ? size_t{0} : size_t{p->full_size},
            is_mutable) {}
  // A readonly runtime trusts 'size' bytes of the buffer, clamped to what an
  // offset can reach.  As offset 0 is never valid, 1 trusts none, so the bound
  // is never 0.
  MessageRuntime(::phaser::PayloadBuffer* p, size_t size,
                 bool is_mutable = false)
      : pb(p),
        size_or_mark_(is_mutable ? 0
                                 : static_cast<uint32_t>(std::clamp<size_t>(
                                       size, 1, UINT32_MAX))),
        is_mutable_(is_mutable),
        mode_(is_mutable ? RuntimeHandleMode::kFixedMutable
                         : RuntimeHandleMode::kBorrowedReadonly) {}
  MessageRuntime(const MessageRuntime&) = default;
  MessageRuntime& operator=(const MessageRuntime&) = default;
  // Not virtual: the only derived runtime that is owned is deleted as its own
  // type.
  ~MessageRuntime() = default;
  ::phaser::PayloadBuffer* pb;

  // A readonly runtime's BufferSize(), or a mutable one's ResetMark().
  uint32_t size_or_mark_ = 0;
  bool is_mutable_ = false;
  RuntimeHandleMode mode_ = RuntimeHandleMode::kBorrowedReadonly;

  // The number of bytes of a readonly buffer that are trusted, or 0 for a
  // mutable buffer, whose size is the one inside the payload buffer.  We can't
  // rely on the size inside the payload buffer if we are looking at received
  // data (someone could set it to anything and we have no way to check it's
  // valid).
  size_t BufferSize() const { return is_mutable_ ? 0 : size_or_mark_; }

  // BufferSize() of a readonly runtime.  Being known not to be 0, it is not
  // replaced by the size inside the payload buffer.
  size_t ReadonlyLimit() const {
    const size_t limit = size_or_mark_;
    ABSL_ASSUME(limit != 0);
    return limit;
  }

  // The start of the free memory of a new buffer holding only its main
  // message and its metadata, or 0.  Everything allocated past it belongs
  // to the main message, so clearing that can free it all at once.
  ::phaser::BufferOffset ResetMark() const {
    return is_mutable_ ? size_or_mark_ : 0;
  }

  // Records that the buffer holds only a new main message and its metadata.
  void MarkNewBuffer() {
    if (!is_mutable_) {
      return;
    }
    size_or_mark_ = 0;
    if (pb == nullptr) {
      return;
    }
    for (const ::phaser::BufferOffset head : pb->bitmaps) {
      if (head != 0) {
        return;
      }
    }
    const ::phaser::BufferOffset free = pb->free_list;
    const auto* block = pb->UncheckedAddress<::phaser::FreeBlockHeader>(free);
    if (block == nullptr || block->next != 0 ||
        block->length != pb->full_size - free ||
        pb->hwm != free + sizeof(::phaser::FreeBlockHeader)) {
      return;
    }
    size_or_mark_ = free;
  }

  // MarkNewBuffer() for a buffer from NewFixedBuffer(), which is new.
  void MarkNewFixedBuffer() { size_or_mark_ = pb->free_list; }

  // Whether clearing the main message at 'message' may free everything
  // allocated since the buffer was new.  ResetFor() decides.
  bool MayResetFor(::phaser::BufferOffset message) const {
    return ResetMark() != 0 && message == pb->message;
  }

  // Frees everything allocated since the buffer was new and empties the
  // main message, of 'binary_size' bytes at 'message', but for its
  // metadata, and undoes a failed allocation.  Not if the metadata or
  // anything it refers to has reached past the mark, or after Finalize() of
  // a buffer with no failed allocation.  Returns whether it did.
  bool ResetFor(::phaser::BufferOffset message, size_t binary_size) {
    if (!MayResetFor(message)) {
      return false;
    }
    // Nothing allocated since the last reset leaves nothing to free, as
    // nothing is allocated below the mark.
    const ::phaser::BufferOffset mark = ResetMark();
    if ((pb->free_list != mark ||
         pb->hwm != mark + sizeof(::phaser::FreeBlockHeader)) &&
        !FreeSinceMark()) {
      return false;
    }
    memset(pb->AddressOf<char>(message) + sizeof(::phaser::BufferOffset), 0,
           binary_size - sizeof(::phaser::BufferOffset));
    return true;
  }

  // Frees everything allocated since the mark, if nothing that stays has
  // reached past it.  Returns whether it did.
  ABSL_ATTRIBUTE_NOINLINE bool FreeSinceMark() {
    const ::phaser::BufferOffset mark = ResetMark();
    // Finalize() empties the free list, and so does a failed allocation.
    if ((pb->free_list == 0 && !pb->AllocationFailed()) ||
        pb->metadata >= mark ||
        pb->full_size - sizeof(::phaser::FreeBlockHeader) < mark) {
      return false;
    }
    RuntimeControlBlock* control = GetRuntimeControl();
    if (control != nullptr) {
      // Growing in place can take the control block past the mark.
      if (pb->metadata + RuntimeControlSize(control->capacity) > mark ||
          control->user_metadata >= mark) {
        return false;
      }
      // The metadata of types first used after the mark is freed with the
      // rest.
      uint32_t kept = 0;
      for (uint32_t i = 0; i < control->count; ++i) {
        if (control->entries[i].field_data < mark) {
          control->entries[kept++] = control->entries[i];
        }
      }
      control->count = kept;
    }
    auto* block = pb->AddressOf<::phaser::FreeBlockHeader>(mark);
    block->length = pb->full_size - mark;
    block->next = 0;
    pb->free_list = mark;
    pb->hwm = mark + sizeof(::phaser::FreeBlockHeader);
    for (::phaser::BufferOffset& head : pb->bitmaps) {
      head = 0;
    }
    pb->ClearAllocationFailed();
    return true;
  }

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
      if (memory == nullptr) {
        return;
      }
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
  // 'hint' is the index at which 'key' was last found, in any buffer.
  ::phaser::BufferOffset GetMetadata(uint64_t key,
                                     std::atomic<uint32_t>& hint) const {
    const RuntimeControlBlock* control = GetRuntimeControl();
    if (control == nullptr) {
      return 0;
    }
    const uint32_t guess = hint.load(std::memory_order_relaxed);
    if (guess < control->count && control->entries[guess].key == key) {
      return control->entries[guess].field_data;
    }
    for (uint32_t i = 0; i < control->count; ++i) {
      if (control->entries[i].key == key) {
        hint.store(i, std::memory_order_relaxed);
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
    RuntimeControlBlock* control;
    if (is_mutable_) {
      // The metadata may be the user's rather than a control block, so it is
      // only known to lie inside the buffer.
      if (pb->full_size < sizeof(RuntimeControlBlock) ||
          pb->metadata > pb->full_size - sizeof(RuntimeControlBlock)) {
        return nullptr;
      }
      control = pb->UncheckedAddress<RuntimeControlBlock>(pb->metadata);
    } else {
      control =
          pb->ToAddress<RuntimeControlBlock>(pb->metadata, ReadonlyLimit());
    }
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
    return pb->ToAddress<T>(offset, ReadonlyLimit());
  }

  template <typename T = void>
  const T* ToAddress(::phaser::BufferOffset offset) const {
    if (is_mutable_) {
      return pb->UncheckedAddress<const T>(offset);
    }
    return pb->ToAddress<T>(offset, ReadonlyLimit());
  }

  // Number of bytes that are trusted to be available in the buffer.  On the
  // readonly receive path this is the size passed to CreateReadonly; otherwise
  // the buffer is one we own and full_size is authoritative.
  size_t TrustedSize() const {
    if (!is_mutable_) {
      return ReadonlyLimit();
    }
    return pb != nullptr ? size_t(pb->full_size) : 0;
  }

  // Size-aware string reader.  Uses the trusted buffer size so a hostile length
  // word in received data cannot cause an out-of-bounds read.
  std::string_view GetStringView(::phaser::BufferOffset header_offset) const {
    if (pb == nullptr) {
      return {};
    }
    if (is_mutable_) {
      return MutableStringView(header_offset);
    }
    return pb->GetStringView(header_offset, BufferSize());
  }

  size_t StringSize(::phaser::BufferOffset header_offset) const {
    if (pb == nullptr) {
      return 0;
    }
    if (is_mutable_) {
      return MutableStringView(header_offset).size();
    }
    return pb->StringSize(header_offset, BufferSize());
  }

  const char* StringData(::phaser::BufferOffset header_offset) const {
    if (pb == nullptr) {
      return nullptr;
    }
    if (is_mutable_) {
      return MutableStringView(header_offset).data();
    }
    return pb->StringData(header_offset, BufferSize());
  }

  // The string whose StringHeader is at 'header_offset' in a mutable buffer,
  // which this process built.
  std::string_view MutableStringView(
      ::phaser::BufferOffset header_offset) const {
    const ::phaser::BufferOffset str =
        *pb->UncheckedAddress<const ::phaser::BufferOffset>(header_offset);
    if (str == 0) {
      return {};
    }
    const auto* length = pb->UncheckedAddress<const uint32_t>(str);
    return std::string_view(reinterpret_cast<const char*>(length + 1),
                            *length);
  }

  // Size-aware presence-bit read, a byte at a time as PayloadBuffer writes
  // them.  A hostile field id or inflated full_size must not cause an
  // out-of-bounds load of the presence mask.
  bool IsPresent(uint32_t bit, uint32_t presence_mask_offset) const {
    if (pb == nullptr || bit == static_cast<uint32_t>(-1)) {
      return false;
    }
    const uint8_t* p = ToAddress<const uint8_t>(presence_mask_offset + bit / 8);
    if (p == nullptr) {
      return false;
    }
    return (*p & (1U << (bit % 8))) != 0;
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
    return pb->ToOffset(addr, ReadonlyLimit());
  }

  template <typename T = void>
  ::phaser::BufferOffset ToOffset(T* addr) {
    if (is_mutable_) {
      return pb->UncheckedOffset<T>(addr);
    }
    return pb->ToOffset(addr, ReadonlyLimit());
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
  ~DynamicMutableMessageRuntime() {
    if (free_ != nullptr) {
      pb->~PayloadBuffer();
      free_(pb);
    }
  }
  std::function<void(void*)> free_;
  // The handles that own the runtime; the last one deletes it.
  std::atomic<uint32_t> owners_{0};
};

// Only the runtime of a dynamic message is owned, by the handles of the
// message itself.  Any other handle borrows its runtime or holds a copy.
using RuntimeHandle = MessageRuntime*;

// A handle for part of a message, such as an element or a nested message,
// borrows the message's runtime instead of sharing its ownership, so it is
// valid only while a handle that owns the runtime exists.
inline RuntimeHandle BorrowedRuntime(const RuntimeHandle& runtime) {
  return runtime;
}

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

// What a field's Clear() becomes when the buffer of the main message holding
// it is reset: nothing if Clear() only changes the payload, which the reset
// does, or Clear() before the reset if it also changes the field's handle
// using the payload, or after it if it allocates, or ClearHandle() if it
// also changes the handle without the payload.
enum class ResetClear { kNone, kBefore, kAfter, kHandle };
template <typename F>
struct FieldResetClear
    : std::integral_constant<ResetClear, ResetClear::kBefore> {};
using NoResetClear = std::integral_constant<ResetClear, ResetClear::kNone>;
using ResetClearAfter = std::integral_constant<ResetClear, ResetClear::kAfter>;

template <typename F>
void ClearBeforeReset(F& field) {
  if constexpr (FieldResetClear<F>::value == ResetClear::kBefore) {
    field.Clear();
  } else if constexpr (FieldResetClear<F>::value == ResetClear::kHandle) {
    field.ClearHandle();
  }
}

template <typename F>
void ClearAfterReset(F& field) {
  if constexpr (FieldResetClear<F>::value == ResetClear::kAfter) {
    field.Clear();
  }
}

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
struct HasFieldData : std::false_type {};
template <typename T>
struct HasFieldData<T, std::void_t<decltype(T::field_data)>> : std::true_type {
};

template <typename T, typename = void>
struct HasReleaseAt : std::false_type {};
template <typename T>
struct HasReleaseAt<T, std::void_t<decltype(&T::PhaserReleaseAt)>>
    : std::true_type {};

// Frees everything the message of type T at 'offset' owns, but not the
// message itself.
template <typename T>
void ReleaseMessageContents(const RuntimeHandle& runtime,
                            ::phaser::BufferOffset offset) {
  if constexpr (!ClearReleasesNothing<T>::value) {
    if constexpr (HasReleaseAt<T>::value) {
      T::PhaserReleaseAt(runtime, offset);
    } else {
      T(BorrowedRuntime(runtime), offset).Clear();
    }
  }
}

// Frees the message of type T at 'offset' and everything it owns.
template <typename T>
void ReleaseMessageAt(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset offset) {
  ReleaseMessageContents<T>(runtime, offset);
  runtime->pb->FreeAt(offset);
}

// A vector of messages can have at most this many chunks after the first.
inline constexpr uint32_t kMaxMessageVectorChunks = 31;

// Where the elements of a vector of messages are, read from its
// MessageVectorBlock.  A default layout has no elements.
struct MessageVectorLayout {
  ::phaser::BufferOffset block = 0;
  uint32_t stride = 0;
  uint32_t first_capacity = 0;
  ::phaser::BufferOffset directory = 0;
  uint32_t num_chunks = 0;

  uint64_t Capacity() const { return uint64_t(first_capacity) << num_chunks; }
};

// The layout of the vector data at 'block' in a received buffer, or a
// default layout if the block, its first chunk or its directory is not
// inside the buffer.
inline MessageVectorLayout LoadReceivedMessageVectorLayout(
    const MessageRuntime& runtime, ::phaser::BufferOffset block) {
  const uint64_t size = runtime.TrustedSize();
  if (runtime.pb == nullptr || block == 0 ||
      uint64_t(block) + sizeof(::phaser::MessageVectorBlock) > size) {
    return {};
  }
  const char* base = reinterpret_cast<const char*>(runtime.pb);
  ::phaser::MessageVectorBlock b;
  memcpy(&b, base + block, sizeof(b));
  if (b.stride < sizeof(uint64_t) || b.stride % sizeof(uint64_t) != 0 ||
      b.first_capacity == 0 ||
      uint64_t(block) + sizeof(b) + uint64_t(b.first_capacity) * b.stride >
          size) {
    return {};
  }
  uint32_t num_chunks = 0;
  if (b.directory != 0) {
    if (uint64_t(b.directory) + sizeof(num_chunks) > size) {
      return {};
    }
    memcpy(&num_chunks, base + b.directory, sizeof(num_chunks));
    if (num_chunks > kMaxMessageVectorChunks ||
        uint64_t(b.directory) +
                (uint64_t(num_chunks) + 1) * sizeof(::phaser::BufferOffset) >
            size) {
      return {};
    }
  }
  return {block, b.stride, b.first_capacity, b.directory, num_chunks};
}

// Reads the layout of the vector data at 'block', which is not 0.  The
// layout of a received buffer is validated, and one that is not valid has no
// elements.
inline MessageVectorLayout LoadMessageVectorLayout(
    const MessageRuntime& runtime, ::phaser::BufferOffset block) {
  if (!runtime.IsMutable()) {
    return LoadReceivedMessageVectorLayout(runtime, block);
  }
  const char* base = reinterpret_cast<const char*>(runtime.pb);
  const auto* b =
      reinterpret_cast<const ::phaser::MessageVectorBlock*>(base + block);
  const uint32_t num_chunks =
      b->directory == 0
          ? 0
          : *reinterpret_cast<const uint32_t*>(base + b->directory);
  return {block, b->stride, b->first_capacity, b->directory, num_chunks};
}

// The offset of element 'index', which must be less than the layout's
// capacity.  Returns 0 if the element is not inside a received buffer.
inline ::phaser::BufferOffset MessageVectorElement(
    const MessageRuntime& runtime, const MessageVectorLayout& layout,
    uint32_t index) {
  if (index < layout.first_capacity) {
    // The layout checked that the first chunk is inside the buffer.
    return static_cast<::phaser::BufferOffset>(
        layout.block + sizeof(::phaser::MessageVectorBlock) +
        uint64_t(index) * layout.stride);
  }
  // Chunk k holds the elements from first_capacity << (k - 1), so k - 1 is
  // the shift that brings first_capacity's top bit to index's or one less.
  uint32_t shift = __builtin_clz(layout.first_capacity) - __builtin_clz(index);
  if ((uint64_t(layout.first_capacity) << shift) > index) {
    shift--;
  }
  // The layout checked that the directory is inside the buffer.
  ::phaser::BufferOffset chunk;
  memcpy(&chunk,
         reinterpret_cast<const char*>(runtime.pb) + layout.directory +
             (shift + 1) * sizeof(::phaser::BufferOffset),
         sizeof(chunk));
  if (chunk == 0) {
    return 0;
  }
  const uint64_t start = uint64_t(layout.first_capacity) << shift;
  const uint64_t offset =
      uint64_t(chunk) + (index - start) * uint64_t(layout.stride);
  if (!runtime.IsMutable() && offset + layout.stride > runtime.TrustedSize()) {
    return 0;
  }
  return static_cast<::phaser::BufferOffset>(offset);
}

// Frees what the elements of the message vector whose VectorHeader is at
// 'header' own, and the vector's data, leaving the vector empty.
template <typename T>
void ReleaseMessageVector(const RuntimeHandle& runtime,
                          ::phaser::BufferOffset header) {
  const ::phaser::VectorHeader* hdr =
      runtime->ToAddress<::phaser::VectorHeader>(header);
  if (hdr == nullptr || hdr->data == 0) {
    return;
  }
  const uint32_t count = hdr->num_elements;
  const MessageVectorLayout layout =
      LoadMessageVectorLayout(*runtime, hdr->data);
  if constexpr (!ClearReleasesNothing<T>::value) {
    // The fallback release can allocate and move the buffer, so each element
    // is found again from the layout's offsets.
    for (uint32_t i = 0; i < count && i < layout.Capacity(); i++) {
      const ::phaser::BufferOffset element =
          MessageVectorElement(*runtime, layout, i);
      if (element != 0) {
        ReleaseMessageContents<T>(runtime, element);
      }
    }
  }
  ::phaser::PayloadBuffer* pb = runtime->pb;
  for (uint32_t k = 1; k <= layout.num_chunks; k++) {
    pb->FreeAt(OffsetAt(pb, layout.directory +
                                k * sizeof(::phaser::BufferOffset)));
  }
  pb->FreeAt(layout.directory);
  pb->FreeAt(layout.block);
  ::phaser::VectorHeader* cleared =
      runtime->ToAddress<::phaser::VectorHeader>(header);
  cleared->num_elements = 0;
  cleared->data = 0;
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
    ReleaseMessageVector<T>(runtime, field);
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

// A message stored inside the message holding it is there if its metadata
// word is set.
template <typename T>
struct InlineMessageRelease : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    if (OffsetAt(runtime->pb, field) != 0) {
      ReleaseMessageContents<T>(runtime, field);
    }
  }
};
}  // namespace internal

struct InternalDefault {};
// Selects the constructor of a handle for part of a mutable message, such as
// an element Add() has just appended.  The handle borrows the runtime of the
// handle it was made from, so it is valid only while that handle exists.
struct MutablePart {};

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
      : Message(rt, start, CopiesRuntime(rt)) {}
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
  // 'rt' must be mutable.  The handle must not copy it: a handle that
  // points at itself has to stay in memory, where any store into the payload
  // might change it as far as the compiler knows.
  Message(MutablePart, const RuntimeHandle& rt, ::phaser::BufferOffset start)
      : readonly_runtime_(nullptr, size_t{0}, false),
        runtime(rt),
        absolute_binary_offset(start),
        field_locations_known_(true),
        mutable_(true) {}
  Message(const Message& other)
      : Message(other.runtime, other.absolute_binary_offset) {
    if (other.owns_runtime_) {
      ShareRuntime();
    }
  }
  Message(Message&& other) noexcept
      : Message(other.runtime, other.absolute_binary_offset) {
    if (other.owns_runtime_) {
      owns_runtime_ = true;
      other.owns_runtime_ = false;
      other.runtime = nullptr;
    }
  }
  Message& operator=(const Message& other) {
    if (this != &other) {
      if (other.owns_runtime_) {
        // Before releasing ours, which may be the same runtime.
        static_cast<DynamicMutableMessageRuntime*>(other.runtime)
            ->owners_.fetch_add(1, std::memory_order_relaxed);
      }
      ReleaseRuntime();
      BindRuntime(other.runtime);
      owns_runtime_ = other.owns_runtime_;
      absolute_binary_offset = other.absolute_binary_offset;
      field_locations_known_ = other.field_locations_known_;
      mutable_ = other.mutable_;
    }
    return *this;
  }
  Message& operator=(Message&& other) noexcept {
    if (this != &other) {
      ReleaseRuntime();
      BindRuntime(other.runtime);
      owns_runtime_ = other.owns_runtime_;
      if (other.owns_runtime_) {
        other.owns_runtime_ = false;
        other.runtime = nullptr;
      }
      absolute_binary_offset = other.absolute_binary_offset;
      field_locations_known_ = other.field_locations_known_;
      mutable_ = other.mutable_;
    }
    return *this;
  }
  virtual ~Message() { ReleaseRuntime(); }

  virtual const MessageInfo* GetMessageInfo() const { return nullptr; }
  virtual std::string GetName() const { return "Message"; }
  virtual std::string GetFullName() const { return "phaser.Message"; }
  virtual void Clear() {}
  virtual void CopyFrom(const Message& /*src*/) {}
  virtual void SyncToPayload() const {}

  Message* operator->() { return this; }
  const Message* operator->() const { return this; }
  bool IsBound() const { return runtime != nullptr; }

  // Whether the handle keeps its runtime, that of a dynamic message, alive.
  bool OwnsRuntime() const { return owns_runtime_; }

  RuntimeHandle runtime = nullptr;
  ::phaser::BufferOffset absolute_binary_offset = 0;

  // True if each field is known to be at its compiled offset with its
  // compiled presence id, so fields need not look themselves up.
  bool FieldLocationsKnown() const { return field_locations_known_; }

  // Whether the runtime is mutable, kept in the handle so that code using a
  // handle made mutable knows it is.  It would have to reread the runtime
  // after any store into the payload.
  bool IsMutable() const { return mutable_; }

  // For printing.  The fields of the message print at its indent.
  void Indent(int indent) const {
    print_indent_ = static_cast<int8_t>(print_indent_ + indent);
  }
  void SetIndent(int indent) const {
    print_indent_ = static_cast<int8_t>(indent);
  }
  int GetIndent() const { return print_indent_; }
  void PrintIndent(std::ostream& os) const {
    for (int i = 0; i < print_indent_; i++) {
      os << " ";
    }
  }

 private:
  // Fields are constructed after these, so they are set in the constructor.
  bool field_locations_known_ = false;
  bool mutable_ = false;
  bool owns_runtime_ = false;
  mutable int8_t print_indent_ = 0;

  // Comparing larger tables would cost more than the lookups it saves.
  static constexpr size_t kMaxComparedFieldDataSize = 256;

  bool MetadataIs(const void* field_data, size_t field_data_size) const {
    const ::phaser::PayloadBuffer* pb = runtime->pb;
    if (field_data_size > kMaxComparedFieldDataSize || pb == nullptr ||
        absolute_binary_offset == 0) {
      return false;
    }
    // The bounds hold the magic too, so they are checked first.
    const size_t limit = runtime->TrustedSize();
    if (!internal::MetadataRangeFits(absolute_binary_offset, 1,
                                     sizeof(::phaser::BufferOffset), limit) ||
        !pb->IsValidMagic()) {
      return false;
    }
    const char* base = reinterpret_cast<const char*>(pb);
    ::phaser::BufferOffset metadata_offset;
    memcpy(&metadata_offset, base + absolute_binary_offset,
           sizeof(metadata_offset));
    if (metadata_offset == 0 ||
        !internal::MetadataRangeFits(metadata_offset, 1, field_data_size,
                                     limit)) {
      return false;
    }
    return memcmp(base + metadata_offset, field_data, field_data_size) == 0;
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
    if (!msg->IsMutable()) {
      PHASER_THROW(std::logic_error("cannot mutate a readonly Phaser message"));
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

  // Returns the offset of the metadata of MessageType in the runtime's
  // buffer, copying it in if it is not there yet.  This can move the buffer.
  // Returns 0 if the buffer has no space for the metadata.
  template <typename MessageType>
  static ::phaser::BufferOffset MetadataFor(const RuntimeHandle& runtime) {
    if (const RuntimeControlBlock* control = runtime->GetRuntimeControl();
        control != nullptr) {
      const uint32_t guess =
          metadata_hint<MessageType>.load(std::memory_order_relaxed);
      if (guess < control->count &&
          control->entries[guess].key == MetadataKeyFor<MessageType>()) {
        return control->entries[guess].field_data;
      }
    }
    return FindOrAddMetadata<MessageType>(runtime);
  }

  // The index at which the metadata of MessageType was last found, in any
  // buffer.
  template <typename MessageType>
  static inline std::atomic<uint32_t> metadata_hint{0};

  template <typename MessageType>
  ABSL_ATTRIBUTE_NOINLINE static ::phaser::BufferOffset FindOrAddMetadata(
      const RuntimeHandle& runtime) {
    const uint64_t key = MetadataKeyFor<MessageType>();
    ::phaser::BufferOffset metadata =
        runtime->GetMetadata(key, metadata_hint<MessageType>);
    if (metadata != 0) {
      return metadata;
    }
    void* fields = ::phaser::PayloadBuffer::Allocate(
        &runtime->pb, sizeof(MessageType::field_data), false);
    if (fields == nullptr) {
      return 0;
    }
    memcpy(fields, &MessageType::field_data, sizeof(MessageType::field_data));
    metadata = runtime->pb->ToOffset(fields);
    runtime->AddMetadata(key, metadata);
    return metadata;
  }

  // Returns false, leaving the message without field metadata, if the buffer
  // has no space for it.
  template <typename MessageType>
  bool InstallMetadata() {
    const ::phaser::BufferOffset metadata = MetadataFor<MessageType>(runtime);
    if (metadata == 0) {
      return false;
    }
    auto* header = runtime->pb->template ToAddress<::phaser::BufferOffset>(
        absolute_binary_offset);
    if (header == nullptr) {
      return false;
    }
    *header = metadata;
    return true;
  }

  // True if an allocation in this message's buffer failed because a fixed
  // size buffer was full.  The message is then incomplete: setters that
  // needed space did nothing, and serializing the message fails.  A readonly
  // buffer shorter than the magic word has no flag to read.
  bool AllocationFailed() const {
    if (runtime == nullptr || runtime->pb == nullptr) {
      return false;
    }
    if (!runtime->IsMutable() &&
        runtime->BufferSize() < sizeof(uint32_t)) {
      return false;
    }
    return runtime->pb->AllocationFailed();
  }

  // Binds this handle to its own copy of 'rt'.  Used when an allocation
  // failed and the handle must not write into the caller's buffer.
  void BindDetached(MessageRuntime& rt, ::phaser::BufferOffset offset) {
    ReleaseRuntime();
    readonly_runtime_ = rt;
    runtime = &readonly_runtime_;
    owns_runtime_ = false;
    absolute_binary_offset = offset;
    field_locations_known_ = true;
    mutable_ = true;
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
    return &runtime;
  }

  // Points this handle at the new message at 'start' in the new runtime 'rt',
  // which it owns.
  void BindNewMutable(DynamicMutableMessageRuntime* rt,
                      ::phaser::BufferOffset start) {
    rt->owners_.fetch_add(1, std::memory_order_relaxed);
    ReleaseRuntime();
    runtime = rt;
    owns_runtime_ = true;
    absolute_binary_offset = start;
    field_locations_known_ = true;
    mutable_ = true;
  }

  // Points this handle, made without a runtime, at the new main message of
  // the new fixed buffer 'pb', in a runtime built in place.  A runtime built
  // elsewhere and copied in would be read straight after the narrower stores
  // that built it, and one wide load of them waits for them to reach the
  // cache.
  void BindNewFixed(::phaser::PayloadBuffer* pb) {
    ReleaseRuntime();
    readonly_runtime_ = MessageRuntime(pb, true);
    readonly_runtime_.MarkNewFixedBuffer();
    runtime = &readonly_runtime_;
    absolute_binary_offset = pb->message;
    field_locations_known_ = true;
    mutable_ = true;
  }

  // A runtime that may live in another handle is copied, since that handle
  // may move or be destroyed first.  A dynamic runtime is used in place
  // because its buffer can move; the message that owns it keeps it alive.
  static bool CopiesRuntime(const RuntimeHandle& rt) {
    return rt != nullptr && rt->Mode() != RuntimeHandleMode::kOwnedDynamic;
  }

 public:
  // Points this handle, made for a message of type T inside another message,
  // at the message at 'start' in that message's runtime 'rt'.  It borrows
  // 'rt', which must outlive it.
  template <typename T>
  void BindBorrowed(const RuntimeHandle& rt, ::phaser::BufferOffset start) {
    ReleaseRuntime();
    runtime = rt;
    absolute_binary_offset = start;
    mutable_ = rt != nullptr && rt->IsMutable();
    field_locations_known_ = mutable_;
    if constexpr (internal::HasFieldData<T>::value) {
      if (!field_locations_known_ && rt != nullptr) {
        field_locations_known_ =
            MetadataIs(&T::field_data, sizeof(T::field_data));
      }
    }
  }

  // BindBorrowed() where 'rt' is mutable.
  void BindBorrowedMutable(const RuntimeHandle& rt,
                           ::phaser::BufferOffset start) {
    ReleaseRuntime();
    runtime = rt;
    absolute_binary_offset = start;
    mutable_ = true;
    field_locations_known_ = true;
  }

 private:
  // 'copy' is CopiesRuntime(rt), computed once since copying the runtime
  // could change what 'rt' points to as far as the compiler knows.
  Message(const RuntimeHandle& rt, ::phaser::BufferOffset start, bool copy)
      : readonly_runtime_(copy ? *rt
                               : MessageRuntime(nullptr, size_t{0}, false)),
        runtime(copy ? BorrowRuntime(readonly_runtime_) : rt),
        absolute_binary_offset(start),
        field_locations_known_(runtime != nullptr && runtime->IsMutable()),
        mutable_(runtime != nullptr && runtime->IsMutable()) {}

  void BindRuntime(const RuntimeHandle& other) {
    if (CopiesRuntime(other)) {
      readonly_runtime_ = *other;
      runtime = BorrowRuntime(readonly_runtime_);
    } else {
      runtime = other;
    }
  }

  // Makes this handle, which points at an owned runtime, another owner.
  void ShareRuntime() {
    static_cast<DynamicMutableMessageRuntime*>(runtime)->owners_.fetch_add(
        1, std::memory_order_relaxed);
    owns_runtime_ = true;
  }

  void ReleaseRuntime() {
    if (ABSL_PREDICT_FALSE(owns_runtime_)) {
      owns_runtime_ = false;
      ReleaseOwnedRuntime(runtime);
    }
  }

  ABSL_ATTRIBUTE_NOINLINE static void ReleaseOwnedRuntime(MessageRuntime* rt) {
    auto* owned = static_cast<DynamicMutableMessageRuntime*>(rt);
    if (owned->owners_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete owned;
    }
  }
};

// The start of a new fixed-size buffer holding a new message: the buffer's
// header, the message, its metadata and the header of the free block after
// them.  Only the buffer's size and the length of that free block depend on
// the size of the buffer, so a new buffer of any size the image fits in can
// be copied from it.
class NewBufferImage {
 public:
  // 'make' makes a new buffer, with the small block allocator if 'bitmaps',
  // in the 'size' bytes at 'addr', or returns null if it does not fit.
  using Maker = ::phaser::PayloadBuffer* (*)(void* addr, size_t size,
                                             bool bitmaps);
  explicit NewBufferImage(Maker make);

  // The new buffer in the 'size' bytes at 'addr', or null if the image does
  // not fit in them.
  ::phaser::PayloadBuffer* CopyTo(void* addr, size_t size) const;

 private:
  std::unique_ptr<char[]> bytes_;
  uint32_t size_ = 0;
};

// True if the 'size' bytes at 'addr' can hold an empty fixed-size buffer.
inline bool CanHoldFixedBuffer(const void* addr, size_t size) {
  return addr != nullptr &&
         size >= sizeof(::phaser::PayloadBuffer) +
                     sizeof(::phaser::FreeBlockHeader) &&
         size <= UINT32_MAX;
}

template <typename Root, typename... Reachable>
::phaser::PayloadBuffer* MakeFixedBuffer(void* addr, size_t size,
                                         bool bitmaps) {
  if (!CanHoldFixedBuffer(addr, size)) {
    return nullptr;
  }
  auto* pb = new (addr)
      ::phaser::PayloadBuffer(static_cast<uint32_t>(size), bitmaps);
  if (::phaser::PayloadBuffer::AllocateMainMessage(&pb, Root::BinarySize()) ==
          nullptr ||
      !InitializeMetadata<Root, Reachable...>(&pb)) {
    return nullptr;
  }
  return pb;
}

// A new fixed-size buffer in the 'size' bytes at 'addr', holding a new Root,
// which reaches the message types 'Reachable', or null if they do not fit.
template <typename Root, typename... Reachable>
::phaser::PayloadBuffer* NewFixedBuffer(void* addr, size_t size,
                                        Tuning tuning) {
  if (tuning == Tuning::kPerformance) {
    // Not destroyed, as messages can be made by other static destructors.
    static const absl::NoDestructor<NewBufferImage> image(
        &MakeFixedBuffer<Root, Reachable...>);
    if (::phaser::PayloadBuffer* pb = image->CopyTo(addr, size)) {
      return pb;
    }
  }
  return MakeFixedBuffer<Root, Reachable...>(addr, size,
                                             tuning == Tuning::kPerformance);
}

// Why NewFixedBuffer() returned null: InvalidArgument if the memory cannot
// hold a buffer, ResourceExhausted if the buffer has no room for the
// message's fixed parts.
inline absl::Status NewFixedBufferError(const void* addr, size_t size) {
  if (!CanHoldFixedBuffer(addr, size)) {
    return absl::InvalidArgumentError("phaser: buffer is too small");
  }
  return internal::AllocationFailedError();
}

namespace internal {

// A mutable runtime and the offset of an empty message in it.
struct AllocationFailureSink {
  MessageRuntime* runtime;
  ::phaser::BufferOffset message;
};

// An empty MessageType in detached per-thread storage, for a handle whose
// allocation failed.  Callers that must hand back a writable message use this
// so writes through the handle stay in bounds.  The owning buffer has already
// recorded the failure, and nothing reads the storage back.  Allocations made
// through the sink always fail because its buffer has no free space.
template <typename MessageType>
ABSL_ATTRIBUTE_NOINLINE AllocationFailureSink AllocationFailureSinkFor() {
  constexpr size_t kSize =
      (sizeof(::phaser::PayloadBuffer) + MessageType::BinarySize() + 64 + 7) &
      ~size_t{7};
  alignas(8) static thread_local char storage[kSize];
  static thread_local MessageRuntime runtime(nullptr);
  auto* pb = new (storage)
      ::phaser::PayloadBuffer(static_cast<uint32_t>(kSize), false);
  if (::phaser::PayloadBuffer::AllocateMainMessage(
          &pb, MessageType::BinarySize()) == nullptr) {
    Fatal("allocation failure sink is too small");
  }
  // The sink is full: further allocations must fail rather than reuse the
  // space just taken by the empty message.
  pb->MarkAllocationFailed();
  runtime = MessageRuntime(pb, true);
  return {&runtime, pb->message};
}

// Binds 'msg' to its own copy of the sink's runtime.
template <typename MessageType>
ABSL_ATTRIBUTE_NOINLINE void BindToAllocationFailureSink(MessageType& msg) {
  const AllocationFailureSink sink = AllocationFailureSinkFor<MessageType>();
  msg.BindDetached(*sink.runtime, sink.message);
}

}  // namespace internal

::phaser::PayloadBuffer* NewDynamicBuffer(
    size_t initial_size, Tuning tuning = Tuning::kPerformance);

absl::StatusOr<::phaser::PayloadBuffer*> NewDynamicBuffer(
    size_t initial_size, std::function<absl::StatusOr<void*>(size_t)> alloc,
    std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc,
    Tuning tuning = Tuning::kPerformance);

}  // namespace phaser
