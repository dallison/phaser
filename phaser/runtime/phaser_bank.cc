// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/phaser_bank.h"
#include "absl/strings/str_format.h"
#include <memory>

namespace phaser {

// We never delete this map, but that's fine because it's a singleton.
static absl::flat_hash_map<std::string, BankInfo> *phaser_banks_;

void PhaserBankRegisterMessage(const std::string &name, const BankInfo &info) {
  if (!phaser_banks_) {
    // Lazy init because we can't guarantee the order of static initialization.
    phaser_banks_ = new absl::flat_hash_map<std::string, BankInfo>;
  }
  (*phaser_banks_)[name] = info;
}

static absl::StatusOr<BankInfo *> GetBankInfo(std::string message_type) {
  auto it = phaser_banks_->find(message_type);
  if (it == phaser_banks_->end()) {
    return absl::InternalError(
        absl::StrFormat("Unknown phaser message type '%s'", message_type));
  }
  return &it->second;
}

absl::Status PhaserStreamTo(const std::string &message_type,
                                              const Message &msg,
                                              std::ostream &os, int indent) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  (*bank_info)->stream_to(msg, os, indent);
  return absl::OkStatus();
}

absl::StatusOr<std::string>
PhaserBankDebugString(const std::string &message_type, const Message &msg) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  std::ostringstream os;
  (*bank_info)->stream_to(msg, os, 0);
  return os.str();
}

absl::Status PhaserBankSerializeToBuffer(const std::string &message_type,
                                         const Message &msg,
                                         ProtoBuffer &buffer) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialize_to_buffer(msg, buffer);
}

absl::Status PhaserBankDeserializeFromBuffer(const std::string &message_type,
                                             Message &msg,
                                             ProtoBuffer &buffer) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->deserialize_from_buffer(msg, buffer);
}

absl::StatusOr<size_t> PhaserBankSerializedSize(const std::string &message_type,
                                                const Message &msg) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialized_size(msg);
}

absl::StatusOr<Message *>
PhaserBankAllocateAtOffset(const std::string &message_type,
                           std::shared_ptr<::phaser::MessageRuntime> runtime,
                           toolbelt::BufferOffset offset) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->allocate_at_offset(runtime, offset);
}

absl::Status PhaserBankClear(const std::string &message_type, Message &msg) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  (*bank_info)->clear(msg);
  return absl::OkStatus();
}

absl::Status PhaserBankCopy(const std::string &message_type, const Message &src,
                            Message &dst) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->copy(src, dst);
}

absl::StatusOr<const Message *>
PhaserBankMakeExisting(const std::string &message_type,
                       std::shared_ptr<::phaser::MessageRuntime> runtime,
                       const void *data) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->make_existing(runtime, data);
}

absl::StatusOr<size_t> PhaserBankBinarySize(const std::string &message_type) {
  absl::StatusOr<BankInfo *> bank_info = GetBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->binary_size();
}

} // namespace phaser
