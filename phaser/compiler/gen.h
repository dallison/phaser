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
};


class Generator {
public:
  Generator(const google::protobuf::FileDescriptor *file, const std::string& ns);

  void GenerateHeaders(std::ostream& os);
  void GenerateSources(std::ostream& os);

private:
  void OpenNamespace(std::ostream& os);
  void CloseNamespace(std::ostream& os);

  const google::protobuf::FileDescriptor *file_;
  std::vector<std::unique_ptr<MessageGenerator>> message_gens_;
  std::vector<std::unique_ptr<EnumGenerator>> enum_gens_;
  const std::string& added_namespace_;
};

} // namespace phaser
