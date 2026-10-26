// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/arrow.h"
#include "phaser/runtime/ros_avro_record.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/ros_wireformat.h"
#include "phaser/runtime/wireformat.h"

namespace phaser {

struct BankInfo {
  void (*stream_to)(const Message& msg, std::ostream& os, int indent);
  absl::Status (*serialize_to_buffer)(const Message& msg, ProtoBuffer& buffer);
  absl::Status (*deserialize_from_buffer)(Message& msg, ProtoBuffer& buffer);
  size_t (*serialized_size)(const Message& msg);
  absl::Status (*serialize_at_offset)(::phaser::RuntimeHandle runtime,
                                      ::phaser::BufferOffset offset,
                                      ProtoBuffer& buffer);
  absl::Status (*deserialize_at_offset)(::phaser::RuntimeHandle runtime,
                                        ::phaser::BufferOffset offset,
                                        ProtoBuffer& buffer);
  size_t (*serialized_size_at_offset)(::phaser::RuntimeHandle runtime,
                                      ::phaser::BufferOffset offset);
  Message* (*allocate_at_offset)(::phaser::RuntimeHandle runtime,
                                 ::phaser::BufferOffset offset);
  std::pair<Message*, ::phaser::BufferOffset> (*allocate)(
      ::phaser::RuntimeHandle runtime);
  void (*clear)(Message& msg);
  absl::Status (*copy)(const Message& src, Message& dst);
  const Message* (*make_existing)(::phaser::RuntimeHandle runtime,
                                  const void* data);
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
  // Arrow conversion. Null when the message was registered without it.
  absl::Status (*append_arrow_schema)(::phaser::arrow::SchemaBuilder* builder,
                                      std::string_view name, bool nullable,
                                      const ::phaser::arrow::TypeSet* types);
  absl::Status (*collect_arrow_types_at_offset)(::phaser::RuntimeHandle runtime,
                                                ::phaser::BufferOffset offset,
                                                ::phaser::arrow::TypeSet* types);
  absl::Status (*write_arrow_at_offset)(::phaser::RuntimeHandle runtime,
                                        ::phaser::BufferOffset offset,
                                        ::phaser::arrow::FieldWriter* writer,
                                        const ::phaser::arrow::TypeSet* types);
  absl::Status (*read_arrow_at_offset)(::phaser::RuntimeHandle runtime,
                                       ::phaser::BufferOffset offset,
                                       const ::phaser::arrow::FieldReader* reader);
  // One message, phaser or protobuf wire, written as Avro or Parquet.
  // Null when the message was registered without these conversions.
  absl::Status (*protobuf_to_avro)(std::string_view bytes, std::string* out);
  absl::Status (*phaser_to_avro)(absl::Span<const char> bytes, std::string* out);
  absl::Status (*protobuf_to_parquet)(std::string_view bytes, std::string* out);
  absl::Status (*phaser_to_parquet)(absl::Span<const char> bytes,
                                    std::string* out);
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
absl::Status PhaserBankSerializeAtOffset(std::string_view message_type,
                                         ::phaser::RuntimeHandle runtime,
                                         ::phaser::BufferOffset offset,
                                         ProtoBuffer& buffer);
absl::Status PhaserBankDeserializeAtOffset(std::string_view message_type,
                                           ::phaser::RuntimeHandle runtime,
                                           ::phaser::BufferOffset offset,
                                           ProtoBuffer& buffer);
absl::StatusOr<size_t> PhaserBankSerializedSizeAtOffset(
    std::string_view message_type, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset);

// This allocates a message from the heap (using new) with its storage in the
// payload buffer.  The ownership of the heap memory is passed back to the
// caller, so you can make a std::unique_ptr<Message> from the result or
// otherwise manage the memory.
absl::StatusOr<Message*> PhaserBankAllocateAtOffset(
    const std::string& message_type, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset);

template <typename T>
inline absl::StatusOr<std::pair<T*, ::phaser::BufferOffset>> PhaserBankAllocate(
    const std::string& message_type, ::phaser::RuntimeHandle runtime) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  auto [msg, offset] = (*bank_info)->allocate(runtime);
  if (msg == nullptr) {
    return internal::AllocationFailedError();
  }
  return std::make_pair(static_cast<T*>(msg), offset);
}

template <>
inline absl::StatusOr<std::pair<Message*, ::phaser::BufferOffset>>
PhaserBankAllocate(const std::string& message_type,
                   ::phaser::RuntimeHandle runtime) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  std::pair<Message*, ::phaser::BufferOffset> result =
      (*bank_info)->allocate(runtime);
  if (result.first == nullptr) {
    return internal::AllocationFailedError();
  }
  return result;
}

absl::Status PhaserBankClear(const std::string& message_type, Message& msg);

absl::Status PhaserBankCopy(const std::string& message_type, const Message& src,
                            Message& dst);

absl::StatusOr<const Message*> PhaserBankMakeExisting(
    const std::string& message_type, ::phaser::RuntimeHandle runtime,
    const void* data);

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

// Arrow schema and row conversion for a message looked up by protobuf full
// name. Used for Any payloads, whose concrete type is not known to the
// message that contains them.
absl::Status PhaserBankAppendArrowSchema(std::string_view full_name,
                                         ::phaser::arrow::SchemaBuilder* builder,
                                         std::string_view name, bool nullable,
                                         const ::phaser::arrow::TypeSet* types);
absl::Status PhaserBankCollectArrowTypesAtOffset(
    std::string_view full_name, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset, ::phaser::arrow::TypeSet* types);
absl::Status PhaserBankWriteArrowAtOffset(
    std::string_view full_name, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset, ::phaser::arrow::FieldWriter* writer,
    const ::phaser::arrow::TypeSet* types);
absl::Status PhaserBankReadArrowAtOffset(
    std::string_view full_name, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset, const ::phaser::arrow::FieldReader* reader);

// Write one protobuf message, or one native Phaser payload, as an Avro object
// container or an uncompressed Parquet file. `full_name` is the protobuf full
// name. There is no conversion back from Avro or Parquet.
absl::Status PhaserBankProtobufToAvro(std::string_view full_name,
                                      std::string_view bytes, std::string* out);
absl::Status PhaserBankPhaserToAvro(std::string_view full_name,
                                    absl::Span<const char> bytes,
                                    std::string* out);
absl::Status PhaserBankProtobufToParquet(std::string_view full_name,
                                         std::string_view bytes,
                                         std::string* out);
absl::Status PhaserBankPhaserToParquet(std::string_view full_name,
                                       absl::Span<const char> bytes,
                                       std::string* out);

// Formats accepted by PhaserBankConvert. Each value is an input or output of
// at least one bank conversion.
enum class BankInputType {
  kPhaser,
  kProtobuf,
  kRos,
};

enum class BankOutputType {
  kProtobuf,
  kRos,
  kAvro,
  kParquet,
  // Avro record bytes: stamp (secs, nsecs) + length-prefixed ROS wire bytes.
  kStampedRosBytesAvroRecord,
};

// Dispatches to the bank conversion for this input and output. `bytes` is one
// message in `input`. `out` receives one message in `output`. A pair with no
// bank conversion returns an error.
absl::Status PhaserBankConvert(std::string_view full_name, BankInputType input,
                               absl::Span<const char> bytes,
                               BankOutputType output, std::string* out);

absl::Status PhaserBankConvertToStampedRosBytesAvroRecord(
    std::string_view full_name, BankInputType input, absl::Span<const char> bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out);

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
