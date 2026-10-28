// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/common/conversion_plugin.h"

namespace phaser {
namespace {

absl::Status PhaserToRos(std::string_view full_name, absl::Span<const char> bytes,
                         std::string* out, const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::phaser_to_ros)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::phaser_to_ros,
                                           "Phaser to ROS conversion");
  if (!function.ok()) return function.status();
  ROSBuffer buffer;
  absl::Status status = (*function)(bytes, buffer);
  if (!status.ok()) return status;
  return conversion_plugin::FinishRos(buffer, out);
}

absl::Status ProtobufToRos(std::string_view full_name,
                           absl::Span<const char> bytes, std::string* out,
                           const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::protobuf_to_ros)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::protobuf_to_ros,
                                           "protobuf to ROS conversion");
  if (!function.ok()) return function.status();
  ROSBuffer buffer;
  absl::Status status = (*function)(conversion_plugin::InputBytes(bytes), buffer);
  if (!status.ok()) return status;
  return conversion_plugin::FinishRos(buffer, out);
}

absl::Status RosToProtobuf(std::string_view full_name,
                           absl::Span<const char> bytes, std::string* out,
                           const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::ros_to_protobuf)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::ros_to_protobuf,
                                           "ROS to protobuf conversion");
  if (!function.ok()) return function.status();
  ProtoBuffer buffer;
  absl::Status status = (*function)(bytes, buffer);
  if (!status.ok()) return status;
  return conversion_plugin::FinishProto(buffer, out);
}

struct RosConversionPlugin {
  RosConversionPlugin() {
    static const char* const kFormats[] = {
        kPhaserFormat.data(), kProtobufFormat.data(), kRosFormat.data()};
    static const BankConversion kConversions[] = {
        {kPhaserFormat.data(), kRosFormat.data(),
         BankConversionDirection::kForward, PhaserToRos, nullptr},
        {kProtobufFormat.data(), kRosFormat.data(),
         BankConversionDirection::kBoth, ProtobufToRos, RosToProtobuf},
    };
    conversion_plugin::RegisterOrAbort({"ros", kFormats, kConversions});
  }
};

[[maybe_unused]] const RosConversionPlugin kRosConversionPlugin;

}  // namespace
}  // namespace phaser
