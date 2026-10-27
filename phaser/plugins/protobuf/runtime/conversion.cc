// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/common/conversion_plugin.h"

namespace phaser {
namespace {

absl::Status PhaserToProtobuf(std::string_view full_name,
                              absl::Span<const char> bytes, std::string* out,
                              const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::phaser_to_protobuf)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::phaser_to_protobuf,
                                           "Phaser to protobuf conversion");
  if (!function.ok()) return function.status();
  ProtoBuffer buffer;
  absl::Status status = (*function)(bytes, buffer);
  if (!status.ok()) return status;
  return conversion_plugin::FinishProto(buffer, out);
}

struct ProtobufConversionPlugin {
  ProtobufConversionPlugin() {
    static const char* const kFormats[] = {kPhaserFormat.data(),
                                           kProtobufFormat.data()};
    static const BankConversion kConversions[] = {
        {kPhaserFormat.data(), kProtobufFormat.data(),
         BankConversionDirection::kForward, PhaserToProtobuf, nullptr},
    };
    conversion_plugin::RegisterOrAbort(
        {"protobuf", kFormats, kConversions});
  }
};

[[maybe_unused]] const ProtobufConversionPlugin kProtobufConversionPlugin;

}  // namespace
}  // namespace phaser
