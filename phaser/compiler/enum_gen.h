// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"

namespace phaser {

class EnumGenerator {
 public:
  EnumGenerator(const google::protobuf::EnumDescriptor* e) : enum_(e) {}

  void GenerateHeader(std::ostream& os);
  // py::enum_ registration. The surrounding function has a using-directive for
  // this enum's namespace, so the name here is the unqualified C++ name.
  void GeneratePython(std::ostream& os);

 private:
  friend class MessageGenerator;
  const google::protobuf::EnumDescriptor* enum_;
  std::vector<std::unique_ptr<EnumGenerator>> nested_enum_gens_;
};

}  // namespace phaser
