// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/phaser_bank.h"

#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
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

struct ConversionEdgeKey {
  std::string from;
  std::string to;

  template <typename H>
  friend H AbslHashValue(H hash, const ConversionEdgeKey& key) {
    return H::combine(std::move(hash), key.from, key.to);
  }

  bool operator==(const ConversionEdgeKey& other) const {
    return from == other.from && to == other.to;
  }
};

absl::flat_hash_map<ConversionEdgeKey, BankConvertFn>& ConversionEdges() {
  // Function-local so a plugin static initializer can register before the
  // dynamic initialization of this translation unit.
  static auto* edges =
      new absl::flat_hash_map<ConversionEdgeKey, BankConvertFn>();
  return *edges;
}

absl::Status InstallEdge(const char* from, const char* to, BankConvertFn convert) {
  const bool inserted =
      ConversionEdges()
          .emplace(ConversionEdgeKey{from, to}, convert)
          .second;
  if (!inserted) {
    return absl::AlreadyExistsError(absl::StrFormat(
        "Phaser bank conversion from %s to %s is already registered", from, to));
  }
  return absl::OkStatus();
}

absl::Status RequireFunction(BankConvertFn function, const char* which,
                             const char* plugin) {
  if (function == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Conversion plugin %s is missing its %s function", plugin, which));
  }
  return absl::OkStatus();
}

bool DeclaredFormat(absl::Span<const char* const> formats, const char* name) {
  for (const char* format : formats) {
    if (format != nullptr && std::string_view(format) == name) {
      return true;
    }
  }
  return false;
}

absl::Status RejectFunction(BankConvertFn function, const char* which,
                            const char* plugin) {
  if (function != nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Conversion plugin %s sets its %s function for a direction that does "
        "not use it",
        plugin, which));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status PhaserBankRegisterConversionPlugin(
    const BankConversionPlugin& plugin) {
  if (plugin.name == nullptr || plugin.name[0] == '\0') {
    return absl::InvalidArgumentError("Conversion plugin name is empty");
  }
  for (size_t i = 0; i < plugin.formats.size(); ++i) {
    const char* format = plugin.formats[i];
    if (format == nullptr || format[0] == '\0') {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Conversion plugin %s has an empty format name", plugin.name));
    }
    for (size_t earlier = 0; earlier < i; ++earlier) {
      if (std::string_view(plugin.formats[earlier]) == format) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Conversion plugin %s lists format %s more than once", plugin.name,
            format));
      }
    }
  }
  for (const BankConversion& conversion : plugin.conversions) {
    if (conversion.from == nullptr || conversion.from[0] == '\0' ||
        conversion.to == nullptr || conversion.to[0] == '\0') {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Conversion plugin %s has an empty format name", plugin.name));
    }
    if (std::string_view(conversion.from) == conversion.to) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Conversion plugin %s converts %s to itself", plugin.name,
          conversion.from));
    }
    if (!DeclaredFormat(plugin.formats, conversion.from) ||
        !DeclaredFormat(plugin.formats, conversion.to)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Conversion plugin %s does not accept a conversion from %s to %s",
          plugin.name, conversion.from, conversion.to));
    }
    switch (conversion.direction) {
      case BankConversionDirection::kForward: {
        absl::Status status =
            RequireFunction(conversion.forward, "forward", plugin.name);
        if (!status.ok()) return status;
        status = RejectFunction(conversion.reverse, "reverse", plugin.name);
        if (!status.ok()) return status;
        status = InstallEdge(conversion.from, conversion.to, conversion.forward);
        if (!status.ok()) return status;
        break;
      }
      case BankConversionDirection::kReverse: {
        absl::Status status =
            RequireFunction(conversion.reverse, "reverse", plugin.name);
        if (!status.ok()) return status;
        status = RejectFunction(conversion.forward, "forward", plugin.name);
        if (!status.ok()) return status;
        status = InstallEdge(conversion.to, conversion.from, conversion.reverse);
        if (!status.ok()) return status;
        break;
      }
      case BankConversionDirection::kBoth: {
        absl::Status status =
            RequireFunction(conversion.forward, "forward", plugin.name);
        if (!status.ok()) return status;
        status = RequireFunction(conversion.reverse, "reverse", plugin.name);
        if (!status.ok()) return status;
        status = InstallEdge(conversion.from, conversion.to, conversion.forward);
        if (!status.ok()) return status;
        status = InstallEdge(conversion.to, conversion.from, conversion.reverse);
        if (!status.ok()) return status;
        break;
      }
    }
  }
  return absl::OkStatus();
}

absl::Status PhaserBankConvert(std::string_view full_name, std::string_view from,
                               absl::Span<const char> bytes, std::string_view to,
                               std::string* out, const void* options) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("conversion output string is null");
  }
  out->clear();
  const auto it = ConversionEdges().find(
      ConversionEdgeKey{std::string(from), std::string(to)});
  if (it == ConversionEdges().end()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Phaser bank cannot convert %s to %s", from, to));
  }
  return it->second(full_name, bytes, out, options);
}

std::vector<std::string> PhaserBankRegisteredFormats() {
  absl::flat_hash_set<std::string> formats;
  for (const auto& [edge, unused] : ConversionEdges()) {
    (void)unused;
    formats.insert(edge.from);
    formats.insert(edge.to);
  }
  return std::vector<std::string>(formats.begin(), formats.end());
}

bool PhaserBankHasConversion(std::string_view from, std::string_view to) {
  return ConversionEdges().contains(
      ConversionEdgeKey{std::string(from), std::string(to)});
}

std::vector<std::pair<std::string, std::string>> PhaserBankConversionEdges() {
  std::vector<std::pair<std::string, std::string>> edges;
  edges.reserve(ConversionEdges().size());
  for (const auto& [edge, unused] : ConversionEdges()) {
    (void)unused;
    edges.emplace_back(edge.from, edge.to);
  }
  return edges;
}
}  // namespace phaser
