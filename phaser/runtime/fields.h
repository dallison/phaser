// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Single value fields.

#include <stdint.h>
#include <stdlib.h>

#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/wireformat.h"
#include "toolbelt/payload_buffer.h"

namespace phaser {

template <typename T>
constexpr size_t AlignedOffset(size_t offset) {
  return (offset + sizeof(T) - 1) & ~(sizeof(T) - 1);
}

class Field {
 public:
  Field() = default;
  Field(int id, int number) : id_(id), number_(number) {}
  Field(const Field&) = default;
  Field& operator=(const Field&) = default;
  virtual ~Field() = default;

  // The presence bit is in a set of words immediately after
  // the metadata at the start of the message.
  void SetPresence(::toolbelt::PayloadBuffer* buffer, uint32_t binary_offset) {
    buffer->SetPresenceBit(static_cast<uint32_t>(id_), binary_offset);
  }

  void ClearPresence(::toolbelt::PayloadBuffer* buffer,
                     uint32_t binary_offset) {
    buffer->ClearPresenceBit(static_cast<uint32_t>(id_), binary_offset);
  }

  bool IsPresent(uint32_t field_id, ::toolbelt::PayloadBuffer* buffer,
                 uint32_t binary_offset) const {
    if (field_id == static_cast<uint32_t>(-1)) {
      return false;
    }
    return buffer->IsPresent(field_id, binary_offset);
  }

  int Id() const { return id_; }
  int Number() const { return number_; }

  int32_t FindFieldOffset(uint32_t source_offset) const {
    ResolveField(source_offset);
    return cached_offset_;
  }

  int32_t FindFieldId(uint32_t source_offset) const {
    ResolveField(source_offset);
    return cached_field_id_;
  }

  // For printing.
  void Indent(int indent) const { indent_ += indent; }
  void PrintIndent(std::ostream& os) const {
    for (int i = 0; i < indent_; i++) {
      os << " ";
    }
  }

  int GetIndent() const { return indent_; }

 protected:
  void RequireMutable(uint32_t source_offset) const {
    if (!Message::GetRuntime(this, source_offset)->IsMutable()) {
      throw std::logic_error("cannot mutate a readonly Phaser message");
    }
  }

  void ResolveField(uint32_t source_offset) const {
    if (field_cache_resolved_) {
      return;
    }
    const Message* message = Message::GetMessage(this, source_offset);
    if (message->runtime == nullptr) {
      return;
    }
    const FieldLocation location =
        message->FindField(static_cast<uint32_t>(number_));
    cached_offset_ = location.offset;
    cached_field_id_ = location.id;
    field_cache_resolved_ = true;
  }

  void ResetFieldCache() {
    cached_offset_ = -1;
    cached_field_id_ = -1;
    field_cache_resolved_ = false;
  }

 protected:
  int id_ = 0;
  int number_ = 0;
  mutable int32_t cached_offset_ = -1;
  mutable int32_t cached_field_id_ = -1;
  mutable bool field_cache_resolved_ = false;
  mutable int indent_ = 0;
};

#define DEFINE_PRIMITIVE_FIELD(cname, type)                                   \
  template <bool FixedSize = false, bool Signed = false>                      \
  class cname##Field : public Field {                                         \
   public:                                                                    \
    cname##Field() = default;                                                 \
    explicit cname##Field(uint32_t boff, uint32_t offset, int id, int number) \
        : Field(id, number),                                                  \
          source_offset_(boff),                                               \
          relative_binary_offset_(offset) {}                                  \
    cname##Field(const cname##Field&) = default;                              \
    cname##Field(cname##Field&&) = default;                                   \
    cname##Field& operator=(const cname##Field& other) {                      \
      if (this == &other) {                                                   \
        return *this;                                                         \
      }                                                                       \
      if (other.IsPresent()) {                                                \
        Set(other.Get());                                                     \
      } else {                                                                \
        Clear();                                                              \
      }                                                                       \
      ResetFieldCache();                                                      \
      return *this;                                                           \
    }                                                                         \
    cname##Field& operator=(cname##Field&& other) noexcept {                  \
      return operator=(static_cast<const cname##Field&>(other));              \
    }                                                                         \
    operator type() const { return Get(); }                                   \
    cname##Field& operator=(type v) {                                         \
      Set(v);                                                                 \
      return *this;                                                           \
    }                                                                         \
    type Get() const {                                                        \
      int32_t offset = FindFieldOffset(source_offset_);                       \
      if (offset < 0) {                                                       \
        return type();                                                        \
      }                                                                       \
      return GetBuffer()->template Get<type>(                                 \
          GetMessageBinaryStart() +                                           \
          static_cast<::toolbelt::BufferOffset>(offset));                     \
    }                                                                         \
    type GetForPrinting() const { return Get(); }                             \
    bool IsPresent() const {                                                  \
      return Field::IsPresent(                                                \
          static_cast<uint32_t>(FindFieldId(source_offset_)), GetBuffer(),    \
          GetPresenceMaskStart());                                            \
    }                                                                         \
                                                                              \
    void Set(type v) {                                                        \
      RequireMutable(source_offset_);                                         \
      GetBuffer()->Set(GetMessageBinaryStart() + relative_binary_offset_, v); \
      SetPresence(GetBuffer(), GetPresenceMaskStart());                       \
    }                                                                         \
    void Clear() {                                                            \
      RequireMutable(source_offset_);                                         \
      ClearPresence(GetBuffer(), GetPresenceMaskStart());                     \
    }                                                                         \
    bool operator==(const cname##Field& other) const {                        \
      return Get() == other.Get();                                            \
    }                                                                         \
    bool operator!=(const cname##Field& other) const {                        \
      return !(*this == other);                                               \
    }                                                                         \
    size_t SerializedSize() const {                                           \
      if constexpr (FixedSize) {                                              \
        return ProtoBuffer::TagSize(Number(),                                 \
                                    ProtoBuffer::FixedWireType<type>()) +     \
               sizeof(type);                                                  \
      } else {                                                                \
        return ProtoBuffer::TagSize(Number(), WireType::kVarint) +            \
               ProtoBuffer::VarintSize<type, Signed>(Get());                  \
      }                                                                       \
    }                                                                         \
    absl::Status Serialize(ProtoBuffer& buffer) const {                       \
      if constexpr (FixedSize) {                                              \
        return buffer.SerializeFixed<type>(Number(), Get());                  \
      } else {                                                                \
        return buffer.SerializeVarint<type, Signed>(Number(), Get());         \
      }                                                                       \
    }                                                                         \
                                                                              \
    absl::Status Deserialize(ProtoBuffer& buffer) {                           \
      absl::StatusOr<type> v;                                                 \
      if constexpr (FixedSize) {                                              \
        v = buffer.DeserializeFixed<type>();                                  \
      } else {                                                                \
        v = buffer.DeserializeVarint<type, Signed>();                         \
      }                                                                       \
      if (!v.ok()) {                                                          \
        return v.status();                                                    \
      }                                                                       \
      Set(*v);                                                                \
      return absl::OkStatus();                                                \
    }                                                                         \
                                                                              \
   private:                                                                   \
    ::toolbelt::PayloadBuffer* GetBuffer() const {                            \
      return Message::GetBuffer(this, source_offset_);                        \
    }                                                                         \
    ::toolbelt::BufferOffset GetMessageBinaryStart() const {                  \
      return Message::GetMessageBinaryStart(this, source_offset_);            \
    }                                                                         \
    ::toolbelt::BufferOffset GetPresenceMaskStart() const {                   \
      return Message::GetMessageBinaryStart(this, source_offset_) + 4;        \
    }                                                                         \
    uint32_t source_offset_;                                                  \
    ::toolbelt::BufferOffset relative_binary_offset_;                         \
  };

DEFINE_PRIMITIVE_FIELD(Int32, int32_t)
DEFINE_PRIMITIVE_FIELD(Uint32, uint32_t)
DEFINE_PRIMITIVE_FIELD(Int64, int64_t)
DEFINE_PRIMITIVE_FIELD(Uint64, uint64_t)
DEFINE_PRIMITIVE_FIELD(Double, double)
DEFINE_PRIMITIVE_FIELD(Float, float)
DEFINE_PRIMITIVE_FIELD(Bool, bool)

#undef DEFINE_PRIMITIVE_FIELD

struct InternalIntStringizer {
  std::string operator()(int i) const { return std::to_string(i); }
};

struct InternalIntParser {
  int operator()(const std::string& s) const { return std::stoi(s); }
};

template <typename Enum = int, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser>
class EnumField : public Field {
 public:
  using T = typename std::underlying_type<Enum>::type;
  EnumField() = default;
  explicit EnumField(uint32_t boff, uint32_t offset, int id, int number)
      : Field(id, number),
        source_offset_(boff),
        relative_binary_offset_(offset) {}
  EnumField(const EnumField&) = default;
  EnumField(EnumField&&) = default;
  EnumField& operator=(const EnumField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPresent()) {
      Set(other.Get());
    } else {
      Clear();
    }
    ResetFieldCache();
    return *this;
  }
  EnumField& operator=(EnumField&& other) noexcept {
    return operator=(static_cast<const EnumField&>(other));
  }
  operator Enum() const { return Get(); }
  operator T() const { return GetUnderlying(); }
  EnumField& operator=(Enum e) {
    Set(e);
    return *this;
  }
  EnumField& operator=(T e) {
    Set(e);
    return *this;
  }

  Enum Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return static_cast<Enum>(0);
    }
    return static_cast<Enum>(
        GetBuffer()->template Get<typename std::underlying_type<Enum>::type>(
            GetMessageBinaryStart() +
            static_cast<::toolbelt::BufferOffset>(offset)));
  }

  std::string GetForPrinting() const { return ToString(); }

  bool IsPresent() const {
    return Field::IsPresent(static_cast<uint32_t>(FindFieldId(source_offset_)),
                            GetBuffer(), GetPresenceMaskStart());
  }

  std::string ToString() const { return Stringizer()(Get()); }

  Enum ParseFromString(const std::string& s) { Set(Parser(s)); }

  T GetUnderlying() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return GetBuffer()->template Get<typename std::underlying_type<Enum>::type>(
        GetMessageBinaryStart() +
        static_cast<::toolbelt::BufferOffset>(offset));
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

  bool operator==(const EnumField& other) const {
    return static_cast<Enum>(*this) == static_cast<Enum>(other);
  }
  bool operator!=(const EnumField& other) const { return !(*this == other); }

  size_t SerializedSize() const {
    return ProtoBuffer::TagSize(Number(), WireType::kVarint) +
           ProtoBuffer::VarintSize<int32_t, false>(
               static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    return buffer.SerializeVarint<int32_t, false>(
        Number(), static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
    if (!v.ok()) {
      return v.status();
    }
    Set(*v);
    return absl::OkStatus();
  }

 private:
  ::toolbelt::PayloadBuffer* GetBuffer() const {
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
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  StringField(const StringField&) = default;
  StringField(StringField&&) = default;
  StringField& operator=(const StringField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPresent()) {
      Set(other.Get());
    } else {
      Clear();
    }
    ResetFieldCache();
    return *this;
  }
  StringField& operator=(StringField&& other) noexcept {
    return operator=(static_cast<const StringField&>(other));
  }
  operator std::string_view() const { return Get(); }
  StringField& operator=(const std::string& s) {
    Set(s);
    return *this;
  }
  StringField& operator=(std::string_view s) {
    Set(s);
    return *this;
  }
  StringField& operator=(const char* s) {
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(s, std::strlen(s)),
        GetMessageBinaryStart() + relative_binary_offset_);
    return *this;
  }

  std::string_view Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return std::string_view();
    }
    return GetBuffer()->GetStringView(
        GetMessageBinaryStart() +
        static_cast<::toolbelt::BufferOffset>(offset));
  }

  bool IsPresent() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return false;
    }
    const ::toolbelt::BufferOffset* addr =
        GetRuntime()->ToAddress<const ::toolbelt::BufferOffset>(
            GetMessageBinaryStart() +
            static_cast<::toolbelt::BufferOffset>(offset));
    return *addr != 0;
  }

  template <typename Str>
  void Set(Str s) {
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), s, GetMessageBinaryStart() + relative_binary_offset_);
  }

  void Set(const char* data, size_t size) {
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(data, size),
        GetMessageBinaryStart() + relative_binary_offset_);
  }

  void SetNoCopy(const void* data) {
    toolbelt::StringHeader* header =
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

  bool operator==(const StringField& other) const {
    return Get() == other.Get();
  }
  bool operator!=(const StringField& other) const { return !(*this == other); }

  size_t size() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return GetBuffer()->StringSize(
        GetMessageBinaryStart() +
        static_cast<::toolbelt::BufferOffset>(offset));
  }

  const char* data() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return nullptr;
    }
    return GetBuffer()->StringData(
        GetMessageBinaryStart() +
        static_cast<::toolbelt::BufferOffset>(offset));
  }

  size_t SerializedSize() const {
    size_t s = size();
    return ProtoBuffer::LengthDelimitedSize(Number(), s);
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t s = size();
    return buffer.ProtoBuffer::SerializeLengthDelimited(Number(), data(), s);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<std::string_view> s = buffer.DeserializeString();
    if (!s.ok()) {
      return s.status();
    }
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), *s, GetMessageBinaryStart() + relative_binary_offset_);
    return absl::OkStatus();
  }

 private:
  template <size_t N>
  friend class StringArrayField;

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
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
  explicit NonEmbeddedStringField(const Message* msg,
                                  uint32_t absolute_binary_offset)
      : msg_(msg), absolute_binary_offset_(absolute_binary_offset) {}
  NonEmbeddedStringField(const NonEmbeddedStringField&) = default;
  NonEmbeddedStringField(NonEmbeddedStringField&&) = default;
  NonEmbeddedStringField& operator=(const NonEmbeddedStringField& other) & {
    return AssignValue(other);
  }
  NonEmbeddedStringField& operator=(const NonEmbeddedStringField& other) && {
    return AssignValue(other);
  }
  NonEmbeddedStringField& operator=(NonEmbeddedStringField&& other) & noexcept {
    msg_ = other.msg_;
    absolute_binary_offset_ = other.absolute_binary_offset_;
    return *this;
  }
  NonEmbeddedStringField& operator=(NonEmbeddedStringField&& other) && {
    return AssignValue(other);
  }

 private:
  NonEmbeddedStringField& AssignValue(const NonEmbeddedStringField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPlaceholder()) {
      return *this;
    }
    std::string_view value = other.Get();
    if (GetBuffer() != other.GetBuffer()) {
      Set(value);
      return *this;
    }
    if (value.empty()) {
      Set(value);
      return *this;
    }
    // Preserve an offset rather than an address: allocating the destination
    // may relocate a dynamic payload, but offsets remain valid.
    const ::toolbelt::BufferOffset source =
        GetBuffer()->ToOffset(const_cast<char*>(value.data()));
    absl::Span<char> destination = ::toolbelt::PayloadBuffer::AllocateString(
        GetBufferAddr(), value.size(), absolute_binary_offset_, false);
    memmove(destination.data(), GetBuffer()->ToAddress<char>(source),
            value.size());
    return *this;
  }

 public:
  operator std::string_view() const { return Get(); }
  NonEmbeddedStringField& operator=(const std::string& s) {
    Set(s);
    return *this;
  }
  NonEmbeddedStringField& operator=(std::string_view s) {
    Set(s);
    return *this;
  }
  NonEmbeddedStringField& operator=(const char* s) {
    ::toolbelt::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(s, std::strlen(s)),
        absolute_binary_offset_);
    return *this;
  }

  std::string_view Get() const {
    if (IsPlaceholder()) {
      return {};
    }
    return GetBuffer()->GetStringView(absolute_binary_offset_);
  }

  template <typename Str>
  void Set(Str s) {
    if (IsPlaceholder()) {
      return;
    }
    ::toolbelt::PayloadBuffer::SetString(GetBufferAddr(), s,
                                         absolute_binary_offset_);
  }

  void Clear() {
    if (IsPlaceholder()) {
      return;
    }
    ::toolbelt::PayloadBuffer::ClearString(GetBufferAddr(),
                                           absolute_binary_offset_);
  }

  bool operator==(const NonEmbeddedStringField& other) const {
    return Get() == other.Get();
  }
  bool operator!=(const NonEmbeddedStringField& other) const {
    return !(*this == other);
  }

  size_t size() const {
    if (IsPlaceholder()) {
      return 0;
    }
    return GetBuffer()->StringSize(absolute_binary_offset_);
  }

  const char* data() const {
    if (IsPlaceholder()) {
      return "";
    }
    return GetBuffer()->StringData(absolute_binary_offset_);
  }
  bool empty() const { return size() == 0; }

  bool IsPlaceholder() const { return msg_ == nullptr; }

  // Number of bytes the raw string occupies on the wire (not including any
  // field tag or length prefix). Repeated-string serialization is handled by
  // StringVectorField, which writes the tag/length and bytes directly, so this
  // type intentionally has no standalone Serialize() of its own.
  size_t SerializedSize() const { return size(); }

 private:
  ::toolbelt::PayloadBuffer* GetBuffer() const { return msg_->runtime->pb; }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    if (!msg_->runtime->IsMutable()) {
      throw std::logic_error("cannot mutate a readonly Phaser string");
    }
    return &msg_->runtime->pb;
  }

  const Message* msg_ = nullptr;
  ::toolbelt::BufferOffset
      absolute_binary_offset_ = 0;  // Offset into
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

template <typename MessageType>
class IndirectMessageField : public Field {
 public:
  IndirectMessageField() = default;
  explicit IndirectMessageField(uint32_t source_offset,
                                uint32_t relative_binary_offset, int id,
                                int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset),
        msg_(InternalDefault{}) {}
  IndirectMessageField(const IndirectMessageField&) = default;
  IndirectMessageField(IndirectMessageField&&) = default;
  IndirectMessageField& operator=(const IndirectMessageField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPresent()) {
      if (absl::Status s = Mutable()->CloneFrom(other.Get()); !s.ok()) {
        return *this;
      }
    } else {
      Clear();
    }
    ResetFieldCache();
    return *this;
  }
  IndirectMessageField& operator=(IndirectMessageField&& other) noexcept {
    return operator=(static_cast<const IndirectMessageField&>(other));
  }
  operator const MessageType&() const { return Get(); }
  const MessageType& operator*() const { return Get(); }
  const MessageType* operator->() const { return &Get(); }
  operator MessageType&() { return *Mutable(); }
  MessageType& operator*() { return *Mutable(); }
  MessageType* operator->() { return Mutable(); }

  const MessageType& Msg() const { return Get(); }
  MessageType& MutableMsg() { return *Mutable(); }

  const MessageType& Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return DefaultMessage();
    }
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(static_cast<uint32_t>(offset));
    if (*addr == 0) {
      return DefaultMessage();
    }
    // Load up the message if it's already been allocated.
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = *addr;
    return msg_;
  }

  bool IsPresent() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return false;
    }
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(static_cast<uint32_t>(offset));
    return *addr != 0;
  }

  MessageType* Mutable() {
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    if (*addr != 0) {
      // Already allocated.
      msg_.runtime = GetRuntime();
      msg_.absolute_binary_offset = *addr;
      return &msg_;
    }
    // Allocate a new message.
    void* msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize());
    ::toolbelt::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = msg_offset;

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(relative_binary_offset_);
    *addr = msg_offset;  // Put message field offset into message.

    // Install the metadata into the binary message.
    msg_.template InstallMetadata<MessageType>();
    return &msg_;
  }

  void SetOffset(toolbelt::BufferOffset offset) {
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    if (*addr != 0) {
      // Already set, clear the exising message
      Clear();
    }
    *addr = offset;
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = offset;
  }

  void Clear() {
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    if (*addr == 0) {
      return;
    }
    const ::toolbelt::BufferOffset old_offset = *addr;
    // Clear the message.
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = old_offset;
    msg_.Clear();
    // Delete the memory in the payload buffer.
    GetBuffer()->Free(GetRuntime()->ToAddress(old_offset));
    // Zero out the offset to the message.
    addr = GetIndirectAddress(relative_binary_offset_);
    *addr = 0;
  }

  bool operator==(const IndirectMessageField<MessageType>& other) const {
    return Get() == other.Get();
  }
  bool operator!=(const IndirectMessageField<MessageType>& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(static_cast<uint32_t>(offset));
    if (*addr != 0) {
      // Load up the message if it's already been allocated.
      msg_.runtime = GetRuntime();
      msg_.absolute_binary_offset = *addr;
    }
    return ProtoBuffer::LengthDelimitedSize(Number(), msg_.SerializedSize());
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return absl::OkStatus();
    }
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(static_cast<uint32_t>(offset));
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

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    // Allocate a new message.
    void* msg_addr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize());
    ::toolbelt::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = GetRuntime();
    msg_.absolute_binary_offset = msg_offset;

    // Buffer might have moved, get address of indirect again.
    ::toolbelt::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    *addr = msg_offset;  // Put message field offset into message.

    // Install the metadata into the binary message.
    msg_.template InstallMetadata<MessageType>();

    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

  void SyncToPayload() const {
    if (IsPresent()) {
      Get().SyncToPayload();
    }
  }

  void Indent(int indent) const {
    Field::Indent(indent);
    msg_.Indent(indent);
  }

 protected:
  static const MessageType& DefaultMessage() {
    static const MessageType message(InternalDefault{});
    return message;
  }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::BufferOffset* GetIndirectAddress(uint32_t abs_offset) const {
    return GetBuffer()->template ToAddress<::toolbelt::BufferOffset>(
        GetMessageBinaryStart() + abs_offset);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
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

template <typename MessageType>
class MessageObject {
 public:
  MessageObject() : msg_(InternalDefault{}) {}
  explicit MessageObject(std::shared_ptr<MessageRuntime> runtime,
                         uint32_t absolute_binary_offset)
      : msg_(runtime, absolute_binary_offset) {}
  MessageObject(const MessageObject& other)
      : msg_(other.msg_.runtime, other.msg_.absolute_binary_offset),
        indent_(other.indent_) {}
  MessageObject(MessageObject&& other) noexcept
      : msg_(other.msg_.runtime, other.msg_.absolute_binary_offset),
        indent_(other.indent_) {}
  MessageObject& operator=(const MessageObject& other) {
    if (this == &other) {
      return *this;
    }
    this->~MessageObject();
    new (this) MessageObject(other);
    return *this;
  }
  MessageObject& operator=(MessageObject&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    this->~MessageObject();
    new (this) MessageObject(std::move(other));
    return *this;
  }

  const MessageType& Get() const { return msg_; }

  const MessageType& operator*() const { return msg_; }
  MessageType& operator*() { return msg_; }
  const MessageType* operator->() const { return &msg_; }
  MessageType* operator->() { return &msg_; }

  MessageType* Mutable() { return &msg_; }

  bool operator==(const MessageObject<MessageType>& other) const {
    return msg_ == other.msg_;
  }
  bool operator!=(const MessageObject<MessageType>& other) const {
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

  const MessageType& Msg() const { return msg_; }
  MessageType& MutableMsg() { return msg_; }

  template <typename T>
  absl::Status CloneFrom(const T& other) {
    return msg_.CloneFrom(other.msg_);
  }

  size_t SerializedSize() const { return msg_.SerializedSize(); }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    return msg_.Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
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

  void PrintIndent(std::ostream& os) const {
    for (int i = 0; i < indent_; i++) {
      os << " ";
    }
  }

 private:
  mutable MessageType msg_;
  mutable int indent_ = 0;
};

}  // namespace phaser
