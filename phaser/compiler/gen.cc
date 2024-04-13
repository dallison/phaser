#include "phaser/compiler/gen.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace phaser {

bool CodeGenerator::Generate(
    const google::protobuf::FileDescriptor *file, const std::string &parameter,
    google::protobuf::compiler::GeneratorContext *generator_context,
    std::string *error) const {
  Generator gen(file);

  if (absl::Status status = gen.GenerateHeaders(std::cerr); !status.ok()) {
    *error = absl::StrFormat("Failed to generate header: %s", status.message());
    return false;
  }

  if (absl::Status status = gen.GenerateSources(std::cerr); !status.ok()) {
    *error = absl::StrFormat("Failed to generate source: %s", status.message());
    return false;
  }

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
}

absl::Status Generator::GenerateHeaders(std::ostream &os) {
  os << "#pragma once\n";
  OpenNamespace(os);

  for (auto &msg_gen : message_gens_) {
    if (absl::Status status = msg_gen->GenerateHeader(os); !status.ok()) {
      return status;
    }
  }

  CloseNamespace(os);
  return absl::OkStatus();
}

absl::Status Generator::GenerateSources(std::ostream &os) {
  OpenNamespace(os);

  for (auto &msg_gen : message_gens_) {
    if (absl::Status status = msg_gen->GenerateSource(os); !status.ok()) {
      return status;
    }
  }

  CloseNamespace(os);
  return absl::OkStatus();
}
} // namespace phaser
