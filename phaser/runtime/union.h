// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Union fields.
#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/payload_buffer.h"

namespace phaser {

class UnionMemberField {
 protected:
  ::phaser::PayloadBuffer* GetBuffer(const RuntimeHandle& runtime) const {
    return runtime->pb;
  }

  ::phaser::PayloadBuffer** GetBufferAddr(const RuntimeHandle& runtime) const {
    return &runtime->pb;
  }
};

#define DEFINE_PRIMITIVE_UNION_FIELD(cname, type)                              \
  template <bool FixedSize = false, bool Signed = false>                       \
  class Union##cname##Field : public UnionMemberField {                        \
   public:                                                                     \
    Union##cname##Field() = default;                                           \
    type Get(const RuntimeHandle& runtime, uint32_t abs_offset) const {        \
      if (runtime == nullptr) {                                                \
        return type();                                                         \
      }                                                                        \
      /* Use MessageRuntime::ToAddress so CreateReadonly's trusted size        \
       * clamps hostile offsets; PayloadBuffer::Get trusts inflated            \
       * full_size and can SEGV on receive. */                                 \
      const type* addr = runtime->template ToAddress<const type>(abs_offset);  \
      if (addr == nullptr) {                                                   \
        return type();                                                         \
      }                                                                        \
      return *addr;                                                            \
    }                                                                          \
    void Print(std::ostream& os, int /*indent*/, const RuntimeHandle& runtime, \
               uint32_t abs_offset) const {                                    \
      os << Get(runtime, abs_offset);                                          \
    }                                                                          \
    void Set(type v, const RuntimeHandle& runtime, uint32_t abs_offset) {      \
      GetBuffer(runtime)->Set(abs_offset, v);                                  \
    }                                                                          \
    void SetOffset(const RuntimeHandle& /*runtime*/, uint32_t /*abs_offset*/,  \
                   ::phaser::BufferOffset /*msg_offset*/) {}                   \
                                                                               \
    bool Equal(const Union##cname##Field& other, const RuntimeHandle& runtime, \
               uint32_t abs_offset) {                                          \
      return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);       \
    }                                                                          \
    void Clear(const RuntimeHandle& /*runtime*/, uint32_t /*abs_offset*/) {}   \
    size_t SerializedSize(int number, const RuntimeHandle& runtime,            \
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
    absl::Status Serialize(int number, ProtoMessageWriter buffer,              \
                           const RuntimeHandle& runtime,                       \
                           uint32_t abs_offset) const {                        \
      if constexpr (FixedSize) {                                               \
        return buffer.SerializeFixed<type>(number, Get(runtime, abs_offset));  \
      } else {                                                                 \
        return buffer.SerializeVarint<type, Signed>(number,                    \
                                                    Get(runtime, abs_offset)); \
      }                                                                        \
    }                                                                          \
    absl::Status Deserialize(ProtoBuffer& buffer,                              \
                             const RuntimeHandle& runtime,                     \
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

  Enum Get(const RuntimeHandle& runtime, uint32_t abs_offset) const {
    return static_cast<Enum>(GetUnderlying(runtime, abs_offset));
  }

  void Print(std::ostream& os, int /*indent*/, const RuntimeHandle& runtime,
             uint32_t abs_offset) const {
    os << Stringizer()(Get(runtime, abs_offset));
  }

  T GetUnderlying(const RuntimeHandle& runtime, uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return T();
    }
    const T* addr = runtime->template ToAddress<const T>(abs_offset);
    if (addr == nullptr) {
      return T();
    }
    return *addr;
  }

  void SetOffset(const RuntimeHandle& /*runtime*/, uint32_t /*abs_offset*/,
                 ::phaser::BufferOffset /*msg_offset*/) {}

  void Set(Enum e, const RuntimeHandle& runtime, uint32_t abs_offset) {
    GetBuffer(runtime)->Set(
        abs_offset, static_cast<typename std::underlying_type<Enum>::type>(e));
  }

  void Set(T e, const RuntimeHandle& runtime, uint32_t abs_offset) {
    GetBuffer(runtime)->Set(abs_offset, e);
  }

  bool Equal(const UnionEnumField<Enum, Stringizer, Parser>& other,
             const RuntimeHandle& runtime, uint32_t abs_offset) {
    return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);
  }
  void Clear(const RuntimeHandle& /*runtime*/, uint32_t /*abs_offset*/) {}

  size_t SerializedSize(int number, const RuntimeHandle& runtime,
                        uint32_t abs_offset) const {
    return ProtoBuffer::TagSize(number, WireType::kVarint) +
           ProtoBuffer::VarintSize<T, false>(
               GetUnderlying(runtime, abs_offset));
  }

  absl::Status Serialize(int number, ProtoMessageWriter buffer,
                         const RuntimeHandle& runtime,
                         uint32_t abs_offset) const {
    return buffer.SerializeVarint<T, false>(number,
                                            GetUnderlying(runtime, abs_offset));
  }

  absl::Status Deserialize(ProtoBuffer& buffer, const RuntimeHandle& runtime,
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

  std::string_view Get(const RuntimeHandle& runtime,
                       uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return "";
    }
    return runtime->GetStringView(abs_offset);
  }

  void Print(std::ostream& os, int /*indent*/, const RuntimeHandle& runtime,
             uint32_t abs_offset) const {
    os << "\"" << std::string(Get(runtime, abs_offset)) << "\"";
  }

  bool IsPresent(const RuntimeHandle& runtime, uint32_t abs_offset) const {
    const ::phaser::BufferOffset* addr =
        runtime->ToAddress<const ::phaser::BufferOffset>(abs_offset);
    return addr != nullptr && *addr != 0;
  }

  void SetOffset(const RuntimeHandle& /*runtime*/, uint32_t /*abs_offset*/,
                 ::phaser::BufferOffset /*msg_offset*/) {}

  template <typename Str>
  void Set(Str s, const RuntimeHandle& runtime, uint32_t abs_offset) {
    ::phaser::PayloadBuffer::SetString(GetBufferAddr(runtime), s, abs_offset);
  }

  absl::Span<char> Allocate(size_t size, const RuntimeHandle& runtime,
                            uint32_t abs_offset) {
    return ::phaser::PayloadBuffer::AllocateString(GetBufferAddr(runtime),
                                                     size, abs_offset);
  }

  bool Equal(const UnionStringField& other, const RuntimeHandle& runtime,
             uint32_t abs_offset) {
    return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);
  }

  size_t size(const RuntimeHandle& runtime, uint32_t abs_offset) const {
    return runtime->StringSize(abs_offset);
  }

  const char* data(const RuntimeHandle& runtime, uint32_t abs_offset) const {
    return runtime->StringData(abs_offset);
  }
  void Clear(const RuntimeHandle& runtime, uint32_t abs_offset) {
    ::phaser::PayloadBuffer::ClearString(GetBufferAddr(runtime), abs_offset);
  }

  size_t SerializedSize(int number, const RuntimeHandle& runtime,
                        uint32_t abs_offset) const {
    size_t sz = size(runtime, abs_offset);
    return ProtoBuffer::TagSize(number, WireType::kLengthDelimited) +
           ProtoBuffer::VarintSize<int32_t, false>(static_cast<int32_t>(sz)) +
           sz;
  }

  absl::Status Serialize(int number, ProtoMessageWriter buffer,
                         const RuntimeHandle& runtime,
                         uint32_t abs_offset) const {
    return buffer.SerializeLengthDelimited(number, data(runtime, abs_offset),
                                           size(runtime, abs_offset));
  }

  absl::Status Deserialize(ProtoBuffer& buffer, const RuntimeHandle& runtime,
                           uint32_t abs_offset) {
    absl::StatusOr<std::string_view> v = buffer.DeserializeString();
    if (!v.ok()) {
      return v.status();
    }
    if (::phaser::PayloadBuffer::SetString(GetBufferAddr(runtime), *v,
                                           abs_offset) == nullptr) {
      return internal::AllocationFailedError();
    }
    return absl::OkStatus();
  }

  constexpr WireType GetWireType() { return WireType::kLengthDelimited; }
};

template <typename MessageType>
class UnionMessageField : public UnionMemberField {
 public:
  UnionMessageField() : msg_(InternalDefault{}) {}

  const MessageType& Get(const RuntimeHandle& runtime,
                         uint32_t abs_offset) const {
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr || *addr == 0) {
      return DefaultMessage();
    }
    // Populate msg_ with the information from the message.
    Bind(runtime, *addr);
    return msg_;
  }

  void Print(std::ostream& os, int indent, const RuntimeHandle& runtime,
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

  bool IsPresent(const RuntimeHandle& runtime, uint32_t abs_offset) const {
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    return addr != nullptr && *addr != 0;
  }

  MessageType* Mutable(const RuntimeHandle& runtime, uint32_t abs_offset) {
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr || *addr != 0) {
      // Already allocated.
      if (addr != nullptr) {
        Bind(runtime, *addr);
      }
      return &msg_;
    }
    // Allocate a new message.
    void* msg_addr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize());
    if (msg_addr == nullptr) {
      internal::BindToAllocationFailureSink(msg_);
      return &msg_;
    }
    ::phaser::BufferOffset msg_offset =
        GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.template BindBorrowed<MessageType>(runtime, msg_offset);
    if (!msg_.template InstallMetadata<MessageType>()) {
      internal::BindToAllocationFailureSink(msg_);
      return &msg_;
    }

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(runtime, abs_offset);
    if (addr != nullptr) {
      *addr = msg_offset;  // Put message field offset into message.
    }
    return &msg_;
  }

  void SetOffset(const RuntimeHandle& runtime, uint32_t abs_offset,
                 ::phaser::BufferOffset msg_offset) {
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    *addr = msg_offset;
    msg_.template BindBorrowed<MessageType>(runtime, msg_offset);
  }

  absl::Status SerializeToBuffer(ProtoBuffer& buffer) const {
    return msg_.SerializeToBuffer(buffer);
  }

  absl::Status DeserializeFromBuffer(ProtoBuffer& buffer) {
    return msg_.DeserializeFromBuffer(buffer);
  }

  void Set(const MessageType& msg, const RuntimeHandle& runtime,
           uint32_t abs_offset) {
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    if (*addr != 0) {
      const ::phaser::BufferOffset old_offset = *addr;
      Bind(runtime, old_offset);
      msg_.Clear();
      GetBuffer(runtime)->Free(runtime->ToAddress(old_offset));
      addr = GetIndirectAddress(runtime, abs_offset);
      if (addr == nullptr) {
        return;
      }
      *addr = 0;
    }
    // Allocate a new message.
    void* msg_addr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize());
    if (msg_addr == nullptr) {
      return;
    }
    ::phaser::BufferOffset msg_offset =
        GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.template BindBorrowed<MessageType>(runtime, msg_offset);
    msg_.template InstallMetadata<MessageType>();

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    *addr = msg_offset;  // Put message field offset into message.

    // TODO: what to do here if this fails?
    (void)msg_.CloneFrom(msg);
  }

  void Clear(const RuntimeHandle& runtime, uint32_t abs_offset) {
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return;
    }
    if (*addr != 0) {
      const ::phaser::BufferOffset old_offset = *addr;
      Bind(runtime, old_offset);
      msg_.Clear();
      GetBuffer(runtime)->Free(runtime->ToAddress(old_offset));
      // Clearing the nested message may move the payload buffer.
      addr = GetIndirectAddress(runtime, abs_offset);
      if (addr != nullptr) {
        *addr = 0;
      }
    }
  }

  constexpr WireType GetWireType() { return WireType::kLengthDelimited; }

  size_t SerializedSize(int number, const RuntimeHandle& runtime,
                        uint32_t abs_offset) const {
    return ProtoBuffer::LengthDelimitedSize(
        number, Get(runtime, abs_offset).SerializedSize());
  }

  absl::Status Serialize(int number, ProtoMessageWriter buffer,
                         const RuntimeHandle& runtime,
                         uint32_t abs_offset) const {
    if (absl::Status status = buffer.SerializeLengthDelimitedHeader(
            number, Get(runtime, abs_offset).SerializedSize());
        !status.ok()) {
      return status;
    }
    return Get(runtime, abs_offset).PhaserSerialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer& buffer, const RuntimeHandle& runtime,
                           uint32_t abs_offset) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    void* msg_addr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize());
    if (msg_addr == nullptr) {
      return internal::AllocationFailedError();
    }
    ::phaser::BufferOffset msg_offset =
        GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.template BindBorrowed<MessageType>(runtime, msg_offset);
    if (!msg_.template InstallMetadata<MessageType>()) {
      return internal::AllocationFailedError();
    }

    // Buffer might have moved, get address of indirect again.
    ::phaser::BufferOffset* addr = GetIndirectAddress(runtime, abs_offset);
    if (addr == nullptr) {
      return absl::OkStatus();
    }
    *addr = msg_offset;  // Put message field offset into message.
    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

 private:
  static const MessageType& DefaultMessage() {
    static const MessageType message(InternalDefault{});
    return message;
  }

  void Bind(const RuntimeHandle& runtime, ::phaser::BufferOffset offset) const {
    msg_.template BindBorrowed<MessageType>(runtime, offset);
  }

  ::phaser::BufferOffset* GetIndirectAddress(const RuntimeHandle& runtime,
                                             uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return nullptr;
    }
    return runtime->template ToAddress<::phaser::BufferOffset>(abs_offset);
  }

  mutable MessageType msg_;
};  // namespace phaser

// All member of the tuple must be union fields.  These are stored in a
// std::tuple which does not store them inline so they need to contain the
// buffer shared pointer and the offset of the message binary data.
//
// In binary, this is stored as a 4 byte integer containing the discriminator
// (the field number of the tuple) followed by the binary data for the
// field itself.  The size of the field data is the max of all the fields
// in the union.
template <typename... T>
class UnionField : public FieldBase<StoredFieldLoc> {
 public:
  UnionField() = default;
  UnionField(uint32_t source_offset, uint32_t relative_binary_offset, int id,
             int number, absl::Span<const uint32_t> field_numbers)
      : FieldBase(source_offset, relative_binary_offset, id, number),
        field_numbers_(field_numbers) {}

  size_t index() const {
    const int32_t discriminator = Discriminator();
    for (size_t i = 0; i < field_numbers_.size(); ++i) {
      if (discriminator == static_cast<int32_t>(field_numbers_[i])) {
        return i;
      }
    }
    return std::variant_npos;
  }

  int32_t case_number() const { return Discriminator(); }
  bool valueless_by_exception() const {
    return index() == std::variant_npos;
  }

  void reset() { ClearActive(); }

  template <typename Alternative>
  bool holds_alternative() const {
    static_assert(Alternative::kIndex < sizeof...(T),
                  "oneof alternative index is out of range");
    return IsPresent<Alternative::kIndex>();
  }

  template <typename Alternative>
  decltype(auto) get() const {
    if (!holds_alternative<Alternative>()) {
      PHASER_THROW(std::bad_variant_access());
    }
    if constexpr (Alternative::kIsMessage) {
      return GetReference<Alternative::kIndex,
                          typename Alternative::value_type>();
    } else {
      return GetValue<Alternative::kIndex,
                      typename Alternative::value_type>();
    }
  }

  template <typename Alternative>
  decltype(auto) get() {
    if (!holds_alternative<Alternative>()) {
      PHASER_THROW(std::bad_variant_access());
    }
    if constexpr (Alternative::kIsMessage) {
      return *Mutable<Alternative::kIndex, typename Alternative::value_type>();
    } else {
      return GetValue<Alternative::kIndex,
                      typename Alternative::value_type>();
    }
  }

  template <typename Alternative, typename... Args>
  decltype(auto) emplace(Args&&... args) {
    static_assert(Alternative::kIndex < sizeof...(T),
                  "oneof alternative index is out of range");
    if constexpr (Alternative::kIsMessage) {
      static_assert(sizeof...(Args) == 0,
                    "message oneof alternatives are emplaced empty and then "
                    "mutated through the returned reference");
      return *Mutable<Alternative::kIndex, typename Alternative::value_type>();
    } else {
      using Value = typename Alternative::value_type;
      Value value(std::forward<Args>(args)...);
      Set<Alternative::kIndex>(value);
      return GetValue<Alternative::kIndex, Value>();
    }
  }

  template <int Id, typename F>
  const F& GetReference() const {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return std::get<Id>(value_).Get(nullptr, 0);
    }
    return std::get<Id>(value_).Get(
        GetRuntime(),
        GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(relative_offset) + 4);
  }

  template <int Id, typename F>
  F GetValue() const {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return std::get<Id>(value_).Get(nullptr, 0);
    }
    return std::get<Id>(value_).Get(
        GetRuntime(),
        GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(relative_offset) + 4);
  }

  template <int Id>
  void Print(std::ostream& os) const {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return;
    }
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(relative_offset));
    if (discrim == nullptr ||
        *discrim != static_cast<int32_t>(field_numbers_[Id])) {
      return;
    }
    std::get<Id>(value_).Print(
        os, GetIndent(), GetRuntime(),
        GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(relative_offset) + 4);
  }

  template <int Id, typename U>
  void Set(const U& v) {
    PrepareArm<Id>();
    // Write the field number into the discriminator.
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    *discrim = static_cast<int32_t>(field_numbers_[Id]);
    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto& t = std::get<Id>(value_);
    t.Set(v, GetRuntime(), GetMessageBinaryStart() + Offset() + 4);
  }

  template <int Id, typename U>
  U* Mutable() {
    PrepareArm<Id>();
    // Write the field number into the discriminator.
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    *discrim = static_cast<int32_t>(field_numbers_[Id]);

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto& t = std::get<Id>(value_);
    return t.Mutable(GetRuntime(), GetMessageBinaryStart() + Offset() + 4);
  }

  // Only valid for strings and bytes.
  template <int Id>
  absl::Span<char> Allocate(size_t size) {
    PrepareArm<Id>();
    // Write the field number into the discriminator.
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    *discrim = static_cast<int32_t>(field_numbers_[Id]);

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto& t = std::get<Id>(value_);
    return t.Allocate(size, GetRuntime(),
                      GetMessageBinaryStart() + Offset() + 4);
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
    const Message* msg = Parent();
    int32_t relative_offset = -1;
    for (auto& field_number : field_numbers_) {
      relative_offset = msg->FindFieldOffset(field_number);
      if (relative_offset >= 0) {
        break;
      }
    }
    if (relative_offset < 0) {
      return 0;  // No field present in message (all fields have been removed).
    }
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(relative_offset));
    if (discrim == nullptr) {
      return 0;
    }
    return *discrim;
  }

  template <int Id>
  void Clear() {
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    if (discrim == nullptr) {
      return;
    }
    int32_t field_number = static_cast<int32_t>(field_numbers_[Id]);
    if (*discrim != field_number) {
      return;
    }
    auto& t = std::get<Id>(value_);
    t.Clear(GetRuntime(), GetMessageBinaryStart() + Offset() + 4);
    *discrim = 0;
    // Reset the buffer-offset word in the shared value slot. Scalar/enum arms
    // store their value inline here and their Clear() is a no-op, so without
    // this a later switch to a variable-length arm (string/message) would
    // misread the leftover scalar bytes as an allocated buffer offset.
    ::phaser::BufferOffset* slot =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(
            GetMessageBinaryStart() + Offset() + 4);
    *slot = 0;
  }

  template <int Id>
  size_t SerializedSize(int discriminator) const {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {  // Field not present.
      return 0;
    }
    return std::get<Id>(value_).SerializedSize(
        discriminator, GetRuntime(),
        GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(relative_offset) + 4);
  }

  template <int Id>
  absl::Status Serialize(int discriminator, ProtoMessageWriter buffer) const {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {  // Field not present.
      return absl::OkStatus();
    }
    return std::get<Id>(value_).Serialize(
        discriminator, buffer, GetRuntime(),
        GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(relative_offset) + 4);
  }

  template <int Id>
  absl::Status Deserialize(int /*discriminator*/, ProtoBuffer& buffer) {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {  // Field not present.
      return absl::OkStatus();
    }
    PrepareArm<Id>();
    if (absl::Status status = std::get<Id>(value_).Deserialize(
            buffer, GetRuntime(),
            GetMessageBinaryStart() +
                static_cast<::phaser::BufferOffset>(relative_offset) + 4);
        !status.ok()) {
      return status;
    }
    // Set the discriminator.
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    *discrim = static_cast<int32_t>(field_numbers_[Id]);
    return absl::OkStatus();
  }

  template <int Id, typename M>
  absl::Status CloneFrom(const M& other) {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {  // Field not present.
      return absl::OkStatus();
    }
    PrepareArm<Id>();
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    *discrim = static_cast<int32_t>(field_numbers_[Id]);
    // TODO: this isn't right.  If the field is a message, it can fail to clone.
    std::get<Id>(value_).Set(
        other, GetRuntime(),
        GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(relative_offset) + 4);
    return absl::OkStatus();
  }

  template <int Id>
  bool IsPresent() const {
    int32_t relative_offset = Parent()->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return false;
    }
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(relative_offset));
    return discrim != nullptr &&
           *discrim == static_cast<int32_t>(field_numbers_[Id]);
  }

  template <int Id>
  void SetOffset(::phaser::BufferOffset offset) {
    PrepareArm<Id>();
    int32_t* discrim = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    *discrim = static_cast<int32_t>(field_numbers_[Id]);

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto& t = std::get<Id>(value_);
    return t.SetOffset(GetRuntime(), GetMessageBinaryStart() + Offset() + 4,
                       offset);
  }

 private:
  template <size_t Id = 0>
  void ClearByIndex(size_t active_index) {
    if constexpr (Id < sizeof...(T)) {
      if (active_index == Id) {
        Clear<static_cast<int>(Id)>();
        return;
      }
      ClearByIndex<Id + 1>(active_index);
    }
  }

  void ClearActive() {
    const size_t active_index = index();
    if (active_index != std::variant_npos) {
      ClearByIndex(active_index);
      return;
    }
    int32_t* discriminator = GetRuntime()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + Offset());
    if (discriminator != nullptr) {
      *discriminator = 0;
    }
  }

  template <int Id>
  void PrepareArm() {
    static_assert(Id >= 0 && Id < static_cast<int>(sizeof...(T)),
                  "oneof arm index is out of range");
    if (!GetRuntime()->IsMutable()) {
      PHASER_THROW(std::logic_error("cannot mutate a readonly Phaser message"));
    }
    const int32_t desired = static_cast<int32_t>(field_numbers_[Id]);
    const int32_t current = Discriminator();
    if (current == desired) {
      return;
    }
    ClearActive();
    ::phaser::BufferOffset* slot =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(
            GetMessageBinaryStart() + Offset() + 4);
    if (slot != nullptr) {
      *slot = 0;
    }
  }

  absl::Span<const uint32_t>
      field_numbers_;  // Static field number for each tuple type.
  mutable std::tuple<T...> value_;
};
}  // namespace phaser
