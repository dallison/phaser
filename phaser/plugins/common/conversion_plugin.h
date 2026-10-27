// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Helpers for the built-in conversion plugins. A new plugin does not need this
// header. It calls PhaserBankRegisterConversionPlugin from its own static
// initializer.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "phaser/runtime/phaser_bank.h"

namespace phaser {
namespace conversion_plugin {

inline std::string_view InputBytes(absl::Span<const char> bytes) {
  return std::string_view(bytes.data(), bytes.size());
}

inline absl::Status FinishProto(const ProtoBuffer& buffer, std::string* out) {
  *out = buffer.AsString();
  return absl::OkStatus();
}

inline absl::Status FinishRos(const ROSBuffer& buffer, std::string* out) {
  *out = buffer.AsString();
  return absl::OkStatus();
}

template <typename Fn>
absl::StatusOr<Fn> MessageConversion(std::string_view full_name,
                                     Fn BankInfo::* member,
                                     std::string_view operation) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  const Fn function = (**bank_info).*member;
  if (function == nullptr) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Phaser message '%s' does not support %s", full_name, operation));
  }
  return function;
}

inline void RegisterOrAbort(const BankConversionPlugin& plugin) {
  const absl::Status status = PhaserBankRegisterConversionPlugin(plugin);
  if (!status.ok()) {
    std::fprintf(stderr, "phaser: %s\n",
                 std::string(status.message()).c_str());
    std::abort();
  }
}

}  // namespace conversion_plugin
}  // namespace phaser
