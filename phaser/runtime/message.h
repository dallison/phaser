#pragma once

#include "absl/container/flat_hash_map.h"
#include "phaser/runtime/payload_buffer.h"
#include <memory>
#include <stdint.h>
#include <string>

namespace phaser {

// Message header.
// --------------
// Each message starts with a header which is:
// - The absolute offset of the FieldData for this message.
// - The presence mask - 1 bit per field.  This is variable in
//   size but is always a multiple of 32 bits.

// FieldData is a structure that contains the field numbers and offsets for a
// message. It is stored in the payload buffer.
struct FieldData {
  uint32_t num;
  struct {
    uint32_t number;
    uint32_t offset : 24; // Offset into message.
    uint32_t id : 8;      // Field id for presence bit mask.
  } fields[];
};

// Each message contains a std::shared_ptr to one of these, allocated from
// the heap.  This is used when creating a message in the payload buffer
// so that we know where the metadata for each message is stored.  The
// metadata offset is held in the message header.
struct MessageRuntime {
  MessageRuntime(phaser::PayloadBuffer *p) : pb(p) {}
  virtual ~MessageRuntime() = default;
  phaser::PayloadBuffer *pb;

  virtual void AddMetadata(const std::string &name,
                           phaser::BufferOffset offset) {}
  virtual phaser::BufferOffset GetMetadata(const std::string &name) {
    return 0;
  }
};

// This is a message runtime for a message that is mutable.  It holds a mapping
// for each message name to the offset of the metadata in the payload buffer.
struct MutableMessageRuntime : public MessageRuntime {
  MutableMessageRuntime(phaser::PayloadBuffer *p) : MessageRuntime(p) {}

  absl::flat_hash_map<std::string, phaser::BufferOffset> metadata_offsets;
  void AddMetadata(const std::string &name,
                   phaser::BufferOffset offset) override {
    metadata_offsets[name] = offset;
  }

  phaser::BufferOffset GetMetadata(const std::string &name) override {
    auto it = metadata_offsets.find(name);
    if (it == metadata_offsets.end()) {
      return 0;
    }
    return it->second;
  }
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
// | phaser::PayloadBuffer |  |
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
  Message() = default;
  Message(std::shared_ptr<MessageRuntime> rt, phaser::BufferOffset start)
      : runtime(rt), absolute_binary_offset(start) {}
  std::shared_ptr<MessageRuntime> runtime;
  phaser::BufferOffset absolute_binary_offset;

  // 'field' is the offset from the start of the message to the field (positive)
  // Subtract the field offset from the field to get the address of the
  // std::shared_ptr to the pointer to the phaser::PayloadBuffer.
  static phaser::PayloadBuffer *GetBuffer(const void *field, uint32_t offset) {
    const std::shared_ptr<MessageRuntime> *rt =
        reinterpret_cast<const std::shared_ptr<MessageRuntime> *>(
            reinterpret_cast<const char *>(field) - offset);
    return (*rt)->pb;
  }

  static phaser::PayloadBuffer **GetBufferAddr(const void *field,
                                               uint32_t offset) {
    const std::shared_ptr<MessageRuntime> *rt =
        reinterpret_cast<const std::shared_ptr<MessageRuntime> *>(
            reinterpret_cast<const char *>(field) - offset);
    return &(*rt)->pb;
  }

  static std::shared_ptr<MessageRuntime> GetRuntime(void *field,
                                                    uint32_t offset) {
    std::shared_ptr<MessageRuntime> *rt =
        reinterpret_cast<std::shared_ptr<MessageRuntime> *>(
            reinterpret_cast<char *>(field) - offset);
    return *rt;
  }

  static std::shared_ptr<MessageRuntime> GetRuntime(const void *field,
                                                    uint32_t offset) {
    const std::shared_ptr<MessageRuntime> *rt =
        reinterpret_cast<const std::shared_ptr<MessageRuntime> *>(
            reinterpret_cast<const char *>(field) - offset);
    return *rt;
  }

  static const Message *GetMessage(const void *field, uint32_t offset) {
    const Message *msg = reinterpret_cast<const Message *>(
        reinterpret_cast<const char *>(field) - offset);
    return msg;
  }

  static Message *GetMessage(void *field, uint32_t offset) {
    Message *msg =
        reinterpret_cast<Message *>(reinterpret_cast<char *>(field) - offset);
    return msg;
  }

  static phaser::BufferOffset GetMessageBinaryStart(const void *field,
                                                    uint32_t offset) {
    const Message *msg = reinterpret_cast<const Message *>(
        reinterpret_cast<const char *>(field) - offset);
    return msg->absolute_binary_offset;
  }

  template <typename MessageType> void InstallMetadata() {
    auto metadata = runtime->GetMetadata(MessageType::GetName());
    if (metadata != 0) {
      phaser::BufferOffset *header =
          runtime->pb->ToAddress<phaser::BufferOffset>(absolute_binary_offset);
      *header = metadata;
      return;
    }

    // Allocate space for field data in the payload buffer and copy it in.
    void *fields = phaser::PayloadBuffer::Allocate(
        &runtime->pb, sizeof(MessageType::field_data), 4, false);
    memcpy(fields, &MessageType::field_data, sizeof(MessageType::field_data));
    phaser::BufferOffset *header =
        runtime->pb->ToAddress<phaser::BufferOffset>(absolute_binary_offset);
    *header = runtime->pb->ToOffset(fields);
    runtime->AddMetadata(MessageType::GetName(), *header);
  }

  // Looks for the field number in the field data. Returns the offset of the
  // field if found, -1 otherwise.
  int32_t FindFieldOffset(uint32_t field_number) const;

  // Similar for field id for presence bit mask.
  int32_t FindFieldId(uint32_t field_number) const;
};

} // namespace phaser
