#pragma once

// Union fields.
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/payload_buffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace phaser {

class UnionMemberField {
protected:
  phaser::PayloadBuffer *
  GetBuffer(std::shared_ptr<MessageRuntime> runtime) const {
    return runtime->pb;
  }

  phaser::PayloadBuffer **
  GetBufferAddr(std::shared_ptr<MessageRuntime> runtime) const {
    return &runtime->pb;
  }

  std::shared_ptr<MessageRuntime> GetRuntime() { return runtime_; }

  std::shared_ptr<MessageRuntime> runtime_;
};

#define DEFINE_PRIMITIVE_UNION_FIELD(cname, type)                              \
  class Union##cname##Field : public UnionMemberField {                        \
  public:                                                                      \
    Union##cname##Field() = default;                                           \
    type Get(std::shared_ptr<MessageRuntime> runtime,                          \
             uint32_t abs_offset) const {                                      \
      if (runtime == nullptr) {                                                \
        return type();                                                         \
      }                                                                        \
      return GetBuffer(runtime)->Get<type>(abs_offset);                        \
    }                                                                          \
    void Set(type v, std::shared_ptr<MessageRuntime> runtime,                  \
             uint32_t abs_offset) {                                            \
      GetBuffer(runtime)->Set(abs_offset, v);                                  \
    }                                                                          \
                                                                               \
    bool Equal(const Union##cname##Field &other,                               \
               std::shared_ptr<MessageRuntime> runtime, uint32_t abs_offset) { \
      return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);       \
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

// The union contains an offset to the string data (length and bytes).
class UnionStringField : public UnionMemberField {
public:
  UnionStringField() = default;

  std::string_view Get(std::shared_ptr<MessageRuntime> runtime,
                       uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return "";
    }
    return GetBuffer(runtime)->GetStringView(abs_offset);
  }

  bool IsPresent(std::shared_ptr<MessageRuntime> runtime,
                 uint32_t abs_offset) const {
    const phaser::BufferOffset *addr =
        GetBuffer(runtime)->ToAddress<const phaser::BufferOffset>(abs_offset);
    return *addr != 0;
  }

  void Set(const std::string &s, std::shared_ptr<MessageRuntime> runtime,
           uint32_t abs_offset) {
    phaser::PayloadBuffer::SetString(GetBufferAddr(runtime), s, abs_offset);
  }

  bool Equal(const UnionStringField &other,
             std::shared_ptr<MessageRuntime> runtime, uint32_t abs_offset) {
    return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);
  }

  size_t size(std::shared_ptr<MessageRuntime> runtime,
              uint32_t abs_offset) const {
    return GetBuffer(runtime)->StringSize(abs_offset);
  }

  const char *data(std::shared_ptr<MessageRuntime> runtime,
                   uint32_t abs_offset) const {
    return GetBuffer(runtime)->StringData(abs_offset);
  }
};

template <typename Enum> class UnionEnumField : public UnionMemberField {
public:
  using T = typename std::underlying_type<Enum>::type;
  UnionEnumField() = default;

  Enum Get(std::shared_ptr<MessageRuntime> runtime, uint32_t abs_offset) const {
    if (runtime == nullptr) {
      return static_cast<Enum>(0);
    }
    return static_cast<Enum>(
        GetBuffer(runtime)
            ->template Get<typename std::underlying_type<Enum>::type>(
                abs_offset));
  }

  T GetUnderlying(std::shared_ptr<MessageRuntime> runtime,
                  uint32_t abs_offset) const {
    return GetBuffer(runtime)
        ->template Get<typename std::underlying_type<Enum>::type>(abs_offset);
  }

  void Set(Enum e, std::shared_ptr<MessageRuntime> runtime,
           uint32_t abs_offset) {
    GetBuffer(runtime)->Set(
        abs_offset, static_cast<typename std::underlying_type<Enum>::type>(e));
  }

  void Set(T e, std::shared_ptr<MessageRuntime> runtime, uint32_t abs_offset) {
    GetBuffer(runtime)->Set(abs_offset, e);
  }

  bool Equal(const UnionEnumField<Enum> &other,
             std::shared_ptr<MessageRuntime> runtime, uint32_t abs_offset) {
    return Get(runtime, abs_offset) == other.Get(runtime, abs_offset);
  }
};

template <typename MessageType>
class UnionMessageField : public UnionMemberField {
public:
  UnionMessageField() = default;

  const MessageType &Get(std::shared_ptr<MessageRuntime> runtime,
                         uint32_t abs_offset) {
    phaser::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (*addr == 0) {
      return msg_;
    }
    // Populate msg_ with the information from the message.
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = *addr;
    return msg_;
  }

  bool IsPresent(std::shared_ptr<MessageRuntime> runtime,
                 uint32_t abs_offset) const {
    phaser::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    return *addr != 0;
  }

  MessageType *Mutable(std::shared_ptr<MessageRuntime> runtime,
                       uint32_t abs_offset) {
    phaser::BufferOffset *addr = GetIndirectAddress(runtime, abs_offset);
    if (*addr != 0) {
      // Already allocated.
      return &msg_;
    }
    // Allocate a new message.
    void *msg_addr = phaser::PayloadBuffer::Allocate(
        GetBufferAddr(runtime), MessageType::BinarySize(), 8);
    phaser::BufferOffset msg_offset = GetBuffer(runtime)->ToOffset(msg_addr);
    // Assign to the message.
    msg_.runtime = runtime;
    msg_.absolute_binary_offset = msg_offset;
    msg_.template InstallMetadata<MessageType>();
    
    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(runtime, abs_offset);
    *addr = msg_offset; // Put message field offset into message.
    return &msg_;
  }

  absl::Status SerializeToBuffer(ProtoBuffer &buffer) const {
    return msg_.SerializeToBuffer(buffer);
  }

  absl::Status DeserializeFromBuffer(ProtoBuffer &buffer) {
    return msg_.DeserializeFromBuffer(buffer);
  }

private:
  phaser::BufferOffset *
  GetIndirectAddress(std::shared_ptr<MessageRuntime> runtime,
                     uint32_t abs_offset) const {
    return GetBuffer(runtime)->template ToAddress<phaser::BufferOffset>(
        abs_offset);
  }

  MessageType msg_;
};

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

  template <int Id, typename F> const F &GetReference() {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return std::get<Id>(value_).Get(nullptr, 0);
    }
    return std::get<Id>(value_).Get(GetRuntime(), GetMessageBinaryStart() +
                                                      relative_offset + 4);
  }

  template <int Id, typename F> F GetValue() {
    int32_t relative_offset = Message::GetMessage(this, source_offset_)
                                  ->FindFieldOffset(field_numbers_[Id]);
    if (relative_offset < 0) {
      return std::get<Id>(value_).Get(nullptr, 0);
    }
    return std::get<Id>(value_).Get(GetRuntime(), GetMessageBinaryStart() +
                                                      relative_offset + 4);
  }

  template <int Id, typename U> void Set(const U &v) {
    // Write the field number into the discriminator.
    int32_t *discrim = GetBuffer()->template ToAddress<int32_t>(
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
    int32_t *discrim = GetBuffer()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    *discrim = field_numbers_[Id];

    // Get the variant and set its location.  In binary it is
    // 4 bytes after the discriminator.
    auto &t = std::get<Id>(value_);
    return t.Mutable(GetRuntime(),
                     GetMessageBinaryStart() + relative_binary_offset_ + 4);
  }

  int32_t Discriminator() const {
    int32_t *discrim = GetBuffer()->template ToAddress<int32_t>(
        GetMessageBinaryStart() + relative_binary_offset_);
    return *discrim;
  }

private:
  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  std::shared_ptr<MessageRuntime> GetRuntime() {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
  std::vector<int> field_numbers_; // field number for each tuple type
  std::tuple<T...> value_;
};
} // namespace phaser