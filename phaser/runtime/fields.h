#pragma once

// Single value fields.

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
#include <vector>

namespace phaser {

class ProtoBuffer;

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
  void SetPresence(phaser::PayloadBuffer *buffer, uint32_t binary_offset) {
    buffer->SetPresenceBit(id_, binary_offset);
  }

  void ClearPresence(phaser::PayloadBuffer *buffer, uint32_t binary_offset) {
    buffer->ClearPresenceBit(id_, binary_offset);
  }

  bool IsPresent(uint32_t field_id, phaser::PayloadBuffer *buffer,
                 uint32_t binary_offset) const {
    if (field_id == -1) {
      return false;
    }
    return buffer->IsPresent(field_id, binary_offset);
  }

  int Id() const { return id_; }
  int Number() const { return number_; }

  int32_t FindFieldOffset(uint32_t source_offset) const {
    return Message::GetMessage(this, source_offset)->FindFieldOffset(number_);
  }

  int32_t FindFieldId(uint32_t source_offset) const {
    return Message::GetMessage(this, source_offset)->FindFieldId(number_);
  }

protected:
  int id_ = 0;
  int number_ = 0;
};

#define DEFINE_PRIMITIVE_FIELD(cname, type)                                    \
  class cname##Field : public Field {                                          \
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
      return GetBuffer()->Get<type>(GetMessageBinaryStart() + offset);         \
    }                                                                          \
    bool IsPresent() const {                                                   \
      return Field::IsPresent(FindFieldId(source_offset_), GetBuffer(),        \
                              GetPresenceMaskStart());                         \
    }                                                                          \
                                                                               \
    void Set(type v) {                                                         \
      GetBuffer()->Set(GetMessageBinaryStart() + relative_binary_offset_, v);  \
      SetPresence(GetBuffer(), GetPresenceMaskStart());                        \
    }                                                                          \
    void SetLocation(uint32_t soff, uint32_t boff) {                           \
      source_offset_ = soff;                                                   \
      relative_binary_offset_ = boff;                                          \
    }                                                                          \
    phaser::BufferOffset BinaryEndOffset() const {                             \
      return relative_binary_offset_ + sizeof(type);                           \
    }                                                                          \
    phaser::BufferOffset BinaryOffset() const {                                \
      return relative_binary_offset_;                                          \
    }                                                                          \
    bool operator==(const cname##Field &other) const {                         \
      return Get() == other.Get();                                             \
    }                                                                          \
    bool operator!=(const cname##Field &other) const {                         \
      return !(*this == other);                                                \
    }                                                                          \
                                                                               \
  private:                                                                     \
    phaser::PayloadBuffer *GetBuffer() const {                                 \
      return Message::GetBuffer(this, source_offset_);                         \
    }                                                                          \
    phaser::BufferOffset GetMessageBinaryStart() const {                       \
      return Message::GetMessageBinaryStart(this, source_offset_);             \
    }                                                                          \
    phaser::BufferOffset GetPresenceMaskStart() const {                        \
      return Message::GetMessageBinaryStart(this, source_offset_) + 4;         \
    }                                                                          \
    uint32_t source_offset_;                                                   \
    phaser::BufferOffset relative_binary_offset_;                              \
  };

DEFINE_PRIMITIVE_FIELD(Int32, int32_t)
DEFINE_PRIMITIVE_FIELD(Uint32, uint32_t)
DEFINE_PRIMITIVE_FIELD(Int64, int64_t)
DEFINE_PRIMITIVE_FIELD(Uint64, uint64_t)
DEFINE_PRIMITIVE_FIELD(Double, double)
DEFINE_PRIMITIVE_FIELD(Float, float)
DEFINE_PRIMITIVE_FIELD(Bool, bool)

#undef DEFINE_PRIMITIVE_FIELD

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
    const phaser::BufferOffset *addr =
        GetBuffer()->ToAddress<const phaser::BufferOffset>(
            GetMessageBinaryStart() + relative_binary_offset_);
    return *addr != 0;
  }

  void Set(const std::string &s) {
    phaser::PayloadBuffer::SetString(
        GetBufferAddr(), s, GetMessageBinaryStart() + relative_binary_offset_);
  }

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(phaser::BufferOffset);
  }

  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(const StringField &other) const {
    return Get() == other.Get();
  }
  bool operator!=(const StringField &other) const { return !(*this == other); }

  size_t size() const {
    return GetBuffer()->StringSize(GetMessageBinaryStart() +
                                   relative_binary_offset_);
  }

  const char *data() const {
    return GetBuffer()->StringData(GetMessageBinaryStart() +
                                   relative_binary_offset_);
  }

private:
  template <int N> friend class StringArrayField;

  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
};

// This is a string field that is not embedded inside a message.  These will be
// allocated from the heap, as is the case when used in a std::vector.  They
// store the std::shared_ptr to the phaser::Runtime pointer instead of
// an offset to the start of the message.
class NonEmbeddedStringField {
public:
  NonEmbeddedStringField() = default;
  explicit NonEmbeddedStringField(Message *msg, uint32_t absolute_binary_offset)
      : msg_(msg),
        absolute_binary_offset_(absolute_binary_offset) {}

  phaser::BufferOffset BinaryEndOffset() const {
    return absolute_binary_offset_ + sizeof(phaser::BufferOffset);
  }
  phaser::BufferOffset BinaryOffset() const { return absolute_binary_offset_; }

  std::string_view Get() const {
    return GetBuffer()->GetStringView(absolute_binary_offset_);
  }

  void Set(const std::string &s) {
    phaser::PayloadBuffer::SetString(GetBufferAddr(), s,
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

private:
  phaser::PayloadBuffer *GetBuffer() const { return msg_->runtime->pb; }

  phaser::PayloadBuffer **GetBufferAddr() const { return &msg_->runtime->pb; }

  Message *msg_;
  phaser::BufferOffset absolute_binary_offset_; // Offset into
                                                // phaser::PayloadBuffer of
                                                // phaser::StringHeader
};

template <typename Enum> class EnumField : public Field {
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

  bool IsPresent() const {
    return IsPresent(FindFieldId(source_offset_), GetBuffer(),
                     GetMessageBinaryStart());
  }

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
    SetPresence(GetBuffer(), GetMessageBinaryStart());
  }

  void Set(T e) {
    GetBuffer()->Set(GetMessageBinaryStart() + relative_binary_offset_, e);
    SetPresence(GetBuffer(), GetMessageBinaryStart());
  }

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ +
           sizeof(typename std::underlying_type<Enum>::type);
  }
  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(const EnumField &other) const {
    return static_cast<Enum>(*this) == static_cast<Enum>(other);
  }
  bool operator!=(const EnumField &other) const { return !(*this == other); }

private:
  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }
  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
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
        relative_binary_offset_(relative_binary_offset) {}

  const MessageType &Get() {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return msg_;
    }
    phaser::BufferOffset *addr = GetIndirectAddress(offset);
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
    phaser::BufferOffset *addr = GetIndirectAddress(offset);
    return *addr != 0;
  }

  MessageType *Mutable() {
    phaser::BufferOffset *addr = GetIndirectAddress(relative_binary_offset_);
    if (*addr != 0) {
      // Already allocated.
      return &msg_;
    }
    // Allocate a new message.
    void *msg_addr = phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize(), 8);
    phaser::BufferOffset msg_offset = GetBuffer()->ToOffset(msg_addr);
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

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(phaser::BufferOffset);
  }

  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(const IndirectMessageField<MessageType> &other) const {
    return msg_ != other.msg_;
  }
  bool operator!=(const IndirectMessageField<MessageType> &other) const {
    return !(*this == other);
  }

  absl::Status SerializeToBuffer(ProtoBuffer &buffer) const {
    return msg_.SerializeToBuffer(buffer);
  }

  absl::Status DeserializeFromBuffer(ProtoBuffer &buffer) {
    return msg_.DeserializeFromBuffer(buffer);
  }

private:
  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::BufferOffset *GetIndirectAddress(uint32_t offset) const {
    return GetBuffer()->template ToAddress<phaser::BufferOffset>(
        GetMessageBinaryStart() + offset);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  std::shared_ptr<MessageRuntime> GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
  MessageType msg_;
};

template <typename MessageType> class MessageObject {
public:
  MessageObject() = default;
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

  absl::Status SerializeToBuffer(ProtoBuffer &buffer) const {
    return msg_.SerializeToBuffer(buffer);
  }

  absl::Status DeserializeFromBuffer(ProtoBuffer &buffer) {
    return msg_.DeserializeFromBuffer(buffer);
  }

  bool empty() const { return msg_.runtime == nullptr; }

  void InstallMetadata() { msg_.template InstallMetadata<MessageType>(); }
  
private:
  MessageType msg_;
};

} // namespace phaser
