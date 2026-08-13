// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"

namespace phaser {

// Message header.
// --------------
// Each message starts with a header which is:
// - The absolute offset of the FieldData for this message.
// - The presence mask - 1 bit per field.  This is variable in
//   size but is always a multiple of 32 bits.

// FieldData is a structure that contains the field numbers and offsets for a
// message. It is stored in the payload buffer.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
struct FieldData {
  uint32_t num;
  struct {
    uint32_t number;
    uint32_t offset : 24;  // Offset into message.
    uint32_t id : 8;       // Field id for presence bit mask.
  } fields[];  // Flexible array member; data lives in the payload buffer.
};
#pragma clang diagnostic pop

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
inline constexpr uint32_t kRuntimeControlVersion = 1;

struct RuntimeMetadataEntry {
  ::toolbelt::BufferOffset name = 0;
  ::toolbelt::BufferOffset field_data = 0;
  uint32_t name_size = 0;
};

struct RuntimeControlBlock {
  uint32_t magic = kRuntimeControlMagic;
  uint32_t version = kRuntimeControlVersion;
  ::toolbelt::BufferOffset user_metadata = 0;
  uint32_t count = 0;
  uint32_t capacity = 0;
  RuntimeMetadataEntry entries[1];
};

inline size_t RuntimeControlSize(size_t capacity) {
  return offsetof(RuntimeControlBlock, entries) +
         capacity * sizeof(RuntimeMetadataEntry);
}

inline RuntimeControlBlock* InitializeRuntimeControl(
    ::toolbelt::PayloadBuffer** pb, size_t capacity) {
  capacity = std::max<size_t>(capacity, 1);
  void* memory = ::toolbelt::PayloadBuffer::Allocate(
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
  MessageRuntime(::toolbelt::PayloadBuffer* p, bool is_mutable = false)
      : pb(p),
        is_mutable_(is_mutable),
        mode_(is_mutable ? RuntimeHandleMode::kFixedMutable
                         : RuntimeHandleMode::kBorrowedReadonly) {}
  MessageRuntime(::toolbelt::PayloadBuffer* p, size_t size,
                 bool is_mutable = false)
      : pb(p),
        buffer_size(size),
        is_mutable_(is_mutable),
        mode_(is_mutable ? RuntimeHandleMode::kFixedMutable
                         : RuntimeHandleMode::kBorrowedReadonly) {}
  MessageRuntime(const MessageRuntime&) = default;
  MessageRuntime& operator=(const MessageRuntime&) = default;
  virtual ~MessageRuntime() = default;
  ::toolbelt::PayloadBuffer* pb;

  // This is the size of the buffer.  If it is zero, the size is inside
  // the payload buffer.  If it's non-zero, it's the size of the received
  // buffer.  We can't rely on the size inside the payload buffer if we
  // are looking at received data (someone could set it to anything and we
  // have no way to check it's valid).
  size_t buffer_size = 0;
  bool is_mutable_ = false;
  RuntimeHandleMode mode_ = RuntimeHandleMode::kBorrowedReadonly;

  virtual void AddMetadata(std::string_view name,
                           ::toolbelt::BufferOffset offset) {
    RuntimeControlBlock* control = GetRuntimeControl();
    if (control == nullptr) {
      return;
    }
    if (control->count == control->capacity) {
      const ::toolbelt::BufferOffset old_offset = pb->metadata;
      const uint32_t new_capacity = control->capacity * 2;
      void* memory = ::toolbelt::PayloadBuffer::Realloc(
          &pb, pb->ToAddress(old_offset),
          static_cast<uint32_t>(RuntimeControlSize(new_capacity)), true);
      control = static_cast<RuntimeControlBlock*>(memory);
      control->capacity = new_capacity;
      pb->metadata = pb->ToOffset(control);
    }
    void* name_memory = ::toolbelt::PayloadBuffer::Allocate(
        &pb, static_cast<uint32_t>(name.size()), false);
    if (!name.empty()) {
      memcpy(name_memory, name.data(), name.size());
    }
    control = GetRuntimeControl();
    RuntimeMetadataEntry& entry = control->entries[control->count++];
    entry.name = pb->ToOffset(name_memory);
    entry.name_size = static_cast<uint32_t>(name.size());
    entry.field_data = offset;
  }
  virtual ::toolbelt::BufferOffset GetMetadata(std::string_view name) {
    RuntimeControlBlock* control = GetRuntimeControl();
    if (control == nullptr) {
      return 0;
    }
    for (uint32_t i = 0; i < control->count; ++i) {
      const RuntimeMetadataEntry& entry = control->entries[i];
      if (entry.name_size != name.size()) {
        continue;
      }
      const char* stored = pb->ToAddress<char>(entry.name);
      if (name.empty() || memcmp(stored, name.data(), name.size()) == 0) {
        return entry.field_data;
      }
    }
    return 0;
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

  template <typename T = void>
  T* ToAddress(toolbelt::BufferOffset offset) {
    return pb->ToAddress<T>(offset, buffer_size);
  }

  template <typename T = void>
  const T* ToAddress(toolbelt::BufferOffset offset) const {
    return pb->ToAddress<T>(offset, buffer_size);
  }

  template <typename T = void>
  toolbelt::BufferOffset ToOffset(const T* addr) const {
    return pb->ToOffset(addr, buffer_size);
  }

  template <typename T = void>
  toolbelt::BufferOffset ToOffset(T* addr) {
    return pb->ToOffset(addr, buffer_size);
  }
};

// This is a message runtime for a message that is mutable.  It holds a mapping
// for each message name to the offset of the metadata in the payload buffer.
struct MutableMessageRuntime : public MessageRuntime {
  MutableMessageRuntime(::toolbelt::PayloadBuffer* p)
      : MessageRuntime(p, true) {}
};

// Dynamically allocated payload buffer.  Must be allocated in memory
// from malloc using the NewDynamicBuffer function.
struct DynamicMutableMessageRuntime : public MutableMessageRuntime {
  DynamicMutableMessageRuntime(::toolbelt::PayloadBuffer* p,
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
  Message(RuntimeHandle rt, ::toolbelt::BufferOffset start)
      : readonly_runtime_(nullptr, size_t{0}, false),
        absolute_binary_offset(start) {
    BindRuntime(std::move(rt));
  }
  Message(const Message& other)
      : readonly_runtime_(nullptr, size_t{0}, false),
        absolute_binary_offset(other.absolute_binary_offset) {
    BindRuntime(other.runtime);
  }
  Message(Message&& other) noexcept
      : readonly_runtime_(nullptr, size_t{0}, false),
        absolute_binary_offset(other.absolute_binary_offset) {
    BindRuntime(std::move(other.runtime));
  }
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
  ::toolbelt::BufferOffset absolute_binary_offset = 0;

  // 'field' is the offset from the start of the message to the field (positive)
  // Subtract the field offset from the field to get the address of the
  // std::shared_ptr to the pointer to the ::toolbelt::PayloadBuffer.
  static ::toolbelt::PayloadBuffer* GetBuffer(const void* field,
                                              uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    return msg->runtime->pb;
  }

  static ::toolbelt::PayloadBuffer** GetBufferAddr(const void* field,
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

  static ::toolbelt::BufferOffset GetMessageBinaryStart(const void* field,
                                                        uint32_t offset) {
    const Message* msg = reinterpret_cast<const Message*>(
        reinterpret_cast<const char*>(field) - offset);
    return msg->absolute_binary_offset;
  }

  absl::Status SetUserMetadata(toolbelt::BufferOffset offset) {
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
    toolbelt::BufferOffset offset = runtime->pb->metadata;
    if (RuntimeControlBlock* control = runtime->GetRuntimeControl();
        control != nullptr) {
      offset = control->user_metadata;
    }
    return runtime->pb->ToAddress(offset);
  }

  void* Allocate(size_t size, size_t alignment = 4, bool clear = true) {
    (void)alignment;
    return toolbelt::PayloadBuffer::Allocate(
        &runtime->pb, static_cast<uint32_t>(size), clear);
  }

  void Free(void* ptr) { runtime->pb->Free(ptr); }

  void* Realloc(void* ptr, size_t size, size_t alignment = 4,
                bool clear = true) {
    (void)alignment;
    return toolbelt::PayloadBuffer::Realloc(&runtime->pb, ptr,
                                            static_cast<uint32_t>(size), clear);
  }

  toolbelt::BufferOffset ToOffset(void* addr) {
    return runtime->pb->ToOffset(addr);
  }

  template <typename T>
  T* ToAddress(toolbelt::BufferOffset offset) {
    return runtime->pb->ToAddress<T>(offset);
  }

  template <typename MessageType>
  void InstallMetadata() {
    auto metadata = runtime->GetMetadata(MessageType::FullName());
    if (metadata != 0) {
      ::toolbelt::BufferOffset* header =
          runtime->pb->ToAddress<::toolbelt::BufferOffset>(
              absolute_binary_offset);
      *header = metadata;
      return;
    }

    // Allocate space for field data in the payload buffer and copy it in.
    void* fields = ::toolbelt::PayloadBuffer::Allocate(
        &runtime->pb, sizeof(MessageType::field_data), false);
    memcpy(fields, &MessageType::field_data, sizeof(MessageType::field_data));
    ::toolbelt::BufferOffset* header =
        runtime->pb->ToAddress<::toolbelt::BufferOffset>(
            absolute_binary_offset);
    *header = runtime->pb->ToOffset(fields);
    runtime->AddMetadata(MessageType::FullName(), *header);
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
    const ::toolbelt::BufferOffset* field_data =
        runtime->ToAddress<::toolbelt::BufferOffset>(absolute_binary_offset);
    if (field_data == nullptr) {
      return {};
    }
    const void* metadata = runtime->ToAddress<void>(*field_data);
    if (metadata == nullptr) {
      return {};
    }

    const uint32_t first_word = *static_cast<const uint32_t*>(metadata);
    if (first_word == kHybridFieldDataMagic) {
      return internal::FindHybridField(
          static_cast<const HybridFieldData*>(metadata), field_number);
    }
    return internal::FindLegacyField(static_cast<const FieldData*>(metadata),
                                     field_number);
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
  void BindRuntime(RuntimeHandle other) {
    if (other != nullptr && other.use_count() == 0) {
      readonly_runtime_ = *other;
      runtime = BorrowRuntime(readonly_runtime_);
    } else {
      runtime = std::move(other);
    }
  }
};

::toolbelt::PayloadBuffer* NewDynamicBuffer(
    size_t initial_size, Tuning tuning = Tuning::kPerformance);

absl::StatusOr<::toolbelt::PayloadBuffer*> NewDynamicBuffer(
    size_t initial_size, std::function<absl::StatusOr<void*>(size_t)> alloc,
    std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc,
    Tuning tuning = Tuning::kPerformance);

}  // namespace phaser
