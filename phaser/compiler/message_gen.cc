#include "phaser/compiler/message_gen.h"

#include <algorithm>

namespace phaser {

bool IsCppReservedWord(const std::string &s) {
  static absl::flat_hash_set<std::string> reserved_words = {
      "alignas",
      "alignof",
      "and",
      "and_eq",
      "asm",
      "atomic_cancel",
      "atomic_commit",
      "atomic_noexcept",
      "auto",
      "bitand",
      "bitor",
      "bool",
      "break",
      "case",
      "catch",
      "char",
      "char8_t",
      "char16_t",
      "char32_t",
      "class",
      "compl",
      "concept",
      "const",
      "consteval",
      "constexpr",
      "constinit",
      "const_cast",
      "continue",
      "co_await",
      "co_return",
      "co_yield",
      "decltype",
      "default",
      "delete",
      "do",
      "double",
      "dynamic_cast",
      "else",
      "enum",
      "explicit",
      "export",
      "extern",
      "false",
      "float",
      "for",
      "friend",
      "goto",
      "if",
      "inline",
      "int",
      "long",
      "mutable",
      "namespace",
      "new",
      "noexcept",
      "not",
      "not_eq",
      "nullptr",
      "operator",
      "or",
      "or_eq",
      "private",
      "protected",
      "public",
      "reflexpr",
      "register",
      "reinterpret_cast",
      "requires",
      "return",
      "short",
      "signed",
      "sizeof",
      "static",
      "static_assert",
      "static_cast",
      "struct",
      "switch",
      "synchronized",
      "template",
      "this",
      "thread_local",
      "throw",
      "true",
      "try",
      "typedef",
      "typeid",
      "typename",
      "union",
      "unsigned",
      "using",
      "virtual",
      "void",
      "volatile",
      "wchar_t",
      "while",
      "xor",
      "xor_eq",
  };
  return reserved_words.contains(s);
}

static std::string EnumName(const google::protobuf::EnumDescriptor *desc) {
  std::string name = desc->name();
  if (desc->containing_type() != nullptr) {
    name = desc->containing_type()->name() + "_" + name;
  }
  return name;
}

static std::string MessageName(const google::protobuf::Descriptor *desc) {
  std::string name = desc->name();
  if (desc->containing_type() != nullptr) {
    name = desc->containing_type()->name() + "_" + name;
  }
  return name;
}

static std::string
FieldCFieldType(const google::protobuf::FieldDescriptor *field) {
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
    return "EnumField<" + EnumName(field->enum_type()) + ">";
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    return "StringField";
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    return "IndirectMessageField<" + MessageName(field->message_type()) + ">";
  default:
    abort();
  }
}

static std::string FieldCType(const google::protobuf::FieldDescriptor *field) {
  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    return "int32_t";
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    return "int64_t";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    return "uint32_t";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    return "uint64_t";
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    return "double";
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    return "float";
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    return "bool";
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    return  EnumName(field->enum_type());
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    return "std::string_view";
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    return MessageName(field->message_type());
  default:
    abort();
  }
}
static std::string
FieldRepeatedCType(const google::protobuf::FieldDescriptor *field) {
  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    return "PrimitiveVectorField<int32_t>";
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    return "PrimitiveVectorField<int64_t>";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    return "PrimitiveVectorField<uint32_t>";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    return "PrimitiveVectorField<uint64_t>";
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    return "PrimitiveVectorField<double>";
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    return "PrimitiveVectorField<float>";
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    return "PrimitiveVectorField<bool>";
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    return "EnumVectorField<" + EnumName(field->enum_type()) + ">";
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    return "StringVectorField";
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    return "MessageVectorField<" + MessageName(field->message_type()) + ">";
  default:
    abort();
  }
}

static std::string
FieldUnionCType(const google::protobuf::FieldDescriptor *field) {
  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    return "UnionInt32Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    return "UnionInt64Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    return "UnionUint32Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    return "UnionUint64Field";
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    return "UnionDoubleField";
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    return "UnionFloatField";
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    return "UnionBoolField";
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    return "UnionEnumField<" + EnumName(field->enum_type()) + ">";
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    return "UnionStringField";
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    return "UnionMessageField<" + MessageName(field->message_type()) + ">";
  default:
    abort();
  }
}

static uint32_t
FieldBinarySize(const google::protobuf::FieldDescriptor *field) {
  switch (field->cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    return 4;
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    return 8;
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    return 4;
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    return 8;
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    return 8;
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    return 4;
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    return 1;
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    return 4;
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    return 4;
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    return 8;
  default:
    abort();
  }
}

void MessageGenerator::CompileUnions() {
  for (int i = 0; i < message_->field_count(); i++) {
    const auto &field = message_->field(i);
    const google::protobuf::OneofDescriptor *oneof = field->containing_oneof();
    if (oneof == nullptr) {
      continue;
    }
    auto it = unions_.find(oneof);
    std::shared_ptr<UnionInfo> union_info;
    if (it == unions_.end()) {
      union_info = std::make_shared<UnionInfo>(oneof, 4, oneof->name() + "_",
                                               "UnionField");
      unions_[oneof] = union_info;
    } else {
      union_info = it->second;
    }
    // Append field to the members of the union.
    std::string field_type = FieldUnionCType(field);
    // Append union type to the end of the the union member type
    if (union_info->member_type == "UnionField") {
      union_info->member_type += "<phaser::" + field_type;
    } else {
      union_info->member_type += ",phaser::" + field_type;
    }
    uint32_t field_size = FieldBinarySize(field);
    union_info->members.push_back(
        std::make_shared<FieldInfo>(field, 4, union_info->id, "", field_type,
                                    FieldCType(field), field_size));
    union_info->binary_size = std::max(union_info->binary_size, 4 + field_size);
    union_info->id++;
  }
  for (auto & [ oneof, union_info ] : unions_) {
    union_info->member_type += ">";
  }
}

void MessageGenerator::CompileFields() {
  uint32_t offset = 0;
  uint32_t id = 0;
  fields_.reserve(message_->field_count());
  for (int i = 0; i < message_->field_count(); i++) {
    const auto &field = message_->field(i);
    std::string field_type;
    const google::protobuf::OneofDescriptor *oneof = field->containing_oneof();
    int32_t field_size;
    uint32_t next_id = id;
    if (oneof != nullptr) {
      // Oneof fields are handled separately.
      continue;
    } else if (field->is_repeated()) {
      field_type = FieldRepeatedCType(field);
      field_size = 8;
    } else {
      field_type = FieldCFieldType(field);
      field_size = FieldBinarySize(field);
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
          field->type() != google::protobuf::FieldDescriptor::TYPE_STRING) {
        // Strings and messages don't consume a presence bit.
        next_id++;
      } else {
        id = 0;
      }
    }
    offset = (offset + (field_size - 1)) & ~(field_size - 1);
    fields_.push_back(
        std::make_shared<FieldInfo>(field, offset, id, field->name() + "_",
                                    field_type, FieldCType(field), field_size));
    offset += field_size;
    id = next_id;
  }
}

void MessageGenerator::FinalizeOffsetsAndSizes() {
  uint32_t size = 4;
  // Find the max field id.  This will determine the number of 32-bit words we
  // need for the presence mask.
  int32_t max_id = -1;
  for (auto &field : fields_) {
    max_id = std::max(max_id, int32_t(field->id));
  }
  presence_mask_size_ = max_id == -1 ? 0 : ((max_id >> 5) + 1) * 4;
  size += presence_mask_size_;

  // Finalize the offsets in the fields vector now that we know the header size.
  for (auto &field : fields_) {
    field->offset += size;
  }

  // Set the offsets for the unions.
  uint32_t offset =
      fields_.empty() ? size
                      : (fields_.back()->offset + fields_.back()->binary_size);
  // Align offset to 4 bytes.
  offset = (offset + 3) & ~3;

  // Add the offset to the unions.
  for (auto & [ oneof, u ] : unions_) {
    u->offset = offset;
    for (auto &field : u->members) {
      field->offset += offset;
    }
    offset += u->binary_size;
    size += u->binary_size;
  }
  binary_size_ = size;
}

void MessageGenerator::GenerateHeader(std::ostream &os) {
  for (const auto &nested : nested_message_gens_) {
     nested->GenerateHeader(os);
  }
  CompileFields();
  CompileUnions();
  FinalizeOffsetsAndSizes();

  os << "class " << MessageName(message_) << " : public phaser::Message {\n";
  os << "public:\n";
  // Generate constructors.
  GenerateConstructors(os);
  // Generate size functions.
  GenerateSizeFunctions(os); 
  // Generate creators.
  GenerateCreators(os); 
  // Generate clear function.
  GenerateClear(os);
  // Generate field metadata.
  GenerateFieldMetadata(os);

  os << "  static std::string GetName() { return \"" << message_->full_name()
     << "\"; }\n\n";

  // Generate protobuf accessors.
  GenerateProtobufAccessors(os);
  os << "private:\n";
  GenerateFieldDeclarations(os);
  os << "};\n";
}

void MessageGenerator::GenerateSource(std::ostream &os) {
  for (const auto &nested : nested_message_gens_) {
    nested->GenerateSource(os);
  }
}

void MessageGenerator::GenerateFieldDeclarations(std::ostream &os) {
  for (auto &field : fields_) {
    os << "  phaser::" << field->member_type << " " << field->member_name
       << ";\n";
  }
  for (auto & [ oneof, u ] : unions_) {
    os << "  phaser::" << u->member_type << " " << u->member_name << ";\n";
  }
}

void MessageGenerator::GenerateEnums(std::ostream &os) {
  // Nested enums.
  for (auto& msg : nested_message_gens_) {
    msg->GenerateEnums(os);
  }
  for (auto &enum_gen : enum_gens_) {
    enum_gen->GenerateHeader(os);
  }
}

void MessageGenerator::GenerateConstructors(std::ostream &os) {
  // Generate default constructor.
  GenerateDefaultConstructor(os); 
  // Generate main constructor.
  GenerateMainConstructor(os);
}

void MessageGenerator::GenerateDefaultConstructor(std::ostream &os) {
  os << "  " << MessageName(message_) << "()\n";
  // Generate field initializers.
  GenerateFieldInitializers(os); 
  os << "  {}\n";
}

void MessageGenerator::GenerateMainConstructor(std::ostream &os) {
  os << "  " << MessageName(message_) << "(";
  os << "std::shared_ptr<phaser::MessageRuntime> runtime, phaser::BufferOffset "
        "offset) : Message(runtime, offset)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os, ", ");
  os << "  {}\n";

}

void MessageGenerator::GenerateFieldInitializers(std::ostream &os,
                                                         const char *sep) {
  if (fields_.empty() && unions_.empty()) {
    return;
  }

  for (auto &field : fields_) {
    os << sep << field->member_name << "(offsetof(" << MessageName(message_) << ", "
       << field->member_name << "), " << field->offset << ", " << field->id
       << ", " << field->field->number() << ")\n";
    sep = ", ";
  }
  for (auto & [ oneof, u ] : unions_) {
    os << sep << u->member_name << "(offsetof(" << MessageName(message_) << ", "
       << u->member_name << "), " << u->offset << ", 0, 0, {";
    const char *num_sep = "";
    for (auto &field : u->members) {
      os << num_sep << field->field->number();
      num_sep = ",";
    }
    os << "})\n";
    sep = ", ";
  }
}

void MessageGenerator::GenerateCreators(std::ostream &os) {
  os << "  static " << MessageName(message_)
     << " CreateMutable(void *addr, size_t size) {\n"
        "    phaser::PayloadBuffer *pb = new (addr) "
        "phaser::PayloadBuffer(size);\n"
        "    phaser::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "    auto runtime = "
        "std::make_shared<phaser::MutableMessageRuntime>(pb);\n"
        "    auto msg = "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "    msg.InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "    return msg;\n"
        "  }\n"
        "\n"
        "  static "
     << MessageName(message_)
     << " CreateReadonly(void *addr) {\n"
        "    phaser::PayloadBuffer *pb = "
        "reinterpret_cast<phaser::PayloadBuffer "
        "*>(addr);\n"
        "    auto runtime = std::make_shared<phaser::MessageRuntime>(pb);\n"
        "    return "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "  }\n";
}

void MessageGenerator::GenerateSizeFunctions(std::ostream &os) {
  os << "  static constexpr size_t BinarySize() { return HeaderSize() + "
     << binary_size_ << "; }\n";
  os << "  static constexpr size_t PresenceMaskSize() { return "
     << presence_mask_size_ << "; }\n";
  os << "  static constexpr uint32_t HeaderSize() { return 4 + "
        "PresenceMaskSize(); }\n";
}

void MessageGenerator::GenerateFieldMetadata(std::ostream &os) {
  // Build a vector of fields from the fields an unions, sorted by field number.
  std::vector<std::shared_ptr<FieldInfo>> all_fields;
  for (auto &field : fields_) {
    all_fields.push_back(field);
  }
  for (auto & [ oneof, u ] : unions_) {
    for (auto &field : u->members) {
      all_fields.push_back(field);
    }
  }

  std::sort(all_fields.begin(), all_fields.end(),
            [](const auto &a, const auto &b) {
              return a->field->number() < b->field->number();
            });

  os << "  struct " << MessageName(message_) << "FieldData {";
  os << R"(
    uint32_t num;
    struct Field {
      uint32_t number;
      uint32_t offset : 24;
      uint32_t id : 8;
)";
  os << "    } fields[" << all_fields.size() << "];\n";
  os << "  };\n";

  // Generate the field data.
  os << "  static constexpr " << MessageName(message_)
     << "FieldData field_data = {\n";
  os << "    .num = " << all_fields.size() << ",\n";
  os << "    .fields = {\n";
  for (auto &field : all_fields) {
    os << "      { .number = " << field->field->number()
       << ", .offset = " << field->offset << ", .id = " << field->id << " },\n";
  }
  os << "  };\n";
}

void MessageGenerator::GenerateClear(std::ostream &os) {
  os << "  void Clear() {\n";
  for (auto &field : fields_) {
    os << "    " << field->member_name << ".Clear();\n";
  }
  for (auto & [ oneof, u ] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      os << "    " << u->member_name << ".Clear<" << i << ">();\n";
    }
  }
  os << "  }\n";
}

void MessageGenerator::GenerateProtobufAccessors(std::ostream &os) {
  // Generate field accessors.
   GenerateFieldProtobufAccessors(os); 
}

void MessageGenerator::GenerateFieldProtobufAccessors(std::ostream &os) {
  for (auto &field : fields_) {
    std::string field_name = field->field->name();
    std::string sanitized_field_name =
        field_name + +(IsCppReservedWord(field_name) ? "_" : "");

    os << "\n  // Field " << field_name << "\n";
    if (field->field->is_repeated()) {
      // Generate repeated accessor.
      switch (field->field->cpp_type()) {
      case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
      case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        os << "  " << field->c_type << " " << sanitized_field_name
           << "(size_t index) const {\n";
        os << "    return " << field->member_name << ".Get(index);\n";
        os << "  }\n";
        os << "  size_t " << field_name << "_size() const {\n";
        os << "    return " << field->member_name << ".Size();\n";
        os << "  }\n";
        os << "  void clear_" << field_name << "() {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";

        // Strings have different accessors from primitive fields.
        if (field->field->cpp_type() ==
            google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
          os << "  void add_" << field_name << "(const std::string& value) {\n";
          os << "    " << field->member_name << ".Add(value);\n";
          os << "  }\n";
          os << "  void set_" << field_name
             << "(size_t index, const std::string& value) {\n";
          os << "    " << field->member_name << ".Set(index, value);\n";
          os << "  }\n";
          os << "  phaser::StringVectorField& " << sanitized_field_name
             << "() {\n";
          os << "    return " << field->member_name << ";\n";
          os << "  }\n";
        } else {
          os << "  void add_" << field_name << "(" << field->c_type
             << " value) {\n";
          os << "    " << field->member_name << ".Add(value);\n";
          os << "  }\n";
          os << "  void set_" << field_name << "(size_t index, "
             << field->c_type << " value) {\n";
          os << "    " << field->member_name << ".Set(index, value);\n";
          os << "  }\n";
          os << "  phaser::PrimitiveVectorField<" << field->c_type << ">& "
             << sanitized_field_name << "() {\n";
          os << "    return " << field->member_name << ";\n";
          os << "  }\n";
        }
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
        os << "  size_t " << field_name << "_size() const {\n";
        os << "    return " << field->member_name << ".Size();\n";
        os << "  }\n";
        os << "  void clear_" << field_name << "() {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
        os << "  " << field->c_type << " " << sanitized_field_name
           << "(size_t index) const {\n";
        os << "    return " << field->member_name << ".Get(index);\n";
        os << "  }\n";
        os << "  " << field->c_type << "* mutable_" << field_name
           << "(size_t index) {\n";
        os << "    return " << field->member_name << ".Mutable(index);\n";
        os << "  }\n";
        os << "  add_" << field_name << "(" << field->c_type << " value) {\n";
        os << "    " << field->member_name << ".Add(value);\n";
        os << "  }\n";
        os << "  phaser::MessageVectorField<" << field->c_type << ">& "
           << sanitized_field_name << "() {\n";
        os << "    return " << field->member_name << ";\n";
        os << "  }\n";
        break;
      }
    } else {
      // Look at the field type and generate the appropriate accessor.
      switch (field->field->cpp_type()) {
      case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
      case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        os << "  " << field->c_type << " " << sanitized_field_name
           << "() const {\n";
        os << "    return " << field->member_name << ".Get();\n";
        os << "  }\n";
        os << "  bool has_" << field_name << "() const {\n";
        os << "    return " << field->member_name << ".IsPresent();\n";
        os << "  }\n";
        os << "  void clear_" << field_name << "() {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
        if (field->field->cpp_type() ==
            google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
          os << "  void set_" << field_name << "(const std::string& value) {\n";
          os << "    " << field->member_name << ".Set(value);\n";
          os << "  }\n";
        } else {
          os << "  void set_" << field_name << "(" << field->c_type
             << " value) {\n";
          os << "    " << field->member_name << ".Set(value);\n";
          os << "  }\n";
        }
        break;

      case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
        os << "  bool has_" << field_name << "() const {\n";
        os << "    return " << field->member_name << ".IsPresent();\n";
        os << "  }\n";
        os << "  void clear_" << field_name << "() {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
        os << "  const " << field->c_type << "& " << sanitized_field_name
           << "() const {\n";
        os << "    return " << field->member_name << ".Get();\n";
        os << "  }\n";
        os << "  " << field->c_type << "* mutable_" << field_name << "() {\n";
        os << "    return " << field->member_name << ".Mutable();\n";
        os << "  }\n";
        break;
      default:
        break;
      }
    }
  }
}

void MessageGenerator::GenerateUnionProtobufAccessors(std::ostream &os) {
}

} // namespace phaser
