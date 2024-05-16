// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/gen.h"
#include "absl/strings/str_format.h"

namespace phaser {

bool CodeGenerator::Generate(
    const google::protobuf::FileDescriptor *file, const std::string &parameter,
    google::protobuf::compiler::GeneratorContext *generator_context,
    std::string *error) const {
  Generator gen(file);

  if (absl::Status status = gen.GenerateHeader(std::cout); !status.ok()) {
    *error = absl::StrFormat("Failed to generate header: %s", status.message());
    return false;
  }

  if (absl::Status status = gen.GenerateSource(std::cout); !status.ok()) {
    *error = absl::StrFormat("Failed to generate source: %s", status.message());
    return false;
  }

  return true;
}

absl::Status Generator::GenerateHeader(std::ostream &os) {
  return absl::OkStatus();
}

absl::Status Generator::GenerateSource(std::ostream &os) {
  return absl::OkStatus();
}
} // namespace phaser
