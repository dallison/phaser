// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/message_property.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "phaser/runtime/phaser_bank.h"

namespace phaser {
namespace {

absl::flat_hash_map<std::string, MessagePropertyFn>& MessageProperties() {
  static auto* properties =
      new absl::flat_hash_map<std::string, MessagePropertyFn>();
  return *properties;
}

template <typename Fn>
absl::StatusOr<std::string> MessageProperty(std::string_view full_name,
                                            Fn BankInfo::* member,
                                            std::string_view operation) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  const Fn function = (**bank_info).*member;
  if (function == nullptr) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Phaser message '%s' does not support %s", full_name, operation));
  }
  return std::string((*function)());
}

absl::StatusOr<std::string> RosDataType(std::string_view full_name) {
  return MessageProperty(full_name, &BankInfo::ros_data_type, "ROS datatype");
}

absl::StatusOr<std::string> RosMd5(std::string_view full_name) {
  return MessageProperty(full_name, &BankInfo::ros_md5, "ROS MD5");
}

absl::StatusOr<std::string> RosDefinition(std::string_view full_name) {
  return MessageProperty(full_name, &BankInfo::ros_definition, "ROS definition");
}

absl::StatusOr<std::string> ProtobufFullName(std::string_view full_name) {
  return MessageProperty(full_name, &BankInfo::protobuf_full_name,
                         "protobuf full name");
}

absl::StatusOr<std::string> ProtobufName(std::string_view full_name) {
  return MessageProperty(full_name, &BankInfo::protobuf_name, "protobuf name");
}

const absl::Status& BuiltinMessagePropertiesStatus() {
  static const MessagePropertyOp kProperties[] = {
      {kMessagePropertyRosDataType.data(), RosDataType},
      {kMessagePropertyRosMd5.data(), RosMd5},
      {kMessagePropertyRosDefinition.data(), RosDefinition},
      {kMessagePropertyProtobufFullName.data(), ProtobufFullName},
      {kMessagePropertyProtobufName.data(), ProtobufName},
  };
  static const absl::Status status =
      RegisterMessagePropertyPlugin(absl::MakeConstSpan(kProperties));
  return status;
}

}  // namespace

absl::Status RegisterMessagePropertyPlugin(
    absl::Span<const MessagePropertyOp> properties) {
  for (const MessagePropertyOp& property : properties) {
    if (property.name == nullptr || property.name[0] == '\0') {
      return absl::InvalidArgumentError("Message property name is empty");
    }
    if (property.invoke == nullptr) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Message property %s has no function", property.name));
    }
    const bool inserted =
        MessageProperties().emplace(property.name, property.invoke).second;
    if (!inserted) {
      return absl::AlreadyExistsError(absl::StrFormat(
          "Message property %s is already registered", property.name));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> CallMessageProperty(std::string_view property,
                                                std::string_view full_name) {
  const absl::Status& builtins = BuiltinMessagePropertiesStatus();
  if (!builtins.ok()) {
    return builtins;
  }
  const auto it = MessageProperties().find(std::string(property));
  if (it == MessageProperties().end()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("No message property %s is registered", property));
  }
  return it->second(full_name);
}

std::vector<std::string> ListMessageProperties() {
  (void)BuiltinMessagePropertiesStatus();
  std::vector<std::string> names;
  names.reserve(MessageProperties().size());
  for (const auto& [name, unused] : MessageProperties()) {
    (void)unused;
    names.push_back(name);
  }
  return names;
}

}  // namespace phaser
