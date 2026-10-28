// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_codegen_host.h"
#include "phaser/plugins/protobuf/gen/protobuf_wire_codegen.h"

namespace phaser {
namespace {

void EmitProtobufWireMessageHeader(MessageCodegenHost& host, std::ostream& os) {
  protobuf_wire_codegen::GenerateProtobufSerialization(host, os);
  protobuf_wire_codegen::GenerateSerializedSize(host, os, true);
  protobuf_wire_codegen::GenerateSerializer(host, os, true);
  protobuf_wire_codegen::GenerateDeserializer(host, os, true);

  const std::string name = host.MessageName(host.message());
  os << "  static absl::Status PhaserToProtobuf("
        "absl::Span<const char> phaser, ::phaser::ProtoBuffer& output);\n\n";
}

void EmitProtobufWireMessageSource(MessageCodegenHost& host, std::ostream& os) {
  protobuf_wire_codegen::GenerateSerializedSize(host, os, false);
  protobuf_wire_codegen::GenerateSerializer(host, os, false);
  protobuf_wire_codegen::GenerateDeserializer(host, os, false);

  const std::string name = host.MessageName(host.message());
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

void EmitProtobufBankInfoFields(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
  os << "  .phaser_to_protobuf = " << name << "::PhaserToProtobuf,\n";
  os << "  .protobuf_full_name = " << name << "::FullName,\n";
  os << "  .protobuf_name = " << name << "::Name,\n";
}

void EmitProtobufWireMessageHeaderFromGenerator(MessageGenerator& generator,
                                                std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitProtobufWireMessageHeader(host, os);
}

void EmitProtobufWireMessageSourceFromGenerator(MessageGenerator& generator,
                                                std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitProtobufWireMessageSource(host, os);
}

void EmitProtobufBankInfoFieldsFromGenerator(MessageGenerator& generator,
                                             std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitProtobufBankInfoFields(host, os);
}

const GeneratorPlugin kProtobufGeneratorPlugin = {
    "protobuf",
    1,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    EmitProtobufBankInfoFieldsFromGenerator,
    EmitProtobufWireMessageHeaderFromGenerator,
    EmitProtobufWireMessageSourceFromGenerator,
};

struct ProtobufGeneratorPluginRegistrar {
  ProtobufGeneratorPluginRegistrar() {
    RegisterGeneratorPlugin(kProtobufGeneratorPlugin);
  }
};

ProtobufGeneratorPluginRegistrar g_protobuf_generator_plugin_registrar;

}  // namespace

}  // namespace phaser
