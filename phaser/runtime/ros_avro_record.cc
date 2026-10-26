// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/ros_avro_record.h"

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "phaser/runtime/phaser_bank.h"
#include "phaser/runtime/ros_wireformat.h"

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

absl::StatusOr<RosConnectionMetadata> PhaserBankRosConnectionMetadata(
    std::string_view protobuf_full_name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(protobuf_full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  BankInfo* bank = *bank_info;
  if (bank->ros_data_type == nullptr || bank->ros_md5 == nullptr ||
      bank->ros_definition == nullptr || bank->protobuf_full_name == nullptr) {
    return absl::FailedPreconditionError(absl::StrCat(
        "message ", protobuf_full_name,
        " has no ROS connection metadata; enable ros_metadata on phaser_library"));
  }
  RosConnectionMetadata metadata;
  metadata.ros_datatype = std::string(bank->ros_data_type());
  metadata.ros_md5 = std::string(bank->ros_md5());
  metadata.ros_message_definition = std::string(bank->ros_definition());
  metadata.protobuf_full_name = std::string(bank->protobuf_full_name());
  return metadata;
}

absl::Status PhaserBankProtobufToStampedRosBytesAvroRecord(
    std::string_view protobuf_full_name, std::string_view protobuf_bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out) {
  ROSBuffer ros;
  absl::Status status =
      PhaserBankProtobufToROS(protobuf_full_name, protobuf_bytes, ros);
  if (!status.ok()) {
    return status;
  }
  return WriteStampedRosBytesAvroRecord(stamp_secs, stamp_nsecs, ros.AsSpan(), out);
}

absl::Status PhaserBankPhaserToStampedRosBytesAvroRecord(
    std::string_view protobuf_full_name, absl::Span<const char> phaser_bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out) {
  ROSBuffer ros;
  absl::Status status =
      PhaserBankPhaserToROS(protobuf_full_name, phaser_bytes, ros);
  if (!status.ok()) {
    return status;
  }
  return WriteStampedRosBytesAvroRecord(stamp_secs, stamp_nsecs, ros.AsSpan(), out);
}

}  // namespace ros_avro_record
}  // namespace phaser
