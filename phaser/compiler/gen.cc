#include "phaser/compiler/gen.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include <filesystem>

namespace phaser {

bool CodeGenerator::Generate(
    const google::protobuf::FileDescriptor *file, const std::string &parameter,
    google::protobuf::compiler::GeneratorContext *generator_context,
    std::string *error) const {
  Generator gen(file);

  gen.GenerateHeaders(std::cerr);
  gen.GenerateSources(std::cerr);

  return true;
}

void Generator::OpenNamespace(std::ostream &os) {
  std::vector<std::string> parts = absl::StrSplit(file_->package(), '.');
  for (const auto &part : parts) {
    os << "namespace " << part << " {\n";
  }
}

void Generator::CloseNamespace(std::ostream &os) {
  std::vector<std::string> parts = absl::StrSplit(file_->package(), '.');
  for (const auto &part : parts) {
    os << "} // namespace " << part << "\n";
  }
}

Generator::Generator(const google::protobuf::FileDescriptor *file)
    : file_(file) {
  for (int i = 0; i < file->message_type_count(); i++) {
    message_gens_.push_back(
        std::make_unique<MessageGenerator>(file->message_type(i)));
  }
  // Enums
  for (int i = 0; i < file->enum_type_count(); i++) {
    enum_gens_.push_back(
        std::make_unique<EnumGenerator>(file->enum_type(i)));
  }
}

void Generator::GenerateHeaders(std::ostream &os) {
  os << "#pragma once\n";
  os << "#include \"phaser/runtime/runtime.h\"\n";
  for (int i = 0; i < file_->dependency_count(); i++) {
    std::filesystem::path p(file_->dependency(i)->name());
    p.replace_extension(".phaser.h");
    os << "#include \"" << p.string() << "\"\n";
  }

  OpenNamespace(os);

  // Enums
  for (auto &enum_gen : enum_gens_) {
    enum_gen->GenerateHeader(os);
  }

   for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateEnums(os);
  }

  for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateHeader(os);
  }

  CloseNamespace(os);
}

void Generator::GenerateSources(std::ostream &os) {
  OpenNamespace(os);

  for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateSource(os);
  }

  CloseNamespace(os);
}
} // namespace phaser
