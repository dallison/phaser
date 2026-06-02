// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include "google/protobuf/compiler/code_generator.h"
#include "google/protobuf/compiler/plugin.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "absl/status/status.h"

#include "phaser/compiler/message_gen.h"
#include "phaser/compiler/enum_gen.h"

#include <iostream>
#include <vector>
#include <memory>

namespace phaser {

class CodeGenerator : public google::protobuf::compiler::CodeGenerator {
public:
  CodeGenerator() = default;
  bool Generate(const google::protobuf::FileDescriptor *file,
                const std::string &parameter,
                google::protobuf::compiler::GeneratorContext *generator_context,
                std::string *error) const override;

  uint64_t GetSupportedFeatures() const override {
    return FEATURE_PROTO3_OPTIONAL;
  }
  mutable std::string added_namespace_;
  mutable std::string package_name_;
  mutable std::string target_name_;
  // When true, generated message types get a public `std::any active_message`
  // field. Enabled via the `active_message=true` plugin command-line option
  // (set by phaser_library(enable_active_message = True)).
  mutable bool generate_active_message_ = false;
};


class Generator {
public:
  Generator(const google::protobuf::FileDescriptor *file, const std::string& ns, const std::string& pn, const std::string& tn, bool generate_active_message = false);

  void GenerateHeaders(std::ostream& os);
  void GenerateSources(std::ostream& os);

private:
  void OpenNamespace(std::ostream& os);
  void CloseNamespace(std::ostream& os);

  const google::protobuf::FileDescriptor *file_;
  std::vector<std::unique_ptr<MessageGenerator>> message_gens_;
  std::vector<std::unique_ptr<EnumGenerator>> enum_gens_;
  const std::string& added_namespace_;
  const std::string& package_name_;
  const std::string& target_name_;
  bool generate_active_message_;
};

} // namespace phaser
