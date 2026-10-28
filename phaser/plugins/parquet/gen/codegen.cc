// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_codegen_host.h"

namespace phaser {
namespace {

void EmitParquetMessageHeader(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
  os << "  static absl::Status ToParquet(absl::Span<const " << name
     << "> messages, std::string* out);\n";
  os << "  absl::Status ToParquet(std::string* out) const;\n";
  os << "  static absl::Status ProtobufToParquet(std::string_view bytes, "
        "std::string* out);\n";
  os << "  static absl::Status ProtobufToParquet("
        "absl::Span<const std::string_view> bytes, std::string* out);\n";
  os << "  static absl::Status PhaserToParquet(absl::Span<const char> bytes, "
        "std::string* out);\n";
  os << "  static absl::Status PhaserToParquet("
        "absl::Span<const absl::Span<const char>> bytes, std::string* out);\n";
}

void EmitParquetMessageSource(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());

  os << "absl::Status " << name << "::ToParquet(absl::Span<const " << name
     << "> messages, std::string* out) {\n";
  os << "  return ::phaser::parquet::WriteMessages(messages, out);\n";
  os << "}\n\n";

  os << "absl::Status " << name << "::ToParquet(std::string* out) const {\n";
  os << "  const " << name << "* _phaser_self = this;\n";
  os << "  return ToParquet(absl::MakeConstSpan(_phaser_self, 1), out);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ProtobufToParquet(std::string_view bytes, std::string* out) {\n";
  os << "  " << name << " message;\n";
  os << "  if (!message.ParseFromArray(bytes.data(), bytes.size())) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Protobuf wire bytes are not a valid message\");\n";
  os << "  }\n";
  os << "  return message.ToParquet(out);\n";
  os << "}\n\n";

  os << "absl::Status " << name << "::ProtobufToParquet("
        "absl::Span<const std::string_view> bytes, std::string* out) {\n";
  os << "  std::vector<" << name << "> messages;\n";
  os << "  messages.reserve(bytes.size());\n";
  os << "  for (std::string_view item : bytes) {\n";
  os << "    " << name << " message;\n";
  os << "    if (!message.ParseFromArray(item.data(), item.size())) {\n";
  os << "      return absl::InvalidArgumentError("
        "\"Protobuf wire bytes are not a valid message\");\n";
  os << "    }\n";
  os << "    messages.push_back(std::move(message));\n";
  os << "  }\n";
  os << "  return ToParquet(messages, out);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::PhaserToParquet(absl::Span<const char> bytes, std::string* out) {\n";
  os << "  if (bytes.empty()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Native Phaser payload is empty\");\n";
  os << "  }\n";
  os << "  " << name << " message = CreateReadonly(bytes.data(), bytes.size());\n";
  os << "  return message.ToParquet(out);\n";
  os << "}\n\n";

  os << "absl::Status " << name << "::PhaserToParquet("
        "absl::Span<const absl::Span<const char>> bytes, std::string* out) {\n";
  os << "  std::vector<" << name << "> messages;\n";
  os << "  messages.reserve(bytes.size());\n";
  os << "  for (absl::Span<const char> item : bytes) {\n";
  os << "    if (item.empty()) {\n";
  os << "      return absl::InvalidArgumentError("
        "\"Native Phaser payload is empty\");\n";
  os << "    }\n";
  os << "    messages.push_back(CreateReadonly(item.data(), item.size()));\n";
  os << "  }\n";
  os << "  return ToParquet(messages, out);\n";
  os << "}\n\n";

  os << "static absl::Status " << name
     << "ProtobufToParquet(std::string_view bytes, std::string* out) {\n";
  os << "  return " << name << "::ProtobufToParquet(bytes, out);\n";
  os << "}\n\n";
  os << "static absl::Status " << name
     << "PhaserToParquet(absl::Span<const char> bytes, std::string* out) {\n";
  os << "  return " << name << "::PhaserToParquet(bytes, out);\n";
  os << "}\n\n";
}

void EmitParquetBankInfoFields(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
  os << "  .protobuf_to_parquet = " << name << "ProtobufToParquet,\n";
  os << "  .phaser_to_parquet = " << name << "PhaserToParquet,\n";
}

void EmitParquetFileSourceIncludes(std::ostream& os) {
  os << "#include \"phaser/plugins/parquet/parquet.h\"\n";
}

void EmitParquetMessageHeaderFromGenerator(MessageGenerator& generator,
                                           std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitParquetMessageHeader(host, os);
}

void EmitParquetMessageSourceFromGenerator(MessageGenerator& generator,
                                           std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitParquetMessageSource(host, os);
}

void EmitParquetBankInfoFieldsFromGenerator(MessageGenerator& generator,
                                            std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitParquetBankInfoFields(host, os);
}

const GeneratorPlugin kParquetGeneratorPlugin = {
    "parquet",
    22,
    nullptr,
    EmitParquetFileSourceIncludes,
    EmitParquetMessageHeaderFromGenerator,
    EmitParquetMessageSourceFromGenerator,
    EmitParquetBankInfoFieldsFromGenerator,
    nullptr,
    nullptr,
};

struct ParquetGeneratorPluginRegistrar {
  ParquetGeneratorPluginRegistrar() {
    RegisterGeneratorPlugin(kParquetGeneratorPlugin);
  }
};

ParquetGeneratorPluginRegistrar g_parquet_generator_plugin_registrar;

}  // namespace
}  // namespace phaser
