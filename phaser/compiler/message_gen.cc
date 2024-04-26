#include "phaser/compiler/message_gen.h"
#include "absl/strings/str_format.h"
#include <algorithm>
#include <ctype.h>

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
  switch (field->type()) {
  case google::protobuf::FieldDescriptor::TYPE_INT32:
    return "Int32Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_SINT32:
    return "Int32Field<false, true>";
  case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    return "Int32Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_INT64:
    return "Int64Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_SINT64:
    return "Int64Field<false, true>";
  case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    return "Int64Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_UINT32:
    return "Uint32Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    return "Uint32Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_UINT64:
    return "Uint64Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    return "Uint64Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    return "DoubleField<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    return "FloatField<true, false";
  case google::protobuf::FieldDescriptor::TYPE_BOOL:
    return "BoolField<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_ENUM:
    return "EnumField<" + EnumName(field->enum_type()) + ">";
  case google::protobuf::FieldDescriptor::TYPE_STRING:
  case google::protobuf::FieldDescriptor::TYPE_BYTES:
    return "StringField";
  case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    return "IndirectMessageField<" + MessageName(field->message_type()) + ">";

  case google::protobuf::FieldDescriptor::TYPE_GROUP:
    std::cerr << "Groups are not supported\n";
    exit(1);
  }
}

static std::string FieldCType(const google::protobuf::FieldDescriptor *field) {
  switch (field->type()) {
  case google::protobuf::FieldDescriptor::TYPE_INT32:
  case google::protobuf::FieldDescriptor::TYPE_SINT32:
  case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    return "int32_t";
  case google::protobuf::FieldDescriptor::TYPE_INT64:
  case google::protobuf::FieldDescriptor::TYPE_SINT64:
  case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    return "int64_t";
  case google::protobuf::FieldDescriptor::TYPE_UINT32:
  case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    return "uint32_t";
  case google::protobuf::FieldDescriptor::TYPE_UINT64:
  case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    return "uint64_t";
  case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    return "double";
  case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    return "float";
  case google::protobuf::FieldDescriptor::TYPE_BOOL:
    return "bool";
  case google::protobuf::FieldDescriptor::TYPE_ENUM:
    return EnumName(field->enum_type());
  case google::protobuf::FieldDescriptor::TYPE_STRING:
  case google::protobuf::FieldDescriptor::TYPE_BYTES:
    return "std::string_view";
  case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    return MessageName(field->message_type());
  case google::protobuf::FieldDescriptor::TYPE_GROUP:
    std::cerr << "Groups are not supported\n";
    exit(1);
  }
}

static std::string
FieldRepeatedCType(const google::protobuf::FieldDescriptor *field) {
  std::string packed = field->is_packed() ? ", true>" : ", false>";
  switch (field->type()) {
  case google::protobuf::FieldDescriptor::TYPE_INT32:
    return "PrimitiveVectorField<int32_t, false, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_SINT32:
    return "PrimitiveVectorField<int32_t, false, true" + packed;
  case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    return "PrimitiveVectorField<int32_t, true, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_INT64:
    return "PrimitiveVectorField<int64_t, false, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_SINT64:
    return "PrimitiveVectorField<int64_t, false, true" + packed;
  case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    return "PrimitiveVectorField<int64_t, true, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_UINT32:
    return "PrimitiveVectorField<uint32_t, false, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    return "PrimitiveVectorField<uint32_t, true, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_UINT64:
    return "PrimitiveVectorField<uint64_t, false, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    return "PrimitiveVectorField<uint64_t, true, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    return "PrimitiveVectorField<double, true, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    return "PrimitiveVectorField<float, true, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_BOOL:
    return "PrimitiveVectorField<bool, false, false" + packed;
  case google::protobuf::FieldDescriptor::TYPE_ENUM:
    return "EnumVectorField<" + EnumName(field->enum_type()) + packed;
  case google::protobuf::FieldDescriptor::TYPE_STRING:
  case google::protobuf::FieldDescriptor::TYPE_BYTES:
    return "StringVectorField";
  case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    return "MessageVectorField<" + MessageName(field->message_type()) + ">";
  case google::protobuf::FieldDescriptor::TYPE_GROUP:
    std::cerr << "Groups are not supported\n";
    exit(1);
  }
}

static std::string
FieldUnionCType(const google::protobuf::FieldDescriptor *field) {
  switch (field->type()) {
  case google::protobuf::FieldDescriptor::TYPE_INT32:
    return "UnionInt32Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_SINT32:
    return "UnionInt32Field<false, true>";
  case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    return "UnionInt32Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_INT64:
    return "UnionInt64Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_SINT64:
    return "UnionInt64Field<false, true>";
  case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    return "UnionInt64Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_UINT32:
    return "UnionUint32Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    return "UnionUint32Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_UINT64:
    return "UnionUint64Field<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    return "UnionUint64Field<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    return "UnionDoubleField<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    return "UnionFloatField<true, false>";
  case google::protobuf::FieldDescriptor::TYPE_BOOL:
    return "UnionBoolField<false, false>";
  case google::protobuf::FieldDescriptor::TYPE_ENUM:
    return "UnionEnumField<" + EnumName(field->enum_type()) + ">";
  case google::protobuf::FieldDescriptor::TYPE_STRING:
  case google::protobuf::FieldDescriptor::TYPE_BYTES:
    return "UnionStringField";
  case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    return "UnionMessageField<" + MessageName(field->message_type()) + ">";
  case google::protobuf::FieldDescriptor::TYPE_GROUP:
    std::cerr << "Groups are not supported\n";
    exit(1);
  }
}

static uint32_t
FieldBinarySize(const google::protobuf::FieldDescriptor *field) {
  switch (field->type()) {
  case google::protobuf::FieldDescriptor::TYPE_INT32:
  case google::protobuf::FieldDescriptor::TYPE_SINT32:
  case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    return 4;
  case google::protobuf::FieldDescriptor::TYPE_INT64:
  case google::protobuf::FieldDescriptor::TYPE_SINT64:
  case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    return 8;
  case google::protobuf::FieldDescriptor::TYPE_UINT32:
  case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    return 4;
  case google::protobuf::FieldDescriptor::TYPE_UINT64:
  case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    return 8;
  case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    return 8;
  case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    return 4;
  case google::protobuf::FieldDescriptor::TYPE_BOOL:
    return 1;
  case google::protobuf::FieldDescriptor::TYPE_ENUM:
    return 4;
  case google::protobuf::FieldDescriptor::TYPE_STRING:
  case google::protobuf::FieldDescriptor::TYPE_BYTES:
    return 4;
  case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    return 8;
  case google::protobuf::FieldDescriptor::TYPE_GROUP:
    std::cerr << "Groups are not supported\n";
    exit(1);
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
      union_info->member_type += "<";
    } else {
      union_info->member_type += ", ";
    }
    union_info->member_type += "phaser::" + field_type;
    uint32_t field_size = FieldBinarySize(field);
    union_info->members.push_back(std::make_shared<FieldInfo>(
        field, 4, union_info->id, field->name() + "_", field_type,
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
          field->type() != google::protobuf::FieldDescriptor::TYPE_STRING &&
          field->type() != google::protobuf::FieldDescriptor::TYPE_BYTES) {
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
  GenerateConstructors(os, true);
  // Generate size functions.
  GenerateSizeFunctions(os);
  // Generate creators.
  GenerateCreators(os, true);
  // Generate clear function.
  GenerateClear(os, true);
  // Generate field metadata.
  GenerateFieldMetadata(os);

  os << "  static std::string GetFullName() { return \""
     << message_->full_name() << "\"; }\n";
  os << "  static std::string GetName() { return \"" << message_->name()
     << "\"; }\n\n";

  GenerateNestedTypes(os);
  GenerateFieldNumbers(os);

  // Generate protobuf accessors.
  GenerateProtobufAccessors(os);

  GenerateProtobufSerialization(os);

  // Generate serialized size.
  GenerateSerializedSize(os, true);
  // Generate serializer.
  GenerateSerializer(os, true);
  // Generate deserializer.
  GenerateDeserializer(os, true);

  os << "private:\n";
  GenerateFieldDeclarations(os);
  os << "};\n";
}

void MessageGenerator::GenerateSource(std::ostream &os) {
  for (const auto &nested : nested_message_gens_) {
    nested->GenerateSource(os);
  }

  GenerateConstructors(os, false);

  // Generate creators.
  GenerateCreators(os, false);
  // Generate clear function.
  GenerateClear(os, false);

  // Generate serialized size.
  GenerateSerializedSize(os, false);
  // Generate serializer.
  GenerateSerializer(os, false);
  // Generate deserializer.
  GenerateDeserializer(os, false);
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
  for (auto &msg : nested_message_gens_) {
    msg->GenerateEnums(os);
  }
  for (auto &enum_gen : enum_gens_) {
    enum_gen->GenerateHeader(os);
  }
}

void MessageGenerator::GenerateConstructors(std::ostream &os, bool decl) {
  // Generate default constructor.
  GenerateDefaultConstructor(os, decl);
  GenerateInternalDefaultConstructor(os, decl);
  // Generate main constructor.
  GenerateMainConstructor(os, decl);
}

void MessageGenerator::GenerateDefaultConstructor(std::ostream &os, bool decl) {
  if (decl) {
    os << "  " << MessageName(message_) << "(size_t initial_size = 1024);\n";
    return;
  }
  os << MessageName(message_) << "::" << MessageName(message_)
     << "(size_t initial_size)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os);
  os << R"XXX({
  InitDynamicMutable(initial_size);
}
)XXX";
}

void MessageGenerator::GenerateInternalDefaultConstructor(std::ostream &os,
                                                          bool decl) {
  if (decl) {
    os << "  " << MessageName(message_) << "(phaser::InternalDefault d);\n";
    return;
  }
  os << MessageName(message_) << "::" << MessageName(message_)
     << "(phaser::InternalDefault d)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os);
  os << "{}\n";
}

void MessageGenerator::GenerateMainConstructor(std::ostream &os, bool decl) {
  if (decl) {
    os << "  " << MessageName(message_)
       << "(std::shared_ptr<phaser::MessageRuntime> runtime, "
          "toolbelt::BufferOffset "
          "offset);\n";
    return;
  }
  os << MessageName(message_) << "::" << MessageName(message_) << "(";
  os << "std::shared_ptr<phaser::MessageRuntime> runtime, "
        "toolbelt::BufferOffset "
        "offset) : Message(runtime, offset)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os, ", ");
  os << "{}\n";
}

void MessageGenerator::GenerateFieldInitializers(std::ostream &os,
                                                 const char *sep) {
  if (fields_.empty() && unions_.empty()) {
    return;
  }
  os << "#pragma clang diagnostic push\n";
  os << "#pragma clang diagnostic ignored \"-Winvalid-offsetof\"\n";
  for (auto &field : fields_) {
    os << sep << field->member_name << "(offsetof(" << MessageName(message_)
       << ", " << field->member_name << "), " << field->offset << ", "
       << field->id << ", " << field->field->number() << ")\n";
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
  os << "#pragma clang diagnostic pop\n\n";
}

void MessageGenerator::GenerateCreators(std::ostream &os, bool decl) {
  if (decl) {
    os << "  static " << MessageName(message_)
       << " CreateMutable(void *addr, size_t size);\n";
    os << "  static " << MessageName(message_)
       << " CreateReadonly(void *addr);\n";
    os << "  static " << MessageName(message_)
       << " CreateDynamicMutable(size_t initial_size);\n";
    os << "  void InitDynamicMutable(size_t initial_size);\n";
    return;
  }
  os << MessageName(message_) << " " << MessageName(message_)
     << "::CreateMutable(void *addr, size_t size) {\n"
        "  toolbelt::PayloadBuffer *pb = new (addr) "
        "toolbelt::PayloadBuffer(size);\n"
        "  toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "  auto runtime = "
        "std::make_shared<phaser::MutableMessageRuntime>(pb);\n"
        "  auto msg = "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "  msg.InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "  return msg;\n"
        "}\n"
        "\n"
     << MessageName(message_) << " " << MessageName(message_)
     << "::CreateReadonly(void *addr) {\n"
        "  toolbelt::PayloadBuffer *pb = "
        "reinterpret_cast<toolbelt::PayloadBuffer "
        "*>(addr);\n"
        "  auto runtime = std::make_shared<phaser::MessageRuntime>(pb);\n"
        "  return "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "}\n";
  os << "  " << MessageName(message_) << " " << MessageName(message_)
     << "::CreateDynamicMutable(size_t initial_size = 1024) {\n"
        "  toolbelt::PayloadBuffer *pb = "
        "phaser::NewDynamicBuffer(initial_size);\n"
        "  toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "  auto runtime = "
        "std::make_shared<phaser::DynamicMutableMessageRuntime>(pb);\n"
        "  auto msg = "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "  msg.InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "  return msg;\n"
        "}\n";

  os << "void " << MessageName(message_) << "::InitDynamicMutable(size_t initial_size = 1024) {\n"
        "  toolbelt::PayloadBuffer *pb = "
        "phaser::NewDynamicBuffer(initial_size);\n"
        "  toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "  auto runtime = "
        "std::make_shared<phaser::DynamicMutableMessageRuntime>(pb);\n"
        "  this->runtime = runtime;\n"
        "  this->absolute_binary_offset = pb->message;\n"
        "  this->InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "}\n";
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
  os << "    }\n";
  os << "  };\n";
}

void MessageGenerator::GenerateClear(std::ostream &os, bool decl) {
  if (decl) {
    os << "  void Clear();\n";
    return;
  }
  os << "void " << MessageName(message_) << "::Clear() {\n";
  for (auto &field : fields_) {
    os << "  " << field->member_name << ".Clear();\n";
  }
  for (auto & [ oneof, u ] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      os << "  " << u->member_name << ".Clear<" << i << ">();\n";
    }
  }
  os << "}\n\n";
}

void MessageGenerator::GenerateProtobufAccessors(std::ostream &os) {
  // Generate field accessors.
  GenerateFieldProtobufAccessors(os);
  // Union accessors.
  GenerateUnionProtobufAccessors(os);
}

void MessageGenerator::GenerateFieldProtobufAccessors(std::ostream &os) {
  for (auto &field : fields_) {
    GenerateFieldProtobufAccessors(field, nullptr, -1, os);
  }
}

void MessageGenerator::GenerateFieldProtobufAccessors(
    std::shared_ptr<FieldInfo> field, std::shared_ptr<UnionInfo> union_field,
    int union_index, std::ostream &os) {
  std::string field_name = field->field->name();
  std::string sanitized_field_name =
      field_name + +(IsCppReservedWord(field_name) ? "_" : "");

  std::string member_name = field->member_name;
  if (union_field != nullptr) {
    // For a union, all the accessors use the union field name.
    member_name = union_field->member_name;
  }
  std::string suffix = "";
  if (union_index != -1) {
    suffix += "<" + std::to_string(union_index) + ">";
  }

  std::string fixed_size_string =
      field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_SFIXED32 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_SFIXED64 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_FIXED32 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_FIXED64
          ? ", true"
          : ", false";
  std::string signed_string =
      field->field->type() == google::protobuf::FieldDescriptor::TYPE_SINT32 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_SINT64
          ? ", true"
          : ", false";
  std::string packed_string = field->field->is_packed() ? ", true" : ", false";

  os << "\n  // Field " << field_name << "\n";
  if (field->field->is_repeated()) {
    // Generate repeated accessor.
    switch (field->field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << "  " << field->c_type << " " << sanitized_field_name
         << "(size_t index) const {\n";
      os << "    return " << member_name << ".Get(index);\n";
      os << "  }\n";
      os << "  size_t " << field_name << "_size() const {\n";
      os << "    return " << member_name << ".Size();\n";
      os << "  }\n";
      os << "  void clear_" << field_name << "() {\n";
      os << "    " << member_name << ".Clear();\n";
      os << "  }\n";

      // Strings have different accessors from primitive fields.
      if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_STRING ||
          field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_BYTES) {
        os << "  void add_" << field_name << "(const std::string& value) {\n";
        os << "    " << member_name << ".Add(value);\n";
        os << "  }\n";
        os << "  void set_" << field_name
           << "(size_t index, const std::string& value) {\n";
        os << "    " << member_name << ".Set(index, value);\n";
        os << "  }\n";
        os << "  phaser::StringVectorField& " << sanitized_field_name
           << "() {\n";
        os << "    return " << member_name << ";\n";
        os << "  }\n";
      } else {
        os << "  void add_" << field_name << "(" << field->c_type
           << " value) {\n";
        os << "    " << member_name << ".Add(value);\n";
        os << "  }\n";
        os << "  void set_" << field_name << "(size_t index, " << field->c_type
           << " value) {\n";
        os << "    " << member_name << ".Set(index, value);\n";
        os << "  }\n";
        os << "  phaser::PrimitiveVectorField<" << field->c_type
           << fixed_size_string << signed_string << packed_string << ">& "
           << sanitized_field_name << "() {\n";
        os << "    return " << member_name << ";\n";
        os << "  }\n";
      }
      break;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      os << "  size_t " << field_name << "_size() const {\n";
      os << "    return " << member_name << ".Size();\n";
      os << "  }\n";
      os << "  void clear_" << field_name << "() {\n";
      os << "    " << member_name << ".Clear();\n";
      os << "  }\n";
      os << "  " << field->c_type << " " << sanitized_field_name
         << "(size_t index) const {\n";
      os << "    return " << member_name << ".Get(index);\n";
      os << "  }\n";
      os << "  " << field->c_type << "* mutable_" << field_name
         << "(size_t index) {\n";
      os << "    return " << member_name << ".Mutable(index);\n";
      os << "  }\n";
      os << "  add_" << field_name << "(" << field->c_type << " value) {\n";
      os << "    " << member_name << ".Add(value);\n";
      os << "  }\n";
      os << "  phaser::MessageVectorField<" << field->c_type << ">& "
         << sanitized_field_name << "() {\n";
      os << "    return " << member_name << ";\n";
      os << "  }\n";
      break;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
    }
  } else {
    // Look at the field type and generate the appropriate accessor.
    switch (field->field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << "  " << field->c_type << " " << sanitized_field_name
         << "() const {\n";
      if (union_index == -1) {
        os << "    return " << member_name << ".Get();\n";
      } else {
        os << "    return " << member_name << ".template GetValue<"
           << std::to_string(union_index) << ", " << field->c_type << ">();\n";
      }
      os << "  }\n";
      if (union_index == -1) {
        os << "  bool has_" << field_name << "() const {\n";
        os << "    return " << member_name << ".IsPresent();\n";
        os << "  }\n";
      }
      os << "  void clear_" << field_name << "() {\n";
      os << "    " << member_name << ".Clear" << suffix << "();\n";
      os << "  }\n";
      if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_STRING ||
          field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_BYTES) {
        os << "  void set_" << field_name << "(const std::string& value) {\n";
        os << "    " << member_name << ".Set" << suffix << "(value);\n";
        os << "  }\n";
      } else {
        os << "  void set_" << field_name << "(" << field->c_type
           << " value) {\n";
        os << "    " << member_name << ".Set" << suffix << "(value);\n";
        os << "  }\n";
      }
      break;

    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      os << "  void clear_" << field_name << "() {\n";
      os << "    " << member_name << ".Clear" << suffix << "();\n";
      os << "  }\n";

      if (union_index == -1) {
        os << "  const " << field->c_type << "& " << sanitized_field_name
           << "() const {\n";
        os << "    return " << member_name << ".Get();\n";
        os << "  }\n";
        os << "  bool has_" << field_name << "() const {\n";
        os << "    return " << member_name << ".IsPresent();\n";
        os << "  }\n";
        os << "  " << field->c_type << "* mutable_" << field_name << "() {\n";
        os << "    return " << member_name << ".Mutable();\n";
        os << "  }\n";
      } else {
        // Union members need to be accessed by index and type.
        os << "  const " << field->c_type << "& " << sanitized_field_name
           << "() const {\n";
        os << "    return " << member_name << ".template GetReference<"
           << std::to_string(union_index) << ", "
           << MessageName(field->field->message_type()) << ">();\n";
        os << "  }\n";
        os << "  " << field->c_type << "* mutable_" << field_name << "() {\n";
        os << "    return " << member_name << ".Mutable<"
           << std::to_string(union_index) << ", "
           << MessageName(field->field->message_type()) << ">();\n";
        os << "  }\n";
      }
      break;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
      break;
    }
  }
}

void MessageGenerator::GenerateUnionProtobufAccessors(std::ostream &os) {
  for (auto & [ oneof, u ] : unions_) {
    os << "\n  // Oneof " << oneof->name() << "\n";
    os << "  int " << oneof->name() << "_case() const {\n";
    os << "    return " << u->member_name << ".Discriminator();\n";
    os << "  }\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto &field = u->members[i];
      GenerateFieldProtobufAccessors(field, u, int(i), os);
    }
  }
}

void MessageGenerator::GenerateNestedTypes(std::ostream &os) {
  for (auto &msg : nested_message_gens_) {
    os << "  using " << msg->message_->name() << " = "
       << MessageName(msg->message_) << ";\n";
  }

  for (auto &enum_gen : enum_gens_) {
    os << "  using " << enum_gen->enum_->name() << " = "
       << EnumName(enum_gen->enum_) << ";\n";
    // Generate enum constant aliases.
    for (int i = 0; i < enum_gen->enum_->value_count(); i++) {
      const google::protobuf::EnumValueDescriptor *value =
          enum_gen->enum_->value(i);
      os << "  static constexpr " << enum_gen->enum_->name() << " "
         << value->name() << " = " << EnumName(enum_gen->enum_) << "_"
         << value->name() << ";\n";
    }
  }
}

void MessageGenerator::GenerateFieldNumbers(std::ostream &os) {
  for (auto &field : fields_) {
    std::string name = field->field->camelcase_name();
    name = absl::StrFormat("k%c%s", toupper(name[0]), name.substr(1));
    os << "  static constexpr int " << name
       << "FieldNumber = " << field->field->number() << ";\n";
  }
  for (auto & [ oneof, u ] : unions_) {
    for (auto &field : u->members) {
      std::string name = field->field->camelcase_name();
      name = absl::StrFormat("k%c%s", toupper(name[0]), name.substr(1));
      os << "  static constexpr int " << name
         << "FieldNumber = " << field->field->number() << ";\n";
    }
  }
}

void MessageGenerator::GenerateSerializedSize(std::ostream &os, bool decl) {
  if (decl) {
    os << "  size_t SerializedSize() const;\n";
    return;
  }
  os << "size_t " << MessageName(message_) << "::SerializedSize() const {\n";
  os << "  size_t size = 0;\n";
  for (auto &field : fields_) {
    if (field->field->is_repeated()) {
      os << "  size += " << field->member_name << ".SerializedSize();\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    size += " << field->member_name << ".SerializedSize();\n";
      os << "  }\n";
    }
  }
  for (auto & [ oneof, u ] : unions_) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto &field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      os << "    size += " << u->member_name << ".SerializedSize<" << i << ">("
         << field->field->number() << ");\n";
      os << "    break;\n";
    }
    os << "  }\n";
  }
  os << "  return size;\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateSerializer(std::ostream &os, bool decl) {
  if (decl) {
    os << "  absl::Status Serialize(phaser::ProtoBuffer &buffer) const;\n";
    return;
  }
  os << "absl::Status " << MessageName(message_)
     << "::Serialize(phaser::ProtoBuffer &buffer) const {\n";
  for (auto &field : fields_) {
    if (field->field->is_repeated()) {
      os << "  if (absl::Status status = " << field->member_name
         << ".Serialize(buffer); !status.ok()) return status;\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    if (absl::Status status = " << field->member_name
         << ".Serialize(buffer); !status.ok()) return status;\n";
      os << "  }\n";
    }
  }
  for (auto & [ oneof, u ] : unions_) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto &field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      os << "    if (absl::Status status = " << u->member_name << ".Serialize<"
         << i << ">(" << field->field->number()
         << ", buffer); !status.ok()) return status;\n";
      os << "    break;\n";
    }
    os << "  }\n";
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateDeserializer(std::ostream &os, bool decl) {
  if (decl) {
    os << "  absl::Status Deserialize(phaser::ProtoBuffer &buffer);\n";
    return;
  }
  os << "absl::Status " << MessageName(message_)
     << "::Deserialize(phaser::ProtoBuffer &buffer) {";
  os << R"XXX(
  while (!buffer.Eof()) {
    absl::StatusOr<uint32_t> tag =
        buffer.DeserializeVarint<uint32_t, false>();
    if (!tag.ok()) {
      return tag.status();
    }
    uint32_t field_number = *tag >> phaser::ProtoBuffer::kFieldIdShift;
    switch (field_number) {
)XXX";
  for (auto &field : fields_) {
    os << "    case " << field->field->number() << ":\n";
    os << "      if (absl::Status status = " << field->member_name
       << ".Deserialize(buffer); !status.ok()) return status;\n";
    os << "      break;\n";
  }
  for (auto & [ oneof, u ] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      auto &field = u->members[i];
      os << "    case " << field->field->number() << ":\n";
      os << "      if (absl::Status status = " << u->member_name
         << ".Deserialize<" << i << ">(" << field->field->number()
         << ", buffer); !status.ok()) return status;\n";
      os << "      break;\n";
    }
  }
  os << R"XXX(
    default:
      if (absl::Status status = buffer.SkipTag(*tag); !status.ok()) {
        return status;
      }
    }
  }
)XXX";
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateProtobufSerialization(std::ostream &os) {
  os << R"XXX(
  size_t ByteSizeLong() const {
    return SerializedSize();
  }

  int ByteSize() const {
    return static_cast<int>(ByteSizeLong());
  }

  bool SerializeToArray(char* array, size_t size) const {
    phaser::ProtoBuffer buffer(array, size);
    if (absl::Status status = Serialize(buffer); !status.ok()) return false;
    return true;
  }

  bool ParseFromArray(const char* array, size_t size) {
    phaser::ProtoBuffer buffer(array, size);
    if (absl::Status status = Deserialize(buffer); !status.ok()) return false;
    return true;
  }

  // String serialization.
  bool SerializeToString(std::string* str) const {
    size_t size = ByteSizeLong();
    str->resize(size);
    return SerializeToArray(&(*str)[0], size);
  }

  std::string SerializeAsString() const {
    std::string str;
    SerializeToString(&str);
    return str;
  }

  bool ParseFromString(const std::string& str) {
    return ParseFromArray(str.data(), str.size());
  }
)XXX";
}

} // namespace phaser
