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
  return (*bank_info)
      ->deserialize_at_offset(std::move(runtime), offset, buffer);
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

namespace {

absl::Status MissingBankFunction(std::string_view full_name,
                                 std::string_view operation) {
  return absl::FailedPreconditionError(absl::StrFormat(
      "Phaser message '%s' does not support %s", full_name, operation));
}

}  // namespace

absl::Status PhaserBankProtobufToROS(std::string_view full_name,
                                     std::string_view bytes,
                                     ROSBuffer& output) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->protobuf_to_ros == nullptr) {
    return MissingBankFunction(full_name, "protobuf to ROS conversion");
  }
  return (*bank_info)->protobuf_to_ros(bytes, output);
}

absl::Status PhaserBankPhaserToROS(std::string_view full_name,
                                   absl::Span<const char> bytes,
                                   ROSBuffer& output) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->phaser_to_ros == nullptr) {
    return MissingBankFunction(full_name, "Phaser to ROS conversion");
  }
  return (*bank_info)->phaser_to_ros(bytes, output);
}

absl::StatusOr<std::string_view> PhaserBankRosDataType(
    std::string_view full_name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->ros_data_type == nullptr) {
    return MissingBankFunction(full_name, "ROS datatype");
  }
  return (*bank_info)->ros_data_type();
}

absl::StatusOr<std::string> PhaserBankRosMd5(std::string_view full_name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->ros_md5 == nullptr) {
    return MissingBankFunction(full_name, "ROS MD5");
  }
  return (*bank_info)->ros_md5();
}

absl::StatusOr<std::string_view> PhaserBankRosDefinition(
    std::string_view full_name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->ros_definition == nullptr) {
    return MissingBankFunction(full_name, "ROS definition");
  }
  return (*bank_info)->ros_definition();
}

absl::Status PhaserBankRosToProtobuf(std::string_view full_name,
                                     absl::Span<const char> bytes,
                                     ProtoBuffer& output) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->ros_to_protobuf == nullptr) {
    return MissingBankFunction(full_name, "ROS to protobuf conversion");
  }
  return (*bank_info)->ros_to_protobuf(bytes, output);
}

absl::Status PhaserBankPhaserToProtobuf(std::string_view full_name,
                                        absl::Span<const char> bytes,
                                        ProtoBuffer& output) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->phaser_to_protobuf == nullptr) {
    return MissingBankFunction(full_name, "Phaser to protobuf conversion");
  }
  return (*bank_info)->phaser_to_protobuf(bytes, output);
}

absl::StatusOr<std::string_view> PhaserBankProtobufFullName(
    std::string_view full_name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->protobuf_full_name == nullptr) {
    return MissingBankFunction(full_name, "protobuf full name");
  }
  return (*bank_info)->protobuf_full_name();
}

absl::StatusOr<std::string_view> PhaserBankProtobufName(
    std::string_view full_name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->protobuf_name == nullptr) {
    return MissingBankFunction(full_name, "protobuf name");
  }
  return (*bank_info)->protobuf_name();
}
}  // namespace phaser
