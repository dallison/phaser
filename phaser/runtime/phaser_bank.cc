// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/ros_avro_record.h"
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

absl::Status PhaserBankSerializeAtOffset(std::string_view message_type,
                                         ::phaser::RuntimeHandle runtime,
                                         ::phaser::BufferOffset offset,
                                         ProtoBuffer& buffer) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialize_at_offset(runtime, offset, buffer);
}

absl::Status PhaserBankDeserializeAtOffset(std::string_view message_type,
                                           ::phaser::RuntimeHandle runtime,
                                           ::phaser::BufferOffset offset,
                                           ProtoBuffer& buffer) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->deserialize_at_offset(runtime, offset, buffer);
}

absl::StatusOr<size_t> PhaserBankSerializedSizeAtOffset(
    std::string_view message_type, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  return (*bank_info)->serialized_size_at_offset(runtime, offset);
}

absl::StatusOr<Message*> PhaserBankAllocateAtOffset(
    const std::string& message_type, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset) {
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
    const std::string& message_type, ::phaser::RuntimeHandle runtime,
    const void* data) {
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

absl::Status PhaserBankAppendArrowSchema(
    std::string_view full_name, ::phaser::arrow::SchemaBuilder* builder,
    std::string_view name, bool nullable, const ::phaser::arrow::TypeSet* types) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->append_arrow_schema == nullptr) {
    return MissingBankFunction(full_name, "Arrow schema");
  }
  return (*bank_info)->append_arrow_schema(builder, name, nullable, types);
}

absl::Status PhaserBankCollectArrowTypesAtOffset(
    std::string_view full_name, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset, ::phaser::arrow::TypeSet* types) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->collect_arrow_types_at_offset == nullptr) {
    return MissingBankFunction(full_name, "Arrow type collection");
  }
  return (*bank_info)->collect_arrow_types_at_offset(runtime, offset, types);
}

absl::Status PhaserBankWriteArrowAtOffset(
    std::string_view full_name, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset, ::phaser::arrow::FieldWriter* writer,
    const ::phaser::arrow::TypeSet* types) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->write_arrow_at_offset == nullptr) {
    return MissingBankFunction(full_name, "Arrow writing");
  }
  return (*bank_info)->write_arrow_at_offset(runtime, offset, writer, types);
}

absl::Status PhaserBankReadArrowAtOffset(
    std::string_view full_name, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset, const ::phaser::arrow::FieldReader* reader) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->read_arrow_at_offset == nullptr) {
    return MissingBankFunction(full_name, "Arrow reading");
  }
  return (*bank_info)->read_arrow_at_offset(runtime, offset, reader);
}

absl::Status PhaserBankProtobufToAvro(std::string_view full_name,
                                      std::string_view bytes, std::string* out) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->protobuf_to_avro == nullptr) {
    return MissingBankFunction(full_name, "protobuf to Avro conversion");
  }
  return (*bank_info)->protobuf_to_avro(bytes, out);
}

absl::Status PhaserBankPhaserToAvro(std::string_view full_name,
                                    absl::Span<const char> bytes,
                                    std::string* out) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->phaser_to_avro == nullptr) {
    return MissingBankFunction(full_name, "Phaser to Avro conversion");
  }
  return (*bank_info)->phaser_to_avro(bytes, out);
}

absl::Status PhaserBankProtobufToParquet(std::string_view full_name,
                                         std::string_view bytes,
                                         std::string* out) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->protobuf_to_parquet == nullptr) {
    return MissingBankFunction(full_name, "protobuf to Parquet conversion");
  }
  return (*bank_info)->protobuf_to_parquet(bytes, out);
}

absl::Status PhaserBankPhaserToParquet(std::string_view full_name,
                                       absl::Span<const char> bytes,
                                       std::string* out) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  if ((*bank_info)->phaser_to_parquet == nullptr) {
    return MissingBankFunction(full_name, "Phaser to Parquet conversion");
  }
  return (*bank_info)->phaser_to_parquet(bytes, out);
}

namespace {

std::string_view InputBytes(absl::Span<const char> bytes) {
  return std::string_view(bytes.data(), bytes.size());
}

const char* InputName(BankInputType input) {
  switch (input) {
    case BankInputType::kPhaser:
      return "Phaser";
    case BankInputType::kProtobuf:
      return "protobuf";
    case BankInputType::kRos:
      return "ROS";
  }
  return "an unknown input";
}

const char* OutputName(BankOutputType output) {
  switch (output) {
    case BankOutputType::kProtobuf:
      return "protobuf";
    case BankOutputType::kRos:
      return "ROS";
    case BankOutputType::kAvro:
      return "Avro";
    case BankOutputType::kParquet:
      return "Parquet";
    case BankOutputType::kStampedRosBytesAvroRecord:
      return "stamped ROS bytes Avro record";
  }
  return "an unknown output";
}

absl::Status UnsupportedConversion(BankInputType input, BankOutputType output) {
  return absl::InvalidArgumentError(absl::StrFormat(
      "Phaser bank cannot convert %s to %s", InputName(input),
      OutputName(output)));
}

absl::Status FinishProto(const ProtoBuffer& buffer, std::string* out) {
  *out = buffer.AsString();
  return absl::OkStatus();
}

absl::Status FinishRos(const ROSBuffer& buffer, std::string* out) {
  *out = buffer.AsString();
  return absl::OkStatus();
}

}  // namespace

absl::Status PhaserBankConvert(std::string_view full_name, BankInputType input,
                               absl::Span<const char> bytes,
                               BankOutputType output, std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("conversion output string is null");
  }
  out->clear();
  switch (input) {
    case BankInputType::kProtobuf:
      switch (output) {
        case BankOutputType::kRos: {
          ROSBuffer buffer;
          absl::Status status =
              PhaserBankProtobufToROS(full_name, InputBytes(bytes), buffer);
          if (!status.ok()) return status;
          return FinishRos(buffer, out);
        }
        case BankOutputType::kAvro:
          return PhaserBankProtobufToAvro(full_name, InputBytes(bytes), out);
        case BankOutputType::kParquet:
          return PhaserBankProtobufToParquet(full_name, InputBytes(bytes), out);
        case BankOutputType::kProtobuf:
        case BankOutputType::kStampedRosBytesAvroRecord:
          return UnsupportedConversion(input, output);
      }
      break;
    case BankInputType::kPhaser:
      switch (output) {
        case BankOutputType::kProtobuf: {
          ProtoBuffer buffer;
          absl::Status status =
              PhaserBankPhaserToProtobuf(full_name, bytes, buffer);
          if (!status.ok()) return status;
          return FinishProto(buffer, out);
        }
        case BankOutputType::kRos: {
          ROSBuffer buffer;
          absl::Status status = PhaserBankPhaserToROS(full_name, bytes, buffer);
          if (!status.ok()) return status;
          return FinishRos(buffer, out);
        }
        case BankOutputType::kAvro:
          return PhaserBankPhaserToAvro(full_name, bytes, out);
        case BankOutputType::kParquet:
          return PhaserBankPhaserToParquet(full_name, bytes, out);
        case BankOutputType::kStampedRosBytesAvroRecord:
          return UnsupportedConversion(input, output);
      }
      break;
    case BankInputType::kRos:
      switch (output) {
        case BankOutputType::kProtobuf: {
          ProtoBuffer buffer;
          absl::Status status =
              PhaserBankRosToProtobuf(full_name, bytes, buffer);
          if (!status.ok()) return status;
          return FinishProto(buffer, out);
        }
        case BankOutputType::kRos:
        case BankOutputType::kAvro:
        case BankOutputType::kParquet:
        case BankOutputType::kStampedRosBytesAvroRecord:
          return UnsupportedConversion(input, output);
      }
      break;
  }
  return absl::InvalidArgumentError("unknown Phaser bank conversion");
}

absl::Status PhaserBankConvertToStampedRosBytesAvroRecord(
    std::string_view full_name, BankInputType input, absl::Span<const char> bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("conversion output string is null");
  }
  out->clear();
  switch (input) {
    case BankInputType::kProtobuf:
      return ros_avro_record::PhaserBankProtobufToStampedRosBytesAvroRecord(
          full_name, InputBytes(bytes), stamp_secs, stamp_nsecs, out);
    case BankInputType::kPhaser:
      return ros_avro_record::PhaserBankPhaserToStampedRosBytesAvroRecord(
          full_name, bytes, stamp_secs, stamp_nsecs, out);
    case BankInputType::kRos:
      return ros_avro_record::WriteStampedRosBytesAvroRecord(stamp_secs, stamp_nsecs,
                                                             bytes, out);
  }
  return absl::InvalidArgumentError("unknown Phaser bank input type");
}
}  // namespace phaser
