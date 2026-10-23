// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/ros_wireformat.h"
#include "phaser/runtime/wireformat.h"

namespace phaser {

struct BankInfo {
  void (*stream_to)(const Message& msg, std::ostream& os, int indent);
  absl::Status (*serialize_to_buffer)(const Message& msg, ProtoBuffer& buffer);
  absl::Status (*deserialize_from_buffer)(Message& msg, ProtoBuffer& buffer);
  size_t (*serialized_size)(const Message& msg);
  absl::Status (*serialize_at_offset)(
      std::shared_ptr<::phaser::MessageRuntime> runtime,
      toolbelt::BufferOffset offset, ProtoBuffer& buffer);
  absl::Status (*deserialize_at_offset)(
      std::shared_ptr<::phaser::MessageRuntime> runtime,
      toolbelt::BufferOffset offset, ProtoBuffer& buffer);
  size_t (*serialized_size_at_offset)(
      std::shared_ptr<::phaser::MessageRuntime> runtime,
      toolbelt::BufferOffset offset);
  Message* (*allocate_at_offset)(
      std::shared_ptr<::phaser::MessageRuntime> runtime,
      toolbelt::BufferOffset offset);
  std::pair<Message*, toolbelt::BufferOffset> (*allocate)(
      std::shared_ptr<::phaser::MessageRuntime> runtime);
  void (*clear)(Message& msg);
  absl::Status (*copy)(const Message& src, Message& dst);
  const Message* (*make_existing)(
      std::shared_ptr<::phaser::MessageRuntime> runtime, const void* data);
  size_t (*binary_size)();
  const MessageInfo* (*message_info)();
  bool (*has_field)(const Message& msg, int number);
  void* (*get_field_by_name)(Message& msg, const std::string& name);
  void* (*get_field_by_number)(Message& msg, int number);
  absl::Status (*protobuf_to_ros)(std::string_view bytes, ROSBuffer& output);
  absl::Status (*phaser_to_ros)(absl::Span<const char> bytes,
                                ROSBuffer& output);
  std::string_view (*ros_data_type)();
  std::string (*ros_md5)();
  std::string_view (*ros_definition)();
  absl::Status (*ros_to_protobuf)(absl::Span<const char> bytes,
                                  ProtoBuffer& output);
  absl::Status (*phaser_to_protobuf)(absl::Span<const char> bytes,
                                     ProtoBuffer& output);
  // Protobuf full name (package.Outer.Inner) and unqualified message name.
  std::string_view (*protobuf_full_name)();
  std::string_view (*protobuf_name)();
};

extern std::unique_ptr<absl::flat_hash_map<std::string, BankInfo>>
    phaser_banks_;

absl::StatusOr<BankInfo*> GetPhaserBankInfo(std::string_view message_type);

void PhaserBankRegisterMessage(std::string_view name, const BankInfo& info);

absl::Status PhaserStreamTo(const std::string& message_type, const Message& msg,
                            std::ostream& os, int indent);
absl::StatusOr<std::string> PhaserBankDebugString(
    const std::string& message_type, const Message& msg);

absl::Status PhaserBankSerializeToBuffer(const std::string& message_type,
                                         const Message& msg,
                                         ProtoBuffer& buffer);
absl::Status PhaserBankDeserializeFromBuffer(const std::string& message_type,
                                             Message& msg, ProtoBuffer& buffer);
absl::StatusOr<size_t> PhaserBankSerializedSize(const std::string& message_type,
                                                const Message& msg);
absl::Status PhaserBankSerializeAtOffset(
    std::string_view message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset, ProtoBuffer& buffer);
absl::Status PhaserBankDeserializeAtOffset(
    std::string_view message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset, ProtoBuffer& buffer);
absl::StatusOr<size_t> PhaserBankSerializedSizeAtOffset(
    std::string_view message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset);

// This allocates a message from the heap (using new) with its storage in the
// payload buffer.  The ownership of the heap memory is passed back to the
// caller, so you can make a std::unique_ptr<Message> from the result or
// otherwise manage the memory.
absl::StatusOr<Message*> PhaserBankAllocateAtOffset(
    const std::string& message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime,
    toolbelt::BufferOffset offset);

template <typename T>
inline absl::StatusOr<std::pair<T*, toolbelt::BufferOffset>> PhaserBankAllocate(
    const std::string& message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  auto [msg, offset] = (*bank_info)->allocate(runtime);
  return std::make_pair(static_cast<T*>(msg), offset);
}

template <>
inline absl::StatusOr<std::pair<Message*, toolbelt::BufferOffset>>
PhaserBankAllocate(const std::string& message_type,
                   std::shared_ptr<::phaser::MessageRuntime> runtime) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->allocate(runtime);
}

absl::Status PhaserBankClear(const std::string& message_type, Message& msg);

absl::Status PhaserBankCopy(const std::string& message_type, const Message& src,
                            Message& dst);

absl::StatusOr<const Message*> PhaserBankMakeExisting(
    const std::string& message_type,
    std::shared_ptr<::phaser::MessageRuntime> runtime, const void* data);

absl::StatusOr<size_t> PhaserBankBinarySize(std::string_view message_type);

absl::StatusOr<const MessageInfo*> PhaserBankMessageInfo(
    const std::string& message_type);

absl::StatusOr<bool> PhaserBankHasField(const std::string& message_type,
                                        const Message& msg, int number);

// Convert protobuf wire bytes or a native Phaser payload to ROS1 bytes.
// `full_name` is the message's protobuf full name, the same key used by
// PhaserBankRegisterMessage.
absl::Status PhaserBankProtobufToROS(std::string_view full_name,
                                     std::string_view bytes, ROSBuffer& output);
absl::Status PhaserBankPhaserToROS(std::string_view full_name,
                                   absl::Span<const char> bytes,
                                   ROSBuffer& output);

// ROS datatype, MD5, and definition. These are available when the message was
// compiled with ROS metadata.
absl::StatusOr<std::string_view> PhaserBankRosDataType(
    std::string_view full_name);
absl::StatusOr<std::string> PhaserBankRosMd5(std::string_view full_name);
absl::StatusOr<std::string_view> PhaserBankRosDefinition(
    std::string_view full_name);

// Convert ROS1 bytes or a native Phaser payload to protobuf wire bytes.
absl::Status PhaserBankRosToProtobuf(std::string_view full_name,
                                     absl::Span<const char> bytes,
                                     ProtoBuffer& output);
absl::Status PhaserBankPhaserToProtobuf(std::string_view full_name,
                                        absl::Span<const char> bytes,
                                        ProtoBuffer& output);

// Protobuf full name, such as "foo.bar.Outer.Inner". This is the same string
// as Descriptor::full_name() and the bank key.
absl::StatusOr<std::string_view> PhaserBankProtobufFullName(
    std::string_view full_name);
// Unqualified protobuf message name, such as "Inner".
absl::StatusOr<std::string_view> PhaserBankProtobufName(
    std::string_view full_name);

template <typename Field>
absl::StatusOr<Field*> inline PhaserBankGetFieldByName(
    const std::string& message_type, Message& msg, const std::string& name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  Field* result =
      reinterpret_cast<Field*>((*bank_info)->get_field_by_name(msg, name));
  return result;
}

template <typename Field>
absl::StatusOr<Field*> inline PhaserBankGetFieldByNumber(
    const std::string& message_type, Message& msg, int number) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  Field* result =
      reinterpret_cast<Field*>((*bank_info)->get_field_by_number(msg, number));
  return result;
}

}  // namespace phaser
