// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/common/conversion_plugin.h"
#include "phaser/runtime/ros_avro_record.h"

namespace phaser {
namespace {

const ros_avro_record::StampedRosAvroOptions* StampOptions(const void* options) {
  return static_cast<const ros_avro_record::StampedRosAvroOptions*>(options);
}

absl::Status RequireStamp(const void* options) {
  if (options == nullptr) {
    return absl::InvalidArgumentError(
        "stamped-ros-avro conversion requires StampedRosAvroOptions");
  }
  return absl::OkStatus();
}

absl::Status ProtobufToStamped(std::string_view full_name,
                               absl::Span<const char> bytes, std::string* out,
                               const void* options) {
  absl::Status status = RequireStamp(options);
  if (!status.ok()) return status;
  const ros_avro_record::StampedRosAvroOptions* stamp = StampOptions(options);
  return ros_avro_record::ProtobufToStampedRosBytesAvroRecord(
      full_name, conversion_plugin::InputBytes(bytes), stamp->stamp_secs,
      stamp->stamp_nsecs, out);
}

absl::Status PhaserToStamped(std::string_view full_name,
                             absl::Span<const char> bytes, std::string* out,
                             const void* options) {
  absl::Status status = RequireStamp(options);
  if (!status.ok()) return status;
  const ros_avro_record::StampedRosAvroOptions* stamp = StampOptions(options);
  return ros_avro_record::PhaserToStampedRosBytesAvroRecord(
      full_name, bytes, stamp->stamp_secs, stamp->stamp_nsecs, out);
}

absl::Status RosToStamped(std::string_view full_name, absl::Span<const char> bytes,
                          std::string* out, const void* options) {
  (void)full_name;
  absl::Status status = RequireStamp(options);
  if (!status.ok()) return status;
  const ros_avro_record::StampedRosAvroOptions* stamp = StampOptions(options);
  return ros_avro_record::WriteStampedRosBytesAvroRecord(
      stamp->stamp_secs, stamp->stamp_nsecs, bytes, out);
}

struct StampedRosAvroConversionPlugin {
  StampedRosAvroConversionPlugin() {
    static const char* const kFormats[] = {
        kPhaserFormat.data(), kProtobufFormat.data(), kRosFormat.data(),
        ros_avro_record::kFormat.data()};
    static const BankConversion kConversions[] = {
        {kProtobufFormat.data(), ros_avro_record::kFormat.data(),
         BankConversionDirection::kForward, ProtobufToStamped, nullptr},
        {kPhaserFormat.data(), ros_avro_record::kFormat.data(),
         BankConversionDirection::kForward, PhaserToStamped, nullptr},
        {kRosFormat.data(), ros_avro_record::kFormat.data(),
         BankConversionDirection::kForward, RosToStamped, nullptr},
    };
    conversion_plugin::RegisterOrAbort(
        {"stamped-ros-avro", kFormats, kConversions});
  }
};

[[maybe_unused]] const StampedRosAvroConversionPlugin
    kStampedRosAvroConversionPlugin;

}  // namespace
}  // namespace phaser
