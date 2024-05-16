#pragma once
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/wireformat.h"

namespace phaser {

struct BankInfo {
  void (*stream_to)(const Message &msg, std::ostream &os, int indent);
  absl::Status (*serialize_to_buffer)(const Message &msg, ProtoBuffer &buffer);
  absl::Status (*deserialize_from_buffer)(Message &msg, ProtoBuffer &buffer);
  size_t (*serialized_size)(const Message &msg);
  Message *(*allocate_at_offset)(
      std::shared_ptr<::phaser::MessageRuntime> runtime,
      toolbelt::BufferOffset offset);
  void (*clear)(Message &msg);
  absl::Status (*copy)(const Message &src, Message &dst);
  const Message *(*make_existing)(
      std::shared_ptr<::phaser::MessageRuntime> runtime, const void *data);
  size_t (*binary_size)();
};

void PhaserBankRegisterMessage(const std::string &name, const BankInfo &info);

absl::Status PhaserStreamTo(const std::string &message_type,
                                             const Message &msg,
                                             std::ostream &os, int indent);
absl::StatusOr<std::string>
PhaserBankDebugString(const std::string &message_type, const Message &msg);

absl::Status PhaserBankSerializeToBuffer(const std::string &message_type,
                                         const Message &msg,
                                         ProtoBuffer &buffer);
absl::Status PhaserBankDeserializeFromBuffer(const std::string &message_type,
                                             Message &msg, ProtoBuffer &buffer);
absl::StatusOr<size_t> PhaserBankSerializedSize(const std::string &message_type,
                                                const Message &msg);

// This allocates a message from the heap (using new) with its storage in the
// payload buffer.  The ownership of the heap memory is passed back to the
// caller, so you can make a std::unique_ptr<Message> from the result or
// otherwise manage the memory.
absl::StatusOr<Message *>
PhaserBankAllocateAtOffset(const std::string &message_type,
                           std::shared_ptr<::phaser::MessageRuntime> runtime,
                           toolbelt::BufferOffset offset);

absl::Status PhaserBankClear(const std::string &message_type, Message &msg);

absl::Status PhaserBankCopy(const std::string &message_type, const Message &src,
                            Message &dst);

absl::StatusOr<const Message *>
PhaserBankMakeExisting(const std::string &message_type,
                       std::shared_ptr<::phaser::MessageRuntime> runtime,
                       const void *data);

absl::StatusOr<size_t> PhaserBankBinarySize(const std::string &message_type);

} // namespace phaser
