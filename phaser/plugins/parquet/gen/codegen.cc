// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_gen.h"

namespace phaser {
namespace codegen {

void ParquetMessageCodegen::EmitMessageHeader(MessageGenerator& generator,
                                              std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
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

void ParquetMessageCodegen::EmitMessageSource(MessageGenerator& generator,
                                              std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);

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

void ParquetMessageCodegen::EmitBankInfoFields(MessageGenerator& generator,
                                               std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  .protobuf_to_parquet = " << name << "ProtobufToParquet,\n";
  os << "  .phaser_to_parquet = " << name << "PhaserToParquet,\n";
}

}  // namespace codegen

namespace {

void EmitParquetFileSourceIncludes(std::ostream& os) {
  os << "#include \"phaser/plugins/parquet/parquet.h\"\n";
}

void EmitParquetMessageHeader(MessageGenerator& generator, std::ostream& os) {
  codegen::ParquetMessageCodegen::EmitMessageHeader(generator, os);
}

void EmitParquetMessageSource(MessageGenerator& generator, std::ostream& os) {
  codegen::ParquetMessageCodegen::EmitMessageSource(generator, os);
}

void EmitParquetBankInfoFields(MessageGenerator& generator, std::ostream& os) {
  codegen::ParquetMessageCodegen::EmitBankInfoFields(generator, os);
}

const GeneratorPlugin kParquetGeneratorPlugin = {
    "parquet",
    22,
    nullptr,
    EmitParquetFileSourceIncludes,
    EmitParquetMessageHeader,
    EmitParquetMessageSource,
    EmitParquetBankInfoFields,
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
