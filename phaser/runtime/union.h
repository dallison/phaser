// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Union fields.
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "toolbelt/payload_buffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace phaser {

class UnionMemberField {
protected:
  ::toolbelt::PayloadBuffer *
  GetBuffer(const std::shared_ptr<MessageRuntime> &runtime) const {
    return runtime->pb;
  }

  ::toolbelt::PayloadBuffer **
  GetBufferAddr(const std::shared_ptr<MessageRuntime> &runtime) const {
    return &runtime->pb;
  }
};

#define DEFINE_PRIMITIVE_UNION_FIELD(cname, type)                              \
  template <bool FixedSize = false, bool Signed = false>                       \
  class Union##cname##Field : public UnionMemberField {                        \
  public:                                                                      \
    Union##cname##Field() = default;                                           \
    type Get(const std::shared_ptr<MessageRuntime> &runtime,                   \
             uint32_t abs_offset) const {                                      \
      if (runtime == nullptr) {                                                \
        return type();                                                         \
      }                                                                        \
      return GetBuffer(runtime)->template Get<type>(abs_offset);               \
    }                                                                          \
    void Print(std::ostream &os, int indent,                                   \
               const std::shared_ptr<MessageRuntime> &runtime,                 \
               uint32_t abs_offset) const {                                    \
      os << Get(runtime, abs_offset);                                          \
    }                                                                          \
    void Set(type v, const std::shared_ptr<MessageRuntime> &runtime,           \
             uint32_t abs_offset) {                                            \
      GetBuffer(runtime)->Set(abs_offset, v);                                  \
    }                                                                          \
    void SetOffset(const std::shared_ptr<MessageRuntime> &runtime,             \
                   uint32_t abs_offset, toolbelt::BufferOffset msg_offset) {}  \
                                                                               \
    bool Equal(const Union##cname##Field &other,                               \
               const std::shared_ptr<MessageRuntime> &runtime,                 \
               uint32_t abs_offset) {                                          \
      return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);       \
    }                                                                          \
    void Clear(const std::shared_ptr<MessageRuntime> &runtime,                 \
               uint32_t abs_offset) {}                                         \
    size_t SerializedSize(int number,                                          \
                          const std::shared_ptr<MessageRuntime> &runtime,      \
                          uint32_t abs_offset) const {                         \
      if constexpr (FixedSize) {                                               \
        return ProtoBuffer::TagSize(number,                                    \
                                    ProtoBuffer::FixedWireType<type>()) +      \
               sizeof(type);                                                   \
      } else {                                                                 \
        return ProtoBuffer::TagSize(number, WireType::kVarint) +               \
               ProtoBuffer::VarintSize<type, Signed>(                          \
                   Get(runtime, abs_offset));                                  \
      }                                                                        \
    }                                                                          \
    absl::Status Serialize(int number, ProtoBuffer &buffer,                    \
                           const std::shared_ptr<MessageRuntime> &runtime,     \
                           uint32_t abs_offset) const {                        \
      if constexpr (FixedSize) {                                               \
        return buffer.SerializeFixed<type>(number, Get(runtime, abs_offset));  \
      } else {                                                                 \
        return buffer.SerializeVarint<type, Signed>(number,                    \
                                                    Get(runtime, abs_offset)); \
      }                                                                        \
    }                                                                          \
    absl::Status Deserialize(ProtoBuffer &buffer,                              \
                             const std::shared_ptr<MessageRuntime> &runtime,   \
                             uint32_t abs_offset) {                            \
      absl::StatusOr<type> v;                                                  \
      if constexpr (FixedSize) {                                               \
        v = buffer.DeserializeFixed<type>();                                   \
      } else {                                                                 \
        v = buffer.DeserializeVarint<type, Signed>();                          \
      }                                                                        \
      if (!v.ok()) {                                                           \
        return v.status();                                                     \
      }                                                                        \
      Set(*v, runtime, abs_offset);                                            \
      return absl::OkStatus();                                                 \
    }                                                                          \
    constexpr WireType GetWireType() {                                         \
      if constexpr (FixedSize) {                                               \
        return WireType::kFixed64;                                             \
      } else {                                                                 \
        return WireType::kVarint;                                              \
      }                                                                        \
    }                                                                          \
  };

DEFINE_PRIMITIVE_UNION_FIELD(Int32, int32_t)
DEFINE_PRIMITIVE_UNION_FIELD(Uint32, uint32_t)
DEFINE_PRIMITIVE_UNION_FIELD(Int64, int64_t)
DEFINE_PRIMITIVE_UNION_FIELD(Uint64, uint64_t)
DEFINE_PRIMITIVE_UNION_FIELD(Double, double)
DEFINE_PRIMITIVE_UNION_FIELD(Float, float)
DEFINE_PRIMITIVE_UNION_FIELD(Bool, bool)

#undef DEFINE_PRIMITIVE_UNION_FIELD

template <typename Enum = int, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser>
class UnionEnumField : public UnionMemberField {
public:
  using T = typename std::underlying_type<Enum>::type;
  UnionEnumField() = default;

  Enum Get(const std::shared_ptr<MessageRuntime> &runtime,
           uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return static_cast<Enum>(0);
    }
    return static_cast<Enum>(
        GetBuffer(runtime)
            ->template Get<typename std::underlying_type<Enum>::type>(
                abs_offset));
  }

  void Print(std::ostream &os, int indent,
             const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) const {
    os << Stringizer()(Get(runtime, abs_offset));
  }

  T GetUnderlying(const std::shared_ptr<MessageRuntime> &runtime,
                  uint32_t abs_offset) const {
    return GetBuffer(runtime)
        ->template Get<typename std::underlying_type<Enum>::type>(abs_offset);
  }

  void SetOffset(const std::shared_ptr<MessageRuntime> &runtime, uint32_t abs_offset,
           toolbelt::BufferOffset msg_offset) {}

  void Set(Enum e, const std::shared_ptr<MessageRuntime> &runtime,
           uint32_t abs_offset) {
    GetBuffer(runtime)->Set(
        abs_offset, static_cast<typename std::underlying_type<Enum>::type>(e));
  }

  void Set(T e, const std::shared_ptr<MessageRuntime> &runtime,
           uint32_t abs_offset) {
    GetBuffer(runtime)->Set(abs_offset, e);
  }

  bool Equal(const UnionEnumField<Enum, Stringizer, Parser> &other,
             const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) {
    return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);
  }
  void Clear(const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) {}

  size_t SerializedSize(int number,
                        const std::shared_ptr<MessageRuntime> &runtime,
                        uint32_t abs_offset) const {
    return ProtoBuffer::TagSize(number, WireType::kVarint) +
           ProtoBuffer::VarintSize<T, false>(
               GetUnderlying(runtime, abs_offset));
  }

  absl::Status Serialize(int number, ProtoBuffer &buffer,
                         const std::shared_ptr<MessageRuntime> &runtime,
                         uint32_t abs_offset) const {
    return buffer.SerializeVarint<T, false>(number,
                                            GetUnderlying(runtime, abs_offset));
  }

  absl::Status Deserialize(ProtoBuffer &buffer,
                           const std::shared_ptr<MessageRuntime> &runtime,
                           uint32_t abs_offset) {
    absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
    if (!v.ok()) {
      return v.status();
    }
    Set(static_cast<Enum>(*v), runtime, abs_offset);
    return absl::OkStatus();
  }
  constexpr WireType GetWireType() { return WireType::kVarint; }
};

// The union contains an offset to the string data (length and bytes).
class UnionStringField : public UnionMemberField {
public:
  UnionStringField() = default;

  std::string_view Get(const std::shared_ptr<MessageRuntime> &runtime,
                       uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return "";
    }
    return GetBuffer(runtime)->GetStringView(abs_offset);
  }

  void Print(std::ostream &os, int indent,
             const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) const {
    os << "\"" << std::string(Get(runtime, abs_offset)) << "\"";
  }

  bool IsPresent(const std::shared_ptr<MessageRuntime> &runtime,
                 uint32_t abs_offset) const {
    const ::toolbelt::BufferOffset *addr =
        runtime->ToAddress<const ::toolbelt::BufferOffset>(abs_offset);
    return *addr != 0;
  }

  void SetOffset(const std::shared_ptr<MessageRuntime> &runtime,
                 uint32_t abs_offset, toolbelt::BufferOffset msg_offset) {}

  template <typename Str>
  void Set(Str s, const std::shared_ptr<MessageRuntime> &runtime,
           uint32_t abs_offset) {
    ::toolbelt::PayloadBuffer::SetString(GetBufferAddr(runtime), s, abs_offset);
  }

  absl::Span<char> Allocate(size_t size,
                            const std::shared_ptr<MessageRuntime> &runtime,
                            uint32_t abs_offset) {
    return ::toolbelt::PayloadBuffer::AllocateString(GetBufferAddr(runtime),
                                                     size, abs_offset);
  }

  bool Equal(const UnionStringField &other,
             const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) {
    return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);
  }

  size_t size(const std::shared_ptr<MessageRuntime> &runtime,
              uint32_t abs_offset) const {
    return GetBuffer(runtime)->StringSize(abs_offset);
  }

  const char *data(const std::shared_ptr<MessageRuntime> &runtime,
                   uint32_t abs_offset) const {
    return GetBuffer(runtime)->StringData(abs_offset);
  }
  void Clear(const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) {
    ::toolbelt::PayloadBuffer::ClearString(GetBufferAddr(runtime), abs_offset);
  }

  size_t SerializedSize(int number,
                        const std::shared_ptr<MessageRuntime> &runtime,
                        uint32_t abs_offset) const {
    size_t sz = size(runtime, abs_offset);
    return ProtoBuffer::TagSize(number, WireType::kLengthDelimited) +
           ProtoBuffer::VarintSize<int32_t, false>(sz) + sz;
  }

  absl::Status Serialize(int number, ProtoBuffer &buffer,
                         const std::shared_ptr<MessageRuntime> &runtime,
                         uint32_t abs_offset) const {
    return buffer.SerializeLengthDelimited(number, data(runtime, abs_offset),
                                           size(runtime, abs_offset));
  }

  absl::Status Deserialize(ProtoBuffer &buffer,
                           const std::shared_ptr<MessageRuntime> &runtime,
                           uint32_t abs_offset) {
    absl::StatusOr<std::string_view> v = buffer.DeserializeString();
    if (!v.ok()) {
      return v.status();
    }
    ::toolbelt::PayloadBuffer::SetString(GetBufferAddr(runtime), *v,
                                         abs_offset);
    return absl::OkStatus();
  }

  constexpr WireType GetWireType() { return WireType::kLengthDelimited; }
};

template <typename MessageType>
class UnionMessageField : public UnionMemberField {
public:
  UnionMessageField() : msg_(InternalDefault{}) {}

  const MessageType &Get(const std::shared_ptr<MessageRuntime> &runtime,
                         uint32_t abs_offset) const {
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr || *addr == 0) {
      return msg_;
    }
    // Populate msg_ with the information from the message.
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = *addr;
    return msg_;
  }

  void Print(std::ostream &os, int indent,
             const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) const {
    os << "{\n";
    auto msg = Get(runtime, abs_offset);
    msg.Indent(2);
    os << msg;
    msg.Indent(-2);
    for (int i = 0; i < indent; i++) {
      os << " ";
    }
    os << "}";
  }

  bool IsPresent(const std::shared_ptr<MessageRuntime> &runtime,
                 uint32_t abs_offset) const {
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    return addr == nullptr || *addr != 0;
  }

  MessageType *Mutable(const std::shared_ptr<MessageRuntime> &runtime,
                       uint32_t abs_offset) {
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr || *addr != 0) {
      // Already allocated.
      return &msg_;
    }
    // Allocate a new message.
    void *msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize(), 8);
    ::toolbelt::BufferOffset msg_offset =
        GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = msg_offset;
    msg_.template InstallMetadata<MessageType>();

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(runtime, abs_offset);
    if (addr != nullptr) {
      *addr = msg_offset; // Put message field offset into message.
    }
    return &msg_;
  }

  void SetOffset(const std::shared_ptr<MessageRuntime> &runtime, uint32_t abs_offset,
           toolbelt::BufferOffset msg_offset) {
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    *addr = msg_offset;
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = msg_offset;
  }

  absl::Status SerializeToBuffer(ProtoBuffer &buffer) const {
    return msg_.SerializeToBuffer(buffer);
  }

  absl::Status DeserializeFromBuffer(ProtoBuffer &buffer) {
    return msg_.DeserializeFromBuffer(buffer);
  }

  void Set(const MessageType &msg,
           const std::shared_ptr<MessageRuntime> &runtime,
           uint32_t abs_offset) {
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    if (*addr != 0) {
      msg_.Clear();
      GetBuffer(runtime)->Free(runtime->ToAddress(*addr));
    }
    // Allocate a new message.
    void *msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize(), 8);
    ::toolbelt::BufferOffset msg_offset =
        GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = msg_offset;
    msg_.template InstallMetadata<MessageType>();

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    *addr = msg_offset; // Put message field offset into message.

    // TODO: what to do here if this fails?
    (void)msg_.CloneFrom(msg);
  }

  void Clear(const std::shared_ptr<MessageRuntime> &runtime,
             uint32_t abs_offset) {
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    if (*addr != 0) {
      msg_.Clear();
      GetBuffer(runtime)->Free(runtime->ToAddress(*addr));
      *addr = 0;
    }
  }

  constexpr WireType GetWireType() { return WireType::kLengthDelimited; }

  size_t SerializedSize(int number,
                        const std::shared_ptr<MessageRuntime> &runtime,
                        uint32_t abs_offset) const {
    return ProtoBuffer::LengthDelimitedSize(
        number, Get(runtime, abs_offset).SerializedSize());
  }

  absl::Status Serialize(int number, ProtoBuffer &buffer,
                         const std::shared_ptr<MessageRuntime> &runtime,
                         uint32_t abs_offset) const {
    if (absl::Status status = buffer.SerializeLengthDelimitedHeader(
            number, Get(runtime, abs_offset).SerializedSize());
        !status.ok()) {
      return status;
    }
    return Get(runtime, abs_offset).Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer &buffer,
                           const std::shared_ptr<MessageRuntime> &runtime,
                           uint32_t abs_offset) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    void *msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize(), 8);
    ::toolbelt::BufferOffset msg_offset =
        GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = msg_offset;
    msg_.template InstallMetadata<MessageType>();

    // Buffer might have moved, get address of indirect again.
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return absl::OkStatus();
    }
    *addr = msg_offset; // Put message field offset into message.
    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

private:
  ::toolbelt::BufferOffset *
  GetIndirectAddress(const std::shared_ptr<MessageRuntime> &runtime,
                     uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return nullptr;
    }
    return runtime->template ToAddress<::toolbelt::BufferOffset>(abs_offset);
  }

  mutable MessageType msg_;
}; // namespace phaser

// All member of the tuple must be union fields.  These are stored in a
// std::tuple which does not store them inline so they need to contain the
// buffer shared pointer and the offset of the message binary data.
//
// In binary, this is stored as a 4 byte integer containing the discriminator
// (the field number of the tuple) followed by the binary data for the
// field itself.  The size of the field data is the max of all the fields
// in the union.
template <typename... T> class UnionField : public Field {
public:
  UnionField() = default;
  UnionField(uint32_t source_offset, uint32_t relative_binary_offset, int id,
             int number, std::vector<int> field_numbers)
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset),
        field_numbers_(field_numbers) {}

  template <int Id, typename F> const F &GetReference() const {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return std::get<Id>(value_).Get(nullptr, 0);
    }
    return std::get<Id>(value_).Get(GetRuntime(), GetMessageBinaryStart() +
                                                      relative_offset + 4);
  }

  template <int Id, typename F> F GetValue() const {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return std::get<Id>(value_).Get(nullptr, 0);
    }
    return std::get<Id>(value_).Get(GetRuntime(), GetMessageBinaryStart() +
                                                      relative_offset + 4);
  }

  template <int Id> void Print(std::ostream &os) const {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return;
    }
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_offset);
    if (*discrim != field_numbers_[Id]) {
      return;
    }
    std::get<Id>(value_).Print(os, indent_, GetRuntime(),
                               GetMessageBinaryStart() + relative_offset + 4);
  }

  template <int Id, typename U> void Set(const U &v) {
    // Write the field number into the discriminator.
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];
    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto &t = std::get<Id>(value_);
    t.Set(v, GetRuntime(),
          GetMessageBinaryStart() + relative_binary_offset_ + 4);
  }

  template <int Id, typename U> U *Mutable() {
    // Write the field number into the discriminator.
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto &t = std::get<Id>(value_);
    return t.Mutable(GetRuntime(),
                     GetMessageBinaryStart() + relative_binary_offset_ + 4);
  }

  // Only valid for strings and bytes.
  template <int Id> absl::Span<char> Allocate(size_t size) {
    // Write the field number into the discriminator.
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto &t = std::get<Id>(value_);
    return t.Allocate(size, GetRuntime(),
                      GetMessageBinaryStart() + relative_binary_offset_ + 4);
  }

  bool Is(int number) const { return Discriminator() == number; }

  int32_t Discriminator() const {
    // The offset of all the fields is the offset to the discriminator but we
    // don't know which field is actually present in the message.  We need to
    // search until we find one that is present and use the offset of that
    // field.  It is very likely that the first one we look at will be
    // present as they will only be absent if the message definition has been
    // changed and the field removed. In the worst case, the complete union
    // has been removed and none of the fields are present.
    const Message *msg = Message::GetMessage(this, source_offset_);
    int32_t relative_offset = -1;
    for (auto &field_number : field_numbers_) {
      relative_offset = msg->FindFieldOffset(field_number);
      if (relative_offset >= 0) {
        break;
      }
    }
    if (relative_offset < 0) {
      return 0; // No field present in message (all fields have been removed).
    }
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_offset);
    return *discrim;
  }

  template <int Id> void Clear() {
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    int field_number = field_numbers_[Id];
    if (*discrim != field_number) {
      return;
    }
    auto &t = std::get<Id>(value_);
    t.Clear(GetRuntime(),
            GetMessageBinaryStart() + relative_binary_offset_ + 4);
    *discrim = 0;
  }

  template <int Id> size_t SerializedSize(int discriminator) const {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) { // Field not present.
      return 0;
    }
    return std::get<Id>(value_).SerializedSize(discriminator, GetRuntime(),
                                               GetMessageBinaryStart() +
                                                   relative_offset + 4);
  }

  template <int Id>
  absl::Status Serialize(int discriminator, ProtoBuffer &buffer) const {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) { // Field not present.
      return absl::OkStatus();
    }
    return std::get<Id>(value_).Serialize(discriminator, buffer, GetRuntime(),
                                          GetMessageBinaryStart() +
                                              relative_offset + 4);
  }

  template <int Id>
  absl::Status Deserialize(int discriminator, ProtoBuffer &buffer) {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) { // Field not present.
      return absl::OkStatus();
    }
    if (absl::Status status = std::get<Id>(value_).Deserialize(
            buffer, GetRuntime(),
            GetMessageBinaryStart() + relative_offset + 4);
        !status.ok()) {
      return status;
    }
    // Set the discriminator.
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];
    return absl::OkStatus();
  }

  template <int Id, typename M> absl::Status CloneFrom(const M &other) {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) { // Field not present.
      return absl::OkStatus();
    }
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];
    // TODO: this isn't right.  If the field is a message, it can fail to clone.
    std::get<Id>(value_).Set(other, GetRuntime(),
                             GetMessageBinaryStart() + relative_offset + 4);
    return absl::OkStatus();
  }

  template <int Id> bool IsPresent() const {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return false;
    }
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_offset);
    return *discrim == field_numbers_[Id];
  }

  template <int Id> void SetOffset(toolbelt::BufferOffset offset) {
    int32_t *discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto &t = std::get<Id>(value_);
    return t.SetOffset(GetRuntime(),
                 GetMessageBinaryStart() + relative_binary_offset_ + 4, offset);
  }

private:
  ::toolbelt::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime> &GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  std::vector<int> field_numbers_; // field number for each tuple type
  mutable std::tuple<T...> value_;
};
} // namespace phaser
