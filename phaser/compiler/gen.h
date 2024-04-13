#pragma once

#include "google/protobuf/compiler/code_generator.h"
#include "google/protobuf/compiler/plugin.h"
#include "google/protobuf/descriptor.h"
#include "absl/status/status.h"

#include "phaser/compiler/message_gen.h"

#include <iostream>
#include <vector>
#include <memory>

namespace phaser {

class CodeGenerator : public google::protobuf::compiler::CodeGenerator {
public:
  bool Generate(const google::protobuf::FileDescriptor *file,
                const std::string &parameter,
                google::protobuf::compiler::GeneratorContext *generator_context,
                std::string *error) const override;

  uint64_t GetSupportedFeatures() const override {
    return FEATURE_PROTO3_OPTIONAL;
  }
};

class Generator {
public:
  Generator(const google::protobuf::FileDescriptor *file);

  absl::Status GenerateHeaders(std::ostream& os);
  absl::Status GenerateSources(std::ostream& os);

private:
  void OpenNamespace(std::ostream& os);
  void CloseNamespace(std::ostream& os);

  const google::protobuf::FileDescriptor *file_;
  std::vector<std::unique_ptr<MessageGenerator>> message_gens_;
};

} // namespace phaser
