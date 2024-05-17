// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Single value fields.

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/wireformat.h"
#include "toolbelt/payload_buffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <vector>

namespace phaser {

template <typename T> constexpr size_t AlignedOffset(size_t offset) {
  return (offset + sizeof(T) - 1) & ~(sizeof(T) - 1);
}

class Field {
public:
  Field() = default;
  Field(int id, int number) : id_(id), number_(number) {}
  virtual ~Field() = default;

  // The presence bit is in a set of words immediately after
  // the metadata at the start of the message.
  void SetPresence(::toolbelt::PayloadBuffer *buffer, uint32_t binary_offset) {
    buffer->SetPresenceBit(id_, binary_offset);
  }

  void ClearPresence(::toolbelt::PayloadBuffer *buffer,
                     uint32_t binary_offset) {
    buffer->ClearPresenceBit(id_, binary_offset);
  }

  bool IsPresent(uint32_t field_id, ::toolbelt::PayloadBuffer *buffer,
                 uint32_t binary_offset) const {
    if (field_id == -1) {
      return false;
    }
    return buffer->IsPresent(field_id, binary_offset);
  }

  int Id() const { return id_; }
  int Number() const { return number_; }

  int32_t FindFieldOffset(uint32_t source_offset) const {
    if (cached_offset_ == 0xffffffff) {
      cached_offset_ =
          Message::GetMessage(this, source_offset)->FindFieldOffset(number_);
    }
    return cached_offset_;
  }

  int32_t FindFieldId(uint32_t source_offset) const {
    if (cached_field_id_ == -1) {
      cached_field_id_ =
          Message::GetMessage(this, source_offset)->FindFieldId(number_);
    }
    return cached_field_id_;
  }

  // For printing.
  void Indent(int indent) const { indent_ += indent; }
  void PrintIndent(std::ostream &os) const {
    for (int i = 0; i < indent_; i++) {
      os << " ";
    }
  }

  int GetIndent() const { return indent_; }

protected:
  int id_ = 0;
  int number_ = 0;
  mutable ::toolbelt::BufferOffset cached_offset_ = 0xffffffff;
  mutable int32_t cached_field_id_ = -1;
  mutable int indent_ = 0;
};

#define DEFINE_PRIMITIVE_FIELD(cname, type)                                    \
  template <bool FixedSize, bool Signed> class cname##Field : public Field {   \
  public:                                                                      \
    cname##Field() = default;                                                  \
    explicit cname##Field(uint32_t boff, uint32_t offset, int id, int number)  \
        : Field(id, number), source_offset_(boff),                             \
          relative_binary_offset_(offset) {}                                   \
    type Get() const {                                                         \
      int32_t offset = FindFieldOffset(source_offset_);                        \
      if (offset < 0) {                                                        \
        return type();                                                         \
      }                                                                        \
      return GetBuffer()->template Get<type>(GetMessageBinaryStart() +         \
                                             offset);                          \
    }                                                                          \
    type GetForPrinting() const { return Get(); }                              \
    bool IsPresent() const {                                                   \
      return Field::IsPresent(FindFieldId(source_offset_), GetBuffer(),        \
                              GetPresenceMaskStart());                         \
    }                                                                          \
                                                                               \
    void Set(type v) {                                                         \
      GetBuffer()->Set(GetMessageBinaryStart() + relative_binary_offset_, v);  \
      SetPresence(GetBuffer(), GetPresenceMaskStart());                        \
    }                                                                          \
    void Clear() { ClearPresence(GetBuffer(), GetPresenceMaskStart()); }       \
    bool operator==(const cname##Field &other) const {                         \
      return Get() == other.Get();                                             \
    }                                                                          \
    bool operator!=(const cname##Field &other) const {                         \
      return !(*this == other);                                                \
    }                                                                          \
    size_t SerializedSize() const {                                            \
      if constexpr (FixedSize) {                                               \
        return ProtoBuffer::TagSize(Number(),                                  \
                                    ProtoBuffer::FixedWireType<type>()) +      \
               sizeof(type);                                                   \
      } else {                                                                 \
        return ProtoBuffer::TagSize(Number(), WireType::kVarint) +             \
               ProtoBuffer::VarintSize<type, Signed>(Get());                   \
      }                                                                        \
    }                                                                          \
    absl::Status Serialize(ProtoBuffer &buffer) const {                        \
      if constexpr (FixedSize) {                                               \
        return buffer.SerializeFixed<type>(Number(), Get());                   \
      } else {                                                                 \
        return buffer.SerializeVarint<type, Signed>(Number(), Get());          \
      }                                                                        \
    }                                                                          \
                                                                               \
    absl::Status Deserialize(ProtoBuffer &buffer) {                            \
      absl::StatusOr<type> v;                                                  \
      if constexpr (FixedSize) {                                               \
        v = buffer.DeserializeFixed<type>();                                   \
      } else {                                                                 \
        v = buffer.DeserializeVarint<type, Signed>();                          \
      }                                                                        \
      if (!v.ok()) {                                                           \
        return v.status();                                                     \
      }                                                                        \
      Set(*v);                                                                 \
      return absl::OkStatus();                                                 \
    }                                                                          \
                                                                               \
  private:                                                                     \
    ::toolbelt::PayloadBuffer *GetBuffer() const {                             \
      return Message::GetBuffer(this, source_offset_);                         \
    }                                                                          \
    ::toolbelt::BufferOffset GetMessageBinaryStart() const {                   \
      return Message::GetMessageBinaryStart(this, source_offset_);             \
    }                                                                          \
    ::toolbelt::BufferOffset GetPresenceMaskStart() const {                    \
      return Message::GetMessageBinaryStart(this, source_offset_) + 4;         \
    }                                                                          \
    uint32_t source_offset_;                                                   \
    ::toolbelt::BufferOffset relative_binary_offset_;                          \
  };

DEFINE_PRIMITIVE_FIELD(Int32, int32_t)
DEFINE_PRIMITIVE_FIELD(Uint32, uint32_t)
DEFINE_PRIMITIVE_FIELD(Int64, int64_t)
DEFINE_PRIMITIVE_FIELD(Uint64, uint64_t)
DEFINE_PRIMITIVE_FIELD(Double, double)
DEFINE_PRIMITIVE_FIELD(Float, float)
DEFINE_PRIMITIVE_FIELD(Bool, bool)

#undef DEFINE_PRIMITIVE_FIELD

template <typename Enum, typename Stringizer, typename Parser>
class EnumField : public Field {
public:
  using T = typename std::underlying_type<Enum>::type;
  EnumField() = default;
  explicit EnumField(uint32_t boff, uint32_t offset, int id, int number)
      : Field(id, number), source_offset_(boff),
        relative_binary_offset_(offset) {}

  Enum Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return static_cast<Enum>(0);
    }
    return static_cast<Enum>(
        GetBuffer()->template Get<typename std::underlying_type<Enum>::type>(
            GetMessageBinaryStart() + offset));
  }

  std::string GetForPrinting() const { return ToString(); }

  bool IsPresent() const {
    return Field::IsPresent(FindFieldId(source_offset_), GetBuffer(),
                            GetPresenceMaskStart());
  }

  std::string ToString() const { return Stringizer()(Get()); }

  Enum ParseFromString(const std::string &s) { Set(Parser(s)); }

  T GetUnderlying() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return GetBuffer()->template Get<typename std::underlying_type<Enum>::type>(
        GetMessageBinaryStart() + offset);
  }

  void Set(Enum e) {
    GetBuffer()->Set(GetMessageBinaryStart() + relative_binary_offset_,
                     static_cast<typename std::underlying_type<Enum>::type>(e));
    SetPresence(GetBuffer(), GetPresenceMaskStart());
  }

  void Set(T e) {
    GetBuffer()->Set(GetMessageBinaryStart() + relative_binary_offset_, e);
    SetPresence(GetBuffer(), GetPresenceMaskStart());
  }

  void Clear() { ClearPresence(GetBuffer(), GetPresenceMaskStart()); }

  bool operator==(const EnumField &other) const {
    return static_cast<Enum>(*this) == static_cast<Enum>(other);
  }
  bool operator!=(const EnumField &other) const { return !(*this == other); }

  size_t SerializedSize() const {
    return ProtoBuffer::TagSize(Number(), WireType::kVarint) +
           ProtoBuffer::VarintSize<int32_t, false>(
               static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    return buffer.SerializeVarint<int32_t, false>(
        Number(), static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Deserialize(ProtoBuffer &buffer) {
    absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
    if (!v.ok()) {
      return v.status();
    }
    Set(*v);
    return absl::OkStatus();
  }

private:
  ::toolbelt::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetPresenceMaskStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_) + 4;
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
};

// String field with an offset inline in the message.
class StringField : public Field {
public:
  StringField() = default;
  explicit StringField(uint32_t source_offset, uint32_t relative_binary_offset,
                       int id, int number)
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}

  std::string_view Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return std::string_view();
    }
    return GetBuffer()->GetStringView(GetMessageBinaryStart() + offset);
  }

  bool IsPresent() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return false;
    }
    const ::toolbelt::BufferOffset *addr =
        GetRuntime()->ToAddress<const ::toolbelt::BufferOffset>(
            GetMessageBinaryStart() + offset);
    return *addr != 0;
  }

  template <typename Str> void Set(Str s) {
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), s, GetMessageBinaryStart() + relative_binary_offset_);
  }

  void Set(const char *data, size_t size) {
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(data, size),
        GetMessageBinaryStart() + relative_binary_offset_);
  }

  void SetNoCopy(const void *data) {
    toolbelt::StringHeader *header =
        GetRuntime()->ToAddress<toolbelt::StringHeader>(
            GetMessageBinaryStart() + relative_binary_offset_);
    *header = GetRuntime()->ToOffset(data);
  }

  void Clear() {
    ::toolbelt::PayloadBuffer::ClearString(
        GetBufferAddr(), GetMessageBinaryStart() + relative_binary_offset_);
  }

  // Allocate space for the given size for the string and return the
  // starting address.
  absl::Span<char> Allocate(size_t size, bool clear = false) {
    return ::toolbelt::PayloadBuffer::AllocateString(
        GetBufferAddr(), size,
        GetMessageBinaryStart() + relative_binary_offset_, clear);
  }

  bool operator==(const StringField &other) const {
    return Get() == other.Get();
  }
  bool operator!=(const StringField &other) const { return !(*this == other); }

  size_t size() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return GetBuffer()->StringSize(GetMessageBinaryStart() + offset);
  }

  const char *data() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return nullptr;
    }
    return GetBuffer()->StringData(GetMessageBinaryStart() + offset);
  }

  size_t SerializedSize() const {
    size_t s = size();
    return ProtoBuffer::LengthDelimitedSize(Number(), s);
  }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    size_t s = size();
    return buffer.ProtoBuffer::SerializeLengthDelimited(Number(), data(), s);
  }

  absl::Status Deserialize(ProtoBuffer &buffer) {
    absl::StatusOr<std::string_view> s = buffer.DeserializeString();
    if (!s.ok()) {
      return s.status();
    }
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), *s, GetMessageBinaryStart() + relative_binary_offset_);
    return absl::OkStatus();
  }

private:
  template <int N> friend class StringArrayField;

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
};

// This is a string field that is not embedded inside a message. They
// store the std::shared_ptr to the phaser::Runtime pointer instead of
// an offset to the start of the message.
class NonEmbeddedStringField {
public:
  NonEmbeddedStringField() = default;
  explicit NonEmbeddedStringField(const Message *msg,
                                  uint32_t absolute_binary_offset)
      : msg_(msg), absolute_binary_offset_(absolute_binary_offset) {}

  std::string_view Get() const {
    return GetBuffer()->GetStringView(absolute_binary_offset_);
  }

  template <typename Str> void Set(Str s) {
    ::toolbelt::PayloadBuffer::SetString(GetBufferAddr(), s,
                                         absolute_binary_offset_);
  }

  void Clear() {
    ::toolbelt::PayloadBuffer::ClearString(GetBufferAddr(),
                                           absolute_binary_offset_);
  }

  bool operator==(const NonEmbeddedStringField &other) const {
    return Get() == other.Get();
  }
  bool operator!=(const NonEmbeddedStringField &other) const {
    return !(*this == other);
  }

  size_t size() const {
    return GetBuffer()->StringSize(absolute_binary_offset_);
  }

  const char *data() const {
    return GetBuffer()->StringData(absolute_binary_offset_);
  }
  bool empty() const { return size() == 0; }

  bool IsPlaceholder() const { return msg_ == nullptr; }

  size_t SerializedSize() const { return size(); }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    // TODO:
    return absl::OkStatus();
  }

private:
  ::toolbelt::PayloadBuffer *GetBuffer() const { return msg_->runtime->pb; }

  ::toolbelt::PayloadBuffer **GetBufferAddr() const {
    return &msg_->runtime->pb;
  }

  const Message *msg_;
  ::toolbelt::BufferOffset
      absolute_binary_offset_; // Offset into
                               // ::toolbelt::PayloadBuffer of
                               // toolbelt::StringHeader
};

// This is a buffer offset containing the absolute offset of a message in the
// payload buffer.
//
//    In the message:
//    +------------+
//    |  Indirect  |------->+------------+
//    +------------+        |  Message   |
//                          |            |
//                          |            |
//                          |            |
//                          |            |
//                          +------------+

template <typename MessageType> class IndirectMessageField : public Field {
public:
  IndirectMessageField() = default;
  explicit IndirectMessageField(uint32_t source_offset,
                                uint32_t relative_binary_offset, int id,
                                int number)
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset),
        msg_(InternalDefault{}) {}

  const MessageType &Msg() const { return msg_; }
  MessageType &MutableMsg() { return msg_; }

  const MessageType &Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return msg_;
    }
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(offset);
    if (*addr != 0) {
      // Load up the message if it's already been allocated.
      msg_.runtime = GetRuntime();
      msg_.absolute_binary_offset = *addr;
    }
    return msg_;
  }

  bool IsPresent() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return false;
    }
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(offset);
    return *addr != 0;
  }

  MessageType *Mutable() {
    ::toolbelt::BufferOffset *addr =
        GetIndirectAddress(relative_binary_offset_);
    if (*addr != 0) {
      // Already allocated.
      return &msg_;
    }
    // Allocate a new message.
    void *msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize(), 8);
    ::toolbelt::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = msg_offset;

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(relative_binary_offset_);
    *addr = msg_offset; // Put message field offset into message.

    // Install the metadata into the binary message.
    msg_.template InstallMetadata<MessageType>();
    return &msg_;
  }

  void Clear() {
    ::toolbelt::BufferOffset *addr =
        GetIndirectAddress(relative_binary_offset_);
    if (*addr == 0) {
      return;
    }
    // Clear the message.
    msg_.Clear();
    // Delete the memory in the payload buffer.
    GetBuffer()->Free(GetRuntime()->ToAddress(*addr));
    // Zero out the offset to the message.
    *addr = 0;
  }

  bool operator==(const IndirectMessageField<MessageType> &other) const {
    return msg_ != other.msg_;
  }
  bool operator!=(const IndirectMessageField<MessageType> &other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(offset);
    if (*addr != 0) {
      // Load up the message if it's already been allocated.
      msg_.runtime = GetRuntime();
      msg_.absolute_binary_offset = *addr;
    }
    return ProtoBuffer::LengthDelimitedSize(Number(), msg_.SerializedSize());
  }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return absl::OkStatus();
    }
    ::toolbelt::BufferOffset *addr = GetIndirectAddress(offset);
    if (*addr != 0) {
      // Load up the message if it's already been allocated.
      msg_.runtime = GetRuntime();
      msg_.absolute_binary_offset = *addr;
    }

    size_t size = msg_.SerializedSize();
    if (absl::Status status =
            buffer.SerializeLengthDelimitedHeader(Number(), size);
        !status.ok()) {
      return status;
    }

    return msg_.Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer &buffer) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    // Allocate a new message.
    void *msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize(), 8);
    ::toolbelt::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = msg_offset;

    // Buffer might have moved, get address of indirect again.
    ::toolbelt::BufferOffset *addr =
        GetIndirectAddress(relative_binary_offset_);
    *addr = msg_offset; // Put message field offset into message.

    // Install the metadata into the binary message.
    msg_.template InstallMetadata<MessageType>();

    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

  void Indent(int indent) const {
    Field::Indent(indent);
    msg_.Indent(indent);
  }

protected:
  ::toolbelt::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::BufferOffset *GetIndirectAddress(uint32_t offset) const {
    return GetBuffer()->template ToAddress<::toolbelt::BufferOffset>(
        GetMessageBinaryStart() + offset);
  }

  ::toolbelt::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  mutable MessageType msg_;
};

template <typename MessageType> class MessageObject {
public:
  MessageObject() : msg_(InternalDefault{}){};
  explicit MessageObject(std::shared_ptr<MessageRuntime> runtime,
                         uint32_t absolute_binary_offset)
      : msg_(runtime, absolute_binary_offset) {}

  const MessageType &Get() const { return msg_; }

  MessageType *Mutable() { return &msg_; }

  bool operator==(const MessageObject<MessageType> &other) const {
    return msg_ != other.msg_;
  }
  bool operator!=(const MessageObject<MessageType> &other) const {
    return !(*this == other);
  }

  bool empty() const { return msg_.runtime == nullptr; }

  void InstallMetadata() { msg_.template InstallMetadata<MessageType>(); }

  bool IsPlaceholder() const { return msg_.runtime == nullptr; }

  void Clear() {
    if (msg_.runtime == nullptr) {
      return;
    }
    msg_.Clear();
  }

  const MessageType &Msg() const { return msg_; }
  MessageType &MutableMsg() { return msg_; }

  template <typename T> absl::Status CloneFrom(const T &other) {
    return msg_.CloneFrom(other.msg_);
  }

  size_t SerializedSize() const { return msg_.SerializedSize(); }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    return msg_.Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer &buffer) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

  void Indent(int indent) const {
    indent_ += indent;
    msg_.Indent(indent);
  }

  void PrintIndent(std::ostream &os) const {
    for (int i = 0; i < indent_; i++) {
      os << " ";
    }
  }

private:
  mutable MessageType msg_;
  mutable int indent_ = 0;
};

} // namespace phaser
