#pragma once

// Fields that handle google.protobuf.Any messages.
// These are mesasges that contain two fields:
// 1. A string called 'type_url' that specifies the type of the message
// 2. A bytes field called 'value' that contains a message.
//
// In regular protobuf, the 'value' field is a serialized message, but
// in phaser, the value is the binary of the message.
//
// Protobuf provides a 'PackFrom' method that
#include "phaser/runtime/fields.h"

namespace phaser {

class AnyField : public Field {
public:
  AnyField(uint32_t boff, uint32_t offset, int id, int number)
      : Field(id, number), source_offset_(boff),
        relative_binary_offset_(offset) {}

  bool IsPresent() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return false;
    }
    toolbelt::BufferOffset *addr = GetIndirectAddress(offset);
    return *addr != 0;
  }

  void Clear() {
    toolbelt::BufferOffset *addr = GetIndirectAddress(relative_binary_offset_);
    if (*addr == 0) {
      return;
    }
    // Delete the memory in the payload buffer.
    GetBuffer()->Free(GetBuffer()->ToAddress(*addr));
    // Zero out the offset to the message.
    *addr = 0;
  }

  bool operator==(const AnyField &other) const { return false; }
  bool operator!=(const AnyField &other) const { return false; }

  size_t SerializedSize() const { return 0; }

  absl::Status Serialize(ProtoBuffer &buffer) const { return absl::OkStatus(); }

  absl::Status Deserialize(ProtoBuffer &buffer) { return absl::OkStatus(); }

  bool PackFrom(const Message& msg) {
    return true;
  }

  template <typename T>
  bool PackFrom(const T& msg) {
    return true;
  }

  bool UnpackTo(Message *msg) const {
    return true;
  }

  template <typename T>
  bool UnpackTo(T *msg) const {
    return true;
  }

  template <typename T>
  bool Is() const {
    return true;
  }
  
private:
  toolbelt::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  toolbelt::BufferOffset *GetIndirectAddress(uint32_t offset) const {
    return GetBuffer()->template ToAddress<toolbelt::BufferOffset>(
        GetMessageBinaryStart() + offset);
  }

  toolbelt::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  std::shared_ptr<MessageRuntime> GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  uint32_t relative_binary_offset_;
};

} // namespace phaser
