// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace phaser {
namespace ros_avro_record {

// Format name accepted by the stamped ROS Avro conversion plugin.
inline constexpr std::string_view kFormat = "stamped-ros-avro";

constexpr char kStampedRosBytesAvroSchemaJson[] =
    R"({"doc":"Stamped ROS wire bytes.","fields":[{"name":"stamp","type":{"fields":[{"name":"secs","type":"int"},{"name":"nsecs","type":"int"}],"name":"stamp_record","type":"record"}},{"name":"ros_msg","type":"bytes"}],"name":"stamped_ros_bytes","namespace":"phaser","type":"record"})";

// Passed as PhaserBankConvert options when `to` is `kFormat`.
struct StampedRosAvroOptions {
  int32_t stamp_secs;
  int32_t stamp_nsecs;
};

// One Avro binary record: stamp (secs, nsecs) + length-prefixed ros_msg bytes.
absl::Status WriteStampedRosBytesAvroRecord(int32_t stamp_secs, int32_t stamp_nsecs,
                                            absl::Span<const char> ros_bytes,
                                            std::string* out);

struct RosConnectionMetadata {
  std::string ros_datatype;
  std::string ros_md5;
  std::string ros_message_definition;
  std::string protobuf_full_name;
};

absl::StatusOr<RosConnectionMetadata> PhaserBankRosConnectionMetadata(
    std::string_view protobuf_full_name);

// Converts `from` into one stamped ROS-bytes Avro record. `from` must be
// "phaser", "protobuf", or "ros".
absl::Status ConvertToStampedRosBytesAvroRecord(
    std::string_view full_name, std::string_view from,
    absl::Span<const char> bytes, int32_t stamp_secs, int32_t stamp_nsecs,
    std::string* out);

absl::Status ProtobufToStampedRosBytesAvroRecord(
    std::string_view protobuf_full_name, std::string_view protobuf_bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out);

absl::Status PhaserToStampedRosBytesAvroRecord(
    std::string_view protobuf_full_name, absl::Span<const char> phaser_bytes,
    int32_t stamp_secs, int32_t stamp_nsecs, std::string* out);

}  // namespace ros_avro_record
}  // namespace phaser
