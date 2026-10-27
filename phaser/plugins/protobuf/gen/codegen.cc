// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_gen.h"

namespace phaser {
namespace codegen {

void ProtobufMessageCodegen::EmitMessageWireHeader(MessageGenerator& generator,
                                                   std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  static absl::Status PhaserToProtobuf("
        "absl::Span<const char> phaser, ::phaser::ProtoBuffer& output);\n\n";
}

void ProtobufMessageCodegen::EmitMessageWireSource(MessageGenerator& generator,
                                                   std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "absl::Status " << name
     << "::PhaserToProtobuf(absl::Span<const char> phaser, "
        "::phaser::ProtoBuffer& output) {\n";
  os << "  if (phaser.empty()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Native Phaser payload is empty\");\n";
  os << "  }\n";
  os << "  " << name
     << " message = CreateReadonly(phaser.data(), phaser.size());\n";
  os << "  return message.Serialize(output);\n";
  os << "}\n\n";
}

void ProtobufMessageCodegen::EmitBankInfoFields(MessageGenerator& generator,
                                                std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  .phaser_to_protobuf = " << name << "::PhaserToProtobuf,\n";
  os << "  .protobuf_full_name = " << name << "::FullName,\n";
  os << "  .protobuf_name = " << name << "::Name,\n";
}

}  // namespace codegen

namespace {

void EmitProtobufWireMessageHeader(MessageGenerator& generator,
                                   std::ostream& os) {
  codegen::ProtobufMessageCodegen::EmitMessageWireHeader(generator, os);
}

void EmitProtobufWireMessageSource(MessageGenerator& generator,
                                   std::ostream& os) {
  codegen::ProtobufMessageCodegen::EmitMessageWireSource(generator, os);
}

void EmitProtobufBankInfoFields(MessageGenerator& generator, std::ostream& os) {
  codegen::ProtobufMessageCodegen::EmitBankInfoFields(generator, os);
}

const GeneratorPlugin kProtobufGeneratorPlugin = {
    "protobuf",
    1,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    EmitProtobufBankInfoFields,
    EmitProtobufWireMessageHeader,
    EmitProtobufWireMessageSource,
};

struct ProtobufGeneratorPluginRegistrar {
  ProtobufGeneratorPluginRegistrar() {
    RegisterGeneratorPlugin(kProtobufGeneratorPlugin);
  }
};

ProtobufGeneratorPluginRegistrar g_protobuf_generator_plugin_registrar;

}  // namespace

}  // namespace phaser
