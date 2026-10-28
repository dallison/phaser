// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/avro/avro.h"
#include "phaser/plugins/common/conversion_plugin.h"

namespace phaser {
namespace {

absl::Status ProtobufToAvro(std::string_view full_name,
                            absl::Span<const char> bytes, std::string* out,
                            const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::protobuf_to_avro)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::protobuf_to_avro,
                                           "protobuf to Avro conversion");
  if (!function.ok()) return function.status();
  return (*function)(conversion_plugin::InputBytes(bytes), out);
}

absl::Status PhaserToAvro(std::string_view full_name, absl::Span<const char> bytes,
                          std::string* out, const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::phaser_to_avro)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::phaser_to_avro,
                                           "Phaser to Avro conversion");
  if (!function.ok()) return function.status();
  return (*function)(bytes, out);
}

struct AvroConversionPlugin {
  AvroConversionPlugin() {
    static const char* const kFormats[] = {
        kPhaserFormat.data(), kProtobufFormat.data(), avro::kFormat.data()};
    static const BankConversion kConversions[] = {
        {kPhaserFormat.data(), avro::kFormat.data(),
         BankConversionDirection::kForward, PhaserToAvro, nullptr},
        {kProtobufFormat.data(), avro::kFormat.data(),
         BankConversionDirection::kForward, ProtobufToAvro, nullptr},
    };
    conversion_plugin::RegisterOrAbort({"avro", kFormats, kConversions});
  }
};

[[maybe_unused]] const AvroConversionPlugin kAvroConversionPlugin;

}  // namespace
}  // namespace phaser
