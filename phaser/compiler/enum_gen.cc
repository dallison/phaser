#include "phaser/compiler/enum_gen.h"

#include <algorithm>

namespace phaser {

  void EnumGenerator::GenerateHeader(std::ostream &os) {
    std::string name = enum_->name();
    if (enum_->containing_type() != nullptr) {
      name = enum_->containing_type()->name() + "_" + name;
    }
    os << "enum " << name << " : int {\n";
    for (int i = 0; i < enum_->value_count(); i++) {
      const google::protobuf::EnumValueDescriptor *value = enum_->value(i);
      os << "  " << value->name() << " = " << value->number() << ",\n";
    }
    os << "};\n";
  }

}
