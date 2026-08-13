// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/phaser_bank.h"

#include <memory>

#include "absl/strings/str_format.h"

namespace phaser {

std::unique_ptr<absl::flat_hash_map<std::string, BankInfo>> phaser_banks_;

absl::StatusOr<BankInfo*> GetPhaserBankInfo(std::string_view message_type) {
  if (!phaser_banks_) {
    return absl::InternalError("Phaser message bank is not initialized");
  }
  auto it = phaser_banks_->find(message_type);
  if (it == phaser_banks_->end()) {
    return absl::InternalError(
        absl::StrFormat("Unknown phaser message type '%s'", message_type));
  }
  return &it->second;
}

void PhaserBankRegisterMessage(std::string_view name, const BankInfo& info) {
  if (!phaser_banks_) {
    // Lazy init because we can't guarantee the order of static initialization.
    phaser_banks_ =
        std::make_unique<absl::flat_hash_map<std::string, BankInfo>>();
  }
  (*phaser_banks_)[std::string(name)] = info;
}

absl::Status PhaserStreamTo(const std::string& message_type, const Message& msg,
                            std::ostream& os, int indent) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  (*bank_info)->stream_to(msg, os, indent);
  return absl::OkStatus();
}

absl::StatusOr<std::string> PhaserBankDebugString(
    const std::string& message_type, const Message& msg) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  std::ostringstream os;
  (*bank_info)->stream_to(msg, os, 0);
  return os.str();
}

absl::Status PhaserBankSerializeToBuffer(const std::string& message_type,
                                         const Message& msg,
                                         ProtoBuffer& buffer) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialize_to_buffer(msg, buffer);
}

absl::Status PhaserBankDeserializeFromBuffer(const std::string& message_type,
                                             Message& msg,
                                             ProtoBuffer& buffer) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->deserialize_from_buffer(msg, buffer);
}

absl::StatusOr<size_t> PhaserBankSerializedSize(const std::string& message_type,
                                                const Message& msg) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialized_size(msg);
}

absl::Status PhaserBankSerializeAtOffset(
    std::string_view message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset, ProtoBuffer& buffer) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialize_at_offset(std::move(runtime), offset, buffer);
}

absl::Status PhaserBankDeserializeAtOffset(
    std::string_view message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset, ProtoBuffer& buffer) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->deserialize_at_offset(std::move(runtime), offset, buffer);
}

absl::StatusOr<size_t> PhaserBankSerializedSizeAtOffset(
    std::string_view message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialized_size_at_offset(std::move(runtime), offset);
}

absl::StatusOr<Message*> PhaserBankAllocateAtOffset(
    const std::string& message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->allocate_at_offset(runtime, offset);
}

absl::Status PhaserBankClear(const std::string& message_type, Message& msg) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  (*bank_info)->clear(msg);
  return absl::OkStatus();
}

absl::Status PhaserBankCopy(const std::string& message_type, const Message& src,
                            Message& dst) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->copy(src, dst);
}

absl::StatusOr<const Message*> PhaserBankMakeExisting(
    const std::string& message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime, const void* data) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->make_existing(runtime, data);
}

absl::StatusOr<size_t> PhaserBankBinarySize(std::string_view message_type) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->binary_size();
}

absl::StatusOr<const MessageInfo*> PhaserBankMessageInfo(
    const std::string& message_type) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->message_info();
}

absl::StatusOr<bool> PhaserBankHasField(const std::string& message_type,
                                        const Message& msg, int number) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->has_field(msg, number);
}
}  // namespace phaser
