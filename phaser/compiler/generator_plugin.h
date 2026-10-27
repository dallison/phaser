// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <cstdint>
#include <iosfwd>

namespace phaser {

class MessageGenerator;

// A compile-time plugin for the Phaser message generator. Link a library that
// registers one of these (typically from a static initializer) into the
// phaser compiler binary. The core generator calls these hooks instead of
// hard-coding format-specific methods.
struct GeneratorPlugin {
  const char* name;
  // Lower values run first for wire and BankInfo field emission.
  uint8_t priority;
  void (*emit_file_header_includes)(std::ostream& os);
  void (*emit_file_source_includes)(std::ostream& os);
  void (*emit_message_header)(MessageGenerator& generator, std::ostream& os);
  void (*emit_message_source)(MessageGenerator& generator, std::ostream& os);
  void (*emit_bank_info_fields)(MessageGenerator& generator, std::ostream& os);
  // Wire-format methods (ROS, protobuf bank edges). Run before columnar hooks.
  void (*emit_message_wire_header)(MessageGenerator& generator,
                                   std::ostream& os);
  void (*emit_message_wire_source)(MessageGenerator& generator,
                                   std::ostream& os);
};

void RegisterGeneratorPlugin(const GeneratorPlugin& plugin);

void EmitGeneratorPluginFileHeaderIncludes(std::ostream& os);
void EmitGeneratorPluginFileSourceIncludes(std::ostream& os);
void EmitGeneratorPluginMessageHeader(MessageGenerator& generator,
                                      std::ostream& os);
void EmitGeneratorPluginMessageSource(MessageGenerator& generator,
                                      std::ostream& os);
void EmitGeneratorPluginBankInfoFields(MessageGenerator& generator,
                                       std::ostream& os);
void EmitGeneratorPluginWireMessageHeader(MessageGenerator& generator,
                                          std::ostream& os);
void EmitGeneratorPluginWireMessageSource(MessageGenerator& generator,
                                          std::ostream& os);

}  // namespace phaser
