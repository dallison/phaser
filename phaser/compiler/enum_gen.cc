// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/enum_gen.h"

#include <algorithm>
#include <set>

namespace phaser {

void EnumGenerator::GenerateHeader(std::ostream& os) {
  std::string name(enum_->name());
  if (enum_->containing_type() != nullptr) {
    name = std::string(enum_->containing_type()->name()) + "_" + name;
  }
  os << "enum " << name << " : int {\n";
  for (int i = 0; i < enum_->value_count(); i++) {
    const google::protobuf::EnumValueDescriptor* value = enum_->value(i);
    std::string const_name(value->name());
    if (enum_->containing_type() != nullptr) {
      const_name = name + "_" + const_name;
    }
    os << "  " << const_name << " = " << value->number() << ",\n";
  }
  os << "};\n\n";

  // Stringizer. With `option allow_alias`, several names share one number, so
  // only the first is given a case label; a switch cannot repeat one. That
  // matches protobuf's own _Name, which also reports the first name declared
  // for a number.
  os << "struct " << name << "Stringizer {\n";
  os << "  std::string operator()(" << name << " e) {\n";
  os << "    switch (e) {\n";
  std::set<int> stringized_numbers;
  for (int i = 0; i < enum_->value_count(); i++) {
    const google::protobuf::EnumValueDescriptor* value = enum_->value(i);
    if (!stringized_numbers.insert(value->number()).second) {
      continue;
    }
    std::string const_name(value->name());
    if (enum_->containing_type() != nullptr) {
      const_name = name + "_" + const_name;
    }
    os << "    case " << const_name << ": return \"" << value->name()
       << "\";\n";
  }
  os << "    }\n";
  os << "    return \"\";\n";
  os << "  }\n";
  os << "};\n\n";

  // Parser
  os << "struct " << name << "Parser {\n";
  os << "   " << name << " operator()(const std::string &s) {\n";
  for (int i = 0; i < enum_->value_count(); i++) {
    const google::protobuf::EnumValueDescriptor* value = enum_->value(i);
    std::string const_name(value->name());
    if (enum_->containing_type() != nullptr) {
      const_name = name + "_" + const_name;
    }
    os << "    if (s == \"" << const_name << "\") return " << const_name
       << ";\n";
  }
  os << "    return static_cast<" << name << ">(0);\n";
  os << "  }\n";
  os << "};\n\n";

  // Protobuf compatible _Name and _Parse functions.
  os << "template <typename EnumOrInt>\n";
  os << "inline std::string " << name << "_Name(EnumOrInt e) {\n";
  os << "  " << name << "Stringizer s;\n";
  os << "  return s(static_cast<" << name << ">(e));\n";
  os << "}\n\n";

  os << "inline void " << name << "_Parse(const std::string &s, " << name
     << "* value) {\n";
  os << "  " << name << "Parser p;\n";
  os << "  *value = p(s);\n";
  os << "}\n\n";
}

}  // namespace phaser
