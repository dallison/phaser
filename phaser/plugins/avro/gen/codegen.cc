// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_gen.h"

namespace phaser {
namespace codegen {

void AvroMessageCodegen::EmitMessageHeader(MessageGenerator& generator,
                                           std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  static absl::Status ToAvro(absl::Span<const " << name
     << "> messages, std::string* out);\n";
  os << "  absl::Status ToAvro(std::string* out) const;\n";
  os << "  static absl::Status ProtobufToAvro(std::string_view bytes, "
        "std::string* out);\n";
  os << "  static absl::Status ProtobufToAvro("
        "absl::Span<const std::string_view> bytes, std::string* out);\n";
  os << "  static absl::Status PhaserToAvro(absl::Span<const char> bytes, "
        "std::string* out);\n";
  os << "  static absl::Status PhaserToAvro("
        "absl::Span<const absl::Span<const char>> bytes, std::string* out);\n";
}

void AvroMessageCodegen::EmitMessageSource(MessageGenerator& generator,
                                           std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);

  os << "absl::Status " << name << "::ToAvro(absl::Span<const " << name
     << "> messages, std::string* out) {\n";
  os << "  return ::phaser::avro::WriteMessages(messages, out);\n";
  os << "}\n\n";

  os << "absl::Status " << name << "::ToAvro(std::string* out) const {\n";
  os << "  const " << name << "* _phaser_self = this;\n";
  os << "  return ToAvro(absl::MakeConstSpan(_phaser_self, 1), out);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ProtobufToAvro(std::string_view bytes, std::string* out) {\n";
  os << "  " << name << " message;\n";
  os << "  if (!message.ParseFromArray(bytes.data(), bytes.size())) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Protobuf wire bytes are not a valid message\");\n";
  os << "  }\n";
  os << "  return message.ToAvro(out);\n";
  os << "}\n\n";

  os << "absl::Status " << name << "::ProtobufToAvro("
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
  os << "  return ToAvro(messages, out);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::PhaserToAvro(absl::Span<const char> bytes, std::string* out) {\n";
  os << "  if (bytes.empty()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Native Phaser payload is empty\");\n";
  os << "  }\n";
  os << "  " << name << " message = CreateReadonly(bytes.data(), bytes.size());\n";
  os << "  return message.ToAvro(out);\n";
  os << "}\n\n";

  os << "absl::Status " << name << "::PhaserToAvro("
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
  os << "  return ToAvro(messages, out);\n";
  os << "}\n\n";

  os << "static absl::Status " << name
     << "ProtobufToAvro(std::string_view bytes, std::string* out) {\n";
  os << "  return " << name << "::ProtobufToAvro(bytes, out);\n";
  os << "}\n\n";
  os << "static absl::Status " << name
     << "PhaserToAvro(absl::Span<const char> bytes, std::string* out) {\n";
  os << "  return " << name << "::PhaserToAvro(bytes, out);\n";
  os << "}\n\n";
}

void AvroMessageCodegen::EmitBankInfoFields(MessageGenerator& generator,
                                            std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  .protobuf_to_avro = " << name << "ProtobufToAvro,\n";
  os << "  .phaser_to_avro = " << name << "PhaserToAvro,\n";
}

}  // namespace codegen

namespace {

void EmitAvroFileSourceIncludes(std::ostream& os) {
  os << "#include \"phaser/plugins/avro/avro.h\"\n";
}

void EmitAvroMessageHeader(MessageGenerator& generator, std::ostream& os) {
  codegen::AvroMessageCodegen::EmitMessageHeader(generator, os);
}

void EmitAvroMessageSource(MessageGenerator& generator, std::ostream& os) {
  codegen::AvroMessageCodegen::EmitMessageSource(generator, os);
}

void EmitAvroBankInfoFields(MessageGenerator& generator, std::ostream& os) {
  codegen::AvroMessageCodegen::EmitBankInfoFields(generator, os);
}

const GeneratorPlugin kAvroGeneratorPlugin = {
    "avro",
    21,
    nullptr,
    EmitAvroFileSourceIncludes,
    EmitAvroMessageHeader,
    EmitAvroMessageSource,
    EmitAvroBankInfoFields,
    nullptr,
    nullptr,
};

struct AvroGeneratorPluginRegistrar {
  AvroGeneratorPluginRegistrar() {
    RegisterGeneratorPlugin(kAvroGeneratorPlugin);
  }
};

AvroGeneratorPluginRegistrar g_avro_generator_plugin_registrar;

}  // namespace

}  // namespace phaser
