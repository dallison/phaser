// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/ros_avro_record.h"

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "phaser/runtime/message_property.h"
#include "phaser/runtime/phaser_bank.h"

namespace phaser {
namespace ros_avro_record {
namespace {

void WriteAvroLong(std::string* out, int64_t value) {
  uint64_t bits = static_cast<uint64_t>(value);
  uint64_t encoded = (bits << 1) ^ static_cast<uint64_t>(value >> 63);
  while (encoded > 0x7f) {
    out->push_back(static_cast<char>((encoded & 0x7fU) | 0x80U));
    encoded >>= 7;
  }
  out->push_back(static_cast<char>(encoded));
}

}  // namespace

absl::Status WriteStampedRosBytesAvroRecord(int32_t stamp_secs, int32_t stamp_nsecs,
                                            absl::Span<const char> ros_bytes,
                                            std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Avro output string is null");
  }
  out->clear();
  WriteAvroLong(out, stamp_secs);
  WriteAvroLong(out, stamp_nsecs);
  WriteAvroLong(out, static_cast<int64_t>(ros_bytes.size()));
  out->append(ros_bytes.data(), ros_bytes.size());
  return absl::OkStatus();
}

namespace {

absl::StatusOr<std::string> ConnectionProperty(std::string_view full_name,
                                              std::string_view property) {
  absl::StatusOr<std::string> value = CallMessageProperty(property, full_name);
  if (value.ok() || !absl::IsFailedPrecondition(value.status())) {
    return value;
  }
  return absl::FailedPreconditionError(absl::StrCat(
      "message ", full_name,
      " has no ROS connection metadata; enable ros_metadata on phaser_library"));
}

}  // namespace

absl::StatusOr<RosConnectionMetadata> PhaserBankRosConnectionMetadata(
    std::string_view protobuf_full_name) {
  absl::StatusOr<std::string> ros_datatype =
      ConnectionProperty(protobuf_full_name, kMessagePropertyRosDataType);
  if (!ros_datatype.ok()) {
    return ros_datatype.status();
  }
  absl::StatusOr<std::string> ros_md5 =
      ConnectionProperty(protobuf_full_name, kMessagePropertyRosMd5);
  if (!ros_md5.ok()) {
    return ros_md5.status();
  }
  absl::StatusOr<std::string> ros_definition =
      ConnectionProperty(protobuf_full_name, kMessagePropertyRosDefinition);
  if (!ros_definition.ok()) {
    return ros_definition.status();
  }
  absl::StatusOr<std::string> full_name = ConnectionProperty(
      protobuf_full_name, kMessagePropertyProtobufFullName);
  if (!full_name.ok()) {
    return full_name.status();
  }
  RosConnectionMetadata metadata;
  metadata.ros_datatype = *std::move(ros_datatype);
  metadata.ros_md5 = *std::move(ros_md5);
  metadata.ros_message_definition = *std::move(ros_definition);
  metadata.protobuf_full_name = *std::move(full_name);
  return metadata;
}

absl::Status ConvertToStampedRosBytesAvroRecord(
    std::string_view full_name, std::string_view from,
    absl::Span<const char> bytes, int32_t stamp_secs, int32_t stamp_nsecs,
    std::string* out) {
  if (from != kPhaserFormat && from != kProtobufFormat && from != kRosFormat) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "stamped-ros-avro does not accept %s", from));
  }
  const StampedRosAvroOptions options{stamp_secs, stamp_nsecs};
  return PhaserBankConvert(full_name, from, bytes, kFormat, out, &options);
}

absl::Status ProtobufToStampedRosBytesAvroRecord(
    std::string_view protobuf_full_name, std::string_view protobuf_bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out) {
  std::string ros;
  absl::Status status = PhaserBankConvert(
      protobuf_full_name, kProtobufFormat,
      absl::Span<const char>(protobuf_bytes.data(), protobuf_bytes.size()),
      kRosFormat, &ros);
  if (!status.ok()) {
    return status;
  }
  return WriteStampedRosBytesAvroRecord(
      stamp_secs, stamp_nsecs, absl::Span<const char>(ros.data(), ros.size()),
      out);
}

absl::Status PhaserToStampedRosBytesAvroRecord(
    std::string_view protobuf_full_name, absl::Span<const char> phaser_bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out) {
  std::string ros;
  absl::Status status =
      PhaserBankConvert(protobuf_full_name, kPhaserFormat, phaser_bytes, kRosFormat,
                        &ros);
  if (!status.ok()) {
    return status;
  }
  return WriteStampedRosBytesAvroRecord(
      stamp_secs, stamp_nsecs, absl::Span<const char>(ros.data(), ros.size()),
      out);
}

}  // namespace ros_avro_record
}  // namespace phaser
