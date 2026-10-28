// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/common/conversion_plugin.h"
#include "phaser/plugins/parquet/parquet.h"

namespace phaser {
namespace {

absl::Status ProtobufToParquet(std::string_view full_name,
                               absl::Span<const char> bytes, std::string* out,
                               const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::protobuf_to_parquet)> function =
      conversion_plugin::MessageConversion(
          full_name, &BankInfo::protobuf_to_parquet,
          "protobuf to Parquet conversion");
  if (!function.ok()) return function.status();
  return (*function)(conversion_plugin::InputBytes(bytes), out);
}

absl::Status PhaserToParquet(std::string_view full_name,
                             absl::Span<const char> bytes, std::string* out,
                             const void* options) {
  (void)options;
  absl::StatusOr<decltype(BankInfo::phaser_to_parquet)> function =
      conversion_plugin::MessageConversion(full_name, &BankInfo::phaser_to_parquet,
                                           "Phaser to Parquet conversion");
  if (!function.ok()) return function.status();
  return (*function)(bytes, out);
}

struct ParquetConversionPlugin {
  ParquetConversionPlugin() {
    static const char* const kFormats[] = {
        kPhaserFormat.data(), kProtobufFormat.data(), parquet::kFormat.data()};
    static const BankConversion kConversions[] = {
        {kPhaserFormat.data(), parquet::kFormat.data(),
         BankConversionDirection::kForward, PhaserToParquet, nullptr},
        {kProtobufFormat.data(), parquet::kFormat.data(),
         BankConversionDirection::kForward, ProtobufToParquet, nullptr},
    };
    conversion_plugin::RegisterOrAbort({"parquet", kFormats, kConversions});
  }
};

[[maybe_unused]] const ParquetConversionPlugin kParquetConversionPlugin;

}  // namespace
}  // namespace phaser
