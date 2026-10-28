// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "phaser/compiler/message_gen.h"

namespace phaser {
namespace {

std::vector<const GeneratorPlugin*>& Plugins() {
  static auto* plugins = new std::vector<const GeneratorPlugin*>();
  return *plugins;
}

void RegisterOrAbort(const GeneratorPlugin& plugin) {
  if (plugin.name == nullptr || plugin.name[0] == '\0') {
    std::fprintf(stderr, "phaser: generator plugin name is empty\n");
    std::abort();
  }
  for (const GeneratorPlugin* existing : Plugins()) {
    if (std::string(existing->name) == plugin.name) {
      std::fprintf(stderr, "phaser: generator plugin %s is already registered\n",
                   plugin.name);
      std::abort();
    }
  }
  Plugins().push_back(&plugin);
}

const std::vector<const GeneratorPlugin*>& PluginsSortedByPriority() {
  static std::vector<const GeneratorPlugin*> sorted;
  sorted = Plugins();
  std::sort(sorted.begin(), sorted.end(),
            [](const GeneratorPlugin* a, const GeneratorPlugin* b) {
              return a->priority < b->priority;
            });
  return sorted;
}

}  // namespace

void RegisterGeneratorPlugin(const GeneratorPlugin& plugin) {
  RegisterOrAbort(plugin);
}

void EmitGeneratorPluginFileHeaderIncludes(std::ostream& os) {
  for (const GeneratorPlugin* plugin : Plugins()) {
    if (plugin->emit_file_header_includes != nullptr) {
      plugin->emit_file_header_includes(os);
    }
  }
}

void EmitGeneratorPluginFileSourceIncludes(std::ostream& os) {
  for (const GeneratorPlugin* plugin : Plugins()) {
    if (plugin->emit_file_source_includes != nullptr) {
      plugin->emit_file_source_includes(os);
    }
  }
}

void EmitGeneratorPluginMessageHeader(MessageGenerator& generator,
                                      std::ostream& os) {
  for (const GeneratorPlugin* plugin : PluginsSortedByPriority()) {
    if (plugin->emit_message_header != nullptr) {
      plugin->emit_message_header(generator, os);
    }
  }
}

void EmitGeneratorPluginMessageSource(MessageGenerator& generator,
                                      std::ostream& os) {
  for (const GeneratorPlugin* plugin : PluginsSortedByPriority()) {
    if (plugin->emit_message_source != nullptr) {
      plugin->emit_message_source(generator, os);
    }
  }
}

void EmitGeneratorPluginBankInfoFields(MessageGenerator& generator,
                                       std::ostream& os) {
  for (const GeneratorPlugin* plugin : PluginsSortedByPriority()) {
    if (plugin->emit_bank_info_fields != nullptr) {
      plugin->emit_bank_info_fields(generator, os);
    }
  }
}

void EmitGeneratorPluginWireMessageHeader(MessageGenerator& generator,
                                          std::ostream& os) {
  for (const GeneratorPlugin* plugin : PluginsSortedByPriority()) {
    if (plugin->emit_message_wire_header != nullptr) {
      plugin->emit_message_wire_header(generator, os);
    }
  }
}

void EmitGeneratorPluginWireMessageSource(MessageGenerator& generator,
                                          std::ostream& os) {
  for (const GeneratorPlugin* plugin : PluginsSortedByPriority()) {
    if (plugin->emit_message_wire_source != nullptr) {
      plugin->emit_message_wire_source(generator, os);
    }
  }
}

}  // namespace phaser
