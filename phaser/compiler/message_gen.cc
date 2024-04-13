#include "phaser/compiler/message_gen.h"

namespace phaser {

static std::string FieldCType(const google::protobuf::FieldDescriptor *field) {
  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    return "Int32Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    return "Int64Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    return "Uint32Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    return "Uint64Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    return "DoubleField";
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    return "FloatField";
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    return "BoolField";
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    return "EnumField<" + field->enum_type()->name() + ">";
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    return "StringField";
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    return "MessageField<" + field->message_type()->name() + ">";
  default:
    abort();
  }
}

absl::Status MessageGenerator::GenerateHeader(std::ostream &os) {
  for (const auto &nested : nested_message_gens_) {
    if (absl::Status status = nested->GenerateHeader(os); !status.ok()) {
      return status;
    }
  }
  os << "class " << message_->name() << " {\n";
  os << "public:\n";
  os << "  " << message_->name() << "() = default;\n";
  os << "  ~" << message_->name() << "() = default;\n";
  if (absl::Status status = GenerateFieldDeclarations(os); !status.ok()) {
    return status;
  }
  os << "};\n";
  return absl::OkStatus();
}

absl::Status MessageGenerator::GenerateSource(std::ostream &os) {
  for (const auto &nested : nested_message_gens_) {
    if (absl::Status status = nested->GenerateSource(os); !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status MessageGenerator::GenerateFieldDeclarations(std::ostream &os) {
  for (int i = 0; i < message_->field_count(); i++) {
    const auto &field = message_->field(i);
    os << "  " << FieldCType(field) << " " << field->name() << ";\n";
  }
  return absl::OkStatus();
}

absl::Status MessageGenerator::GenerateFieldDefinitions(std::ostream &os) {
  return absl::OkStatus();
}

} // namespace phaser
