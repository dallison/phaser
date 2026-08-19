// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/message_gen.h"

#include <ctype.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <set>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "phaser/options.pb.h"

namespace phaser {

static bool IsCppReservedWord(const std::string& s) {
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

static bool IsFixedWireType(const google::protobuf::FieldDescriptor* field) {
  using Field = google::protobuf::FieldDescriptor;
  switch (field->type()) {
    case Field::TYPE_FIXED32:
    case Field::TYPE_SFIXED32:
    case Field::TYPE_FLOAT:
    case Field::TYPE_FIXED64:
    case Field::TYPE_SFIXED64:
    case Field::TYPE_DOUBLE:
      return true;
    default:
      return false;
  }
}

static std::string RosBaseType(std::string type) {
  const size_t array = type.find('[');
  if (array != std::string::npos) {
    type.resize(array);
  }
  return type;
}

static bool IsRosBuiltinType(const std::string& type) {
  static const absl::flat_hash_set<std::string> builtin_types = {
      "bool",  "byte",   "char",   "duration", "float32", "float64",
      "int8",  "int16",  "int32",  "int64",    "string",  "time",
      "uint8", "uint16", "uint32", "uint64",
  };
  return builtin_types.contains(RosBaseType(type));
}

static std::string RosDataType(const google::protobuf::Descriptor* message) {
  if (message->options().HasExtension(phaser::ros_message)) {
    const auto& metadata = message->options().GetExtension(phaser::ros_message);
    if (!metadata.data_type().empty()) {
      return metadata.data_type();
    }
  }
  std::string package(message->file()->package());
  constexpr std::string_view suffix = ".proto";
  if (package.size() <= suffix.size() ||
      package.compare(package.size() - suffix.size(), suffix.size(), suffix) !=
          0) {
    return "";
  }
  package.resize(package.size() - suffix.size());
  return package + "/" + std::string(message->name());
}

static std::string RosArraySuffix(
    const google::protobuf::FieldDescriptor* field) {
  if (!field->is_repeated()) {
    return "";
  }
  if (field->options().HasExtension(phaser::array_size)) {
    return absl::StrFormat("[%u]",
                           field->options().GetExtension(phaser::array_size));
  }
  return "[]";
}

static std::string InferredRosFieldType(
    const google::protobuf::FieldDescriptor* field) {
  using Field = google::protobuf::FieldDescriptor;
  std::string type;
  switch (field->type()) {
    case Field::TYPE_BOOL:
      type = "bool";
      break;
    case Field::TYPE_INT32:
      type = "int32";
      break;
    case Field::TYPE_INT64:
      type = "int64";
      break;
    case Field::TYPE_UINT64:
      type = "uint64";
      break;
    case Field::TYPE_FLOAT:
      type = "float32";
      break;
    case Field::TYPE_DOUBLE:
      type = "float64";
      break;
    case Field::TYPE_STRING:
      type = "string";
      break;
    case Field::TYPE_MESSAGE:
      if (field->message_type()->full_name() == "google.protobuf.Timestamp") {
        type = "time";
      } else if (field->message_type()->full_name() ==
                 "google.protobuf.Duration") {
        type = "duration";
      } else {
        type = RosDataType(field->message_type());
      }
      break;
    default:
      // uint32, bytes, and enums each have multiple possible ROS source types.
      // Noncanonical signed encodings also require an explicit override.
      break;
  }
  if (type.empty()) {
    return "";
  }
  return type + RosArraySuffix(field);
}

static std::string RosFieldType(
    const google::protobuf::FieldDescriptor* field) {
  if (field->options().HasExtension(phaser::ros_field)) {
    const auto& metadata = field->options().GetExtension(phaser::ros_field);
    if (!metadata.type().empty()) {
      if (field->is_repeated() &&
          metadata.type().find('[') == std::string::npos) {
        return metadata.type() + RosArraySuffix(field);
      }
      return metadata.type();
    }
  }
  return InferredRosFieldType(field);
}

static std::string RosFieldName(
    const google::protobuf::FieldDescriptor* field) {
  if (field->options().HasExtension(phaser::ros_field)) {
    const auto& metadata = field->options().GetExtension(phaser::ros_field);
    if (!metadata.name().empty()) {
      return metadata.name();
    }
  }
  return std::string(field->name());
}

static std::vector<std::string> RosConstantDeclarations(
    const google::protobuf::Descriptor* message) {
  const auto& message_metadata =
      message->options().GetExtension(phaser::ros_message);
  if (!message_metadata.constants().empty()) {
    return {message_metadata.constants().begin(),
            message_metadata.constants().end()};
  }

  std::vector<std::string> constants;
  for (int i = 0; i < message->enum_type_count(); ++i) {
    const auto* enum_type = message->enum_type(i);
    if (!enum_type->options().HasExtension(phaser::ros_enum)) {
      continue;
    }
    const auto& enum_metadata =
        enum_type->options().GetExtension(phaser::ros_enum);
    for (int j = 0; j < enum_type->value_count(); ++j) {
      const auto* value = enum_type->value(j);
      const auto& value_metadata =
          value->options().GetExtension(phaser::ros_enum_value);
      if (value_metadata.ignore()) {
        continue;
      }
      const std::string name = value_metadata.name().empty()
                                   ? std::string(value->name())
                                   : value_metadata.name();
      const std::string text_value = value_metadata.value().empty()
                                         ? std::to_string(value->number())
                                         : value_metadata.value();
      constants.push_back(enum_metadata.type() + " " + name + "=" + text_value);
    }
  }
  return constants;
}

static std::string RosSourceDefinition(
    const google::protobuf::Descriptor* message) {
  std::string definition;
  for (const auto& constant : RosConstantDeclarations(message)) {
    definition += constant + "\n";
  }
  for (int i = 0; i < message->field_count(); ++i) {
    const auto* field = message->field(i);
    definition += RosFieldType(field) + " " + RosFieldName(field) + "\n";
  }
  return definition;
}

static void AppendRosDependencies(const google::protobuf::Descriptor* message,
                                  absl::flat_hash_set<std::string>* seen,
                                  std::string* definition) {
  for (int i = 0; i < message->field_count(); ++i) {
    const auto* field = message->field(i);
    const auto& field_metadata =
        field->options().GetExtension(phaser::ros_field);
    if (IsRosBuiltinType(RosFieldType(field))) {
      continue;
    }

    std::string data_type;
    std::string source_definition;
    const google::protobuf::Descriptor* dependency = nullptr;
    if (!field_metadata.nested_data_type().empty()) {
      data_type = field_metadata.nested_data_type();
      source_definition = field_metadata.nested_md5_text() + "\n";
    } else if (field->type() ==
               google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      dependency = field->message_type();
      data_type = RosDataType(dependency);
      source_definition = RosSourceDefinition(dependency);
    } else {
      continue;
    }

    if (!seen->insert(data_type).second) {
      continue;
    }
    *definition += std::string(80, '=') + "\n";
    *definition += "MSG: " + data_type + "\n";
    *definition += source_definition;
    if (dependency != nullptr) {
      AppendRosDependencies(dependency, seen, definition);
    }
  }
}

static std::string RosFullDefinition(
    const google::protobuf::Descriptor* message) {
  std::string definition = RosSourceDefinition(message);
  absl::flat_hash_set<std::string> seen;
  AppendRosDependencies(message, &seen, &definition);
  return definition;
}

std::string MessageGenerator::SanitizedIdentifier(
    const std::string& name) const {
  if (IsCppReservedWord(name)) {
    return name + "_";
  }
  return name;
}

std::string MessageGenerator::MemberVariableName(
    const std::string& proto_name) const {
  if (IsRosFrontend()) {
    return SanitizedIdentifier(proto_name);
  }
  return proto_name + "_";
}

std::string MessageGenerator::OneofVariantTypeName(
    const google::protobuf::OneofDescriptor* oneof) const {
  std::string name;
  bool capitalize = true;
  for (char c : oneof->name()) {
    if (c == '_') {
      capitalize = true;
      continue;
    }
    name.push_back(capitalize ? static_cast<char>(std::toupper(c)) : c);
    capitalize = false;
  }
  return SanitizedIdentifier(name + "Variant");
}

std::string MessageGenerator::OneofAlternativeTypeName(
    const google::protobuf::FieldDescriptor* field) const {
  std::string name(field->camelcase_name());
  if (!name.empty()) {
    name[0] = static_cast<char>(std::toupper(name[0]));
  }
  return SanitizedIdentifier(name + "Alternative");
}

int MessageGenerator::GetArraySize(
    const google::protobuf::FieldDescriptor* field) const {
  if (!field->options().HasExtension(phaser::array_size)) {
    return 0;
  }
  return static_cast<int>(field->options().GetExtension(phaser::array_size));
}

bool MessageGenerator::UsesArrayFacade(
    const google::protobuf::FieldDescriptor* field) const {
  return IsRosFrontend() && GetArraySize(field) > 0;
}

absl::Status MessageGenerator::ValidateArraySizeOption(
    const google::protobuf::FieldDescriptor* field) const {
  if (!field->options().HasExtension(phaser::array_size)) {
    return absl::OkStatus();
  }
  const int array_size = GetArraySize(field);
  const std::string context =
      absl::StrFormat("%s.%s", message_->full_name(), field->name());
  if (array_size <= 0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "phaser.array_size must be positive on field %s", context));
  }
  if (!field->is_repeated()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "phaser.array_size is only valid on repeated fields: %s", context));
  }
  if (field->is_map()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "phaser.array_size is not valid on map fields: %s", context));
  }
  return absl::OkStatus();
}

absl::Status MessageGenerator::ValidateFieldOptions() const {
  if (absl::Status status = ValidateRosMetadataOptions(); !status.ok()) {
    return status;
  }
  if (IsRosFrontend() && IsRosHeader(message_) && added_namespace_.empty()) {
    return absl::InvalidArgumentError(
        "ROS frontend generation for std_msgs.Header requires add_namespace "
        "to avoid colliding with the ROS std_msgs::Header type");
  }
  if (IsRosFrontend()) {
    if (absl::Status status = ValidateRosHeaderDescriptor(); !status.ok()) {
      return status;
    }
  }
  for (int i = 0; i < message_->field_count(); i++) {
    const auto* field = message_->field(i);
    if (absl::Status status = ValidateArraySizeOption(field); !status.ok()) {
      return status;
    }
    if (IsRosFrontend() && IsRosIntrinsic(field) &&
        (field->is_repeated() || field->containing_oneof() != nullptr)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "ROS intrinsic field %s.%s must be singular and cannot be in a "
          "oneof",
          message_->full_name(), field->name()));
    }
  }
  for (const auto& nested : nested_message_gens_) {
    if (absl::Status status = nested->ValidateFieldOptions(); !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status MessageGenerator::ValidateRosMetadataOptions() const {
  if (!generate_ros_metadata_ &&
      !message_->options().HasExtension(phaser::ros_message)) {
    return absl::OkStatus();
  }
  if (RosDataType(message_).empty()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("cannot infer ROS datatype for message %s; set "
                        "phaser.ros_message.data_type",
                        message_->full_name()));
  }
  bool has_ros_constant_enums = false;
  for (int i = 0; i < message_->enum_type_count(); ++i) {
    const auto* enum_type = message_->enum_type(i);
    if (!enum_type->options().HasExtension(phaser::ros_enum)) {
      continue;
    }
    has_ros_constant_enums = true;
    if (enum_type->options().GetExtension(phaser::ros_enum).type().empty()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("phaser.ros_enum.type must not be empty on enum %s",
                          enum_type->full_name()));
    }
  }
  const auto& message_metadata =
      message_->options().GetExtension(phaser::ros_message);
  if (has_ros_constant_enums && !message_metadata.constants().empty()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "message %s cannot combine phaser.ros_message.constants with "
        "phaser.ros_enum constant groups",
        message_->full_name()));
  }
  for (int i = 0; i < message_->field_count(); ++i) {
    const auto* field = message_->field(i);
    const std::string field_type = RosFieldType(field);
    if (field_type.empty()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "ROS type for field %s.%s is ambiguous; set phaser.ros_field.type",
          message_->full_name(), field->name()));
    }
    const auto& field_metadata =
        field->options().GetExtension(phaser::ros_field);
    if (!IsRosBuiltinType(field_type) &&
        field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
        field_metadata.nested_md5_text().empty()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "non-builtin ROS field %s.%s must map to a protobuf message or "
          "provide nested_md5_text",
          message_->full_name(), field->name()));
    }
    if (!field_metadata.nested_md5_text().empty() &&
        field_metadata.nested_data_type().empty()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "non-protobuf ROS field %s.%s must provide nested_data_type",
          message_->full_name(), field->name()));
    }
    if (!IsRosBuiltinType(field_type) &&
        field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
        RosDataType(field->message_type()).empty() &&
        field_metadata.nested_md5_text().empty()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "cannot infer ROS datatype for nested field %s.%s referencing %s",
          message_->full_name(), field->name(),
          field->message_type()->full_name()));
    }
  }
  return absl::OkStatus();
}

absl::Status MessageGenerator::ValidateRosHeaderDescriptor() const {
  if (!IsRosHeader(message_)) {
    return absl::OkStatus();
  }
  const auto* seq = message_->FindFieldByName("seq");
  const auto* stamp = message_->FindFieldByName("stamp");
  const auto* frame_id = message_->FindFieldByName("frame_id");
  if (seq == nullptr ||
      seq->type() != google::protobuf::FieldDescriptor::TYPE_UINT32 ||
      stamp == nullptr ||
      stamp->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE ||
      !IsRosTime(stamp->message_type()) || frame_id == nullptr ||
      frame_id->type() != google::protobuf::FieldDescriptor::TYPE_STRING) {
    return absl::InvalidArgumentError(
        "std_msgs.Header must declare uint32 seq, "
        "google.protobuf.Timestamp stamp, and string frame_id");
  }
  return absl::OkStatus();
}

std::string MessageGenerator::EnumName(
    const google::protobuf::EnumDescriptor* desc) {
  std::string name(desc->name());
  if (desc->containing_type() != nullptr) {
    name = std::string(desc->containing_type()->name()) + "_" + name;
  }
  return name;
}

std::string MessageGenerator::MessageName(
    const google::protobuf::Descriptor* desc, bool is_ref) {
  if (is_ref && IsAny(desc)) {
    return "::phaser::AnyMessage";
  }
  std::string full_name(desc->full_name());
  // If the message is in our package, use the short name.
  if (full_name.find(package_name_) == std::string::npos) {
    std::string cpp_name =
        absl::StrReplaceAll(desc->full_name(), {{".", "::"}});
    if (added_namespace_.empty()) {
      return cpp_name;
    }
    // Add the namespace between the final :: and the message name.
    size_t pos = cpp_name.rfind("::");
    return cpp_name.substr(0, pos) + "::" + added_namespace_ +
           cpp_name.substr(pos);
  }
  std::string name(desc->name());
  if (desc->containing_type() != nullptr) {
    name = std::string(desc->containing_type()->name()) + "_" + name;
  }
  return name;
}

std::string MessageGenerator::FieldCFieldType(
    const google::protobuf::FieldDescriptor* field) {
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
      return "FloatField<true, false>";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "BoolField<false, false>";
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return "EnumField<" + EnumName(field->enum_type()) + ", " +
             EnumName(field->enum_type()) + "Stringizer, " +
             EnumName(field->enum_type()) + "Parser>";
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "StringField";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (IsAny(field)) {
        return "AnyField";
      }
      if (IsRosFrontend() && IsRosIntrinsic(field)) {
        return RosIntrinsicFieldType(field);
      }
      return "IndirectMessageField<" +
             MessageName(field->message_type(), true) + ">";

    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  // Unreachable: every protobuf field type is handled above and GROUP exits.
  abort();
}

std::string MessageGenerator::FieldInfoType(
    const google::protobuf::FieldDescriptor* field) {
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
      return "::phaser::FieldType::kFieldInt32";
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
      return "::phaser::FieldType::kFieldInt32";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      return "::phaser::FieldType::kFieldInt32";
    case google::protobuf::FieldDescriptor::TYPE_INT64:
      return "::phaser::FieldType::kFieldInt64";
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
      return "::phaser::FieldType::kFieldInt64";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      return "::phaser::FieldType::kFieldInt64";
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
      return "::phaser::FieldType::kFieldInt32";
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      return "::phaser::FieldType::kFieldInt32";
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
      return "::phaser::FieldType::kFieldInt64";
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      return "::phaser::FieldType::kFieldInt64";
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return "::phaser::FieldType::kFieldDouble";
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      return "::phaser::FieldType::kFieldFloat";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "::phaser::FieldType::kFieldBool";
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return "::phaser::FieldType::kFieldEnum";
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      return "::phaser::FieldType::kFieldString";
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "::phaser::FieldType::kFieldBytes";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return "::phaser::FieldType::kFieldMessage";

    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  // Unreachable: every protobuf field type is handled above and GROUP exits.
  abort();
}

std::string MessageGenerator::FieldCType(
    const google::protobuf::FieldDescriptor* field) {
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
      if (IsRosFrontend() && IsRosIntrinsic(field)) {
        return RosIntrinsicCType(field);
      }
      return MessageName(field->message_type(), true);
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  // Unreachable: every protobuf field type is handled above and GROUP exits.
  abort();
}

std::string MessageGenerator::FieldRepeatedCType(
    const google::protobuf::FieldDescriptor* field) {
  const int array_size = GetArraySize(field);
  if (IsRosFrontend() && array_size > 0) {
    return FieldRepeatedArrayCType(field, array_size);
  }
  return FieldRepeatedVectorCType(field);
}

std::string MessageGenerator::FieldRepeatedVectorCType(
    const google::protobuf::FieldDescriptor* field) {
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
      return "EnumVectorField<" + EnumName(field->enum_type()) + ", " +
             EnumName(field->enum_type()) + "Stringizer, " +
             EnumName(field->enum_type()) + "Parser" + packed;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "StringVectorField";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return "MessageVectorField<" + MessageName(field->message_type(), true) +
             ">";
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  // Unreachable: every protobuf field type is handled above and GROUP exits.
  abort();
}

std::string MessageGenerator::FieldRepeatedArrayCType(
    const google::protobuf::FieldDescriptor* field, int array_size) {
  const std::string extent = std::to_string(array_size);
  const std::string packed = field->is_packed() ? ", true>" : ", false>";
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
      return "PrimitiveArrayField<int32_t, " + extent + ", false, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
      return "PrimitiveArrayField<int32_t, " + extent + ", false, true" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      return "PrimitiveArrayField<int32_t, " + extent + ", true, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
      return "PrimitiveArrayField<int64_t, " + extent + ", false, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
      return "PrimitiveArrayField<int64_t, " + extent + ", false, true" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      return "PrimitiveArrayField<int64_t, " + extent + ", true, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
      return "PrimitiveArrayField<uint32_t, " + extent + ", false, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      return "PrimitiveArrayField<uint32_t, " + extent + ", true, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
      return "PrimitiveArrayField<uint64_t, " + extent + ", false, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      return "PrimitiveArrayField<uint64_t, " + extent + ", true, false" +
             packed;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return "PrimitiveArrayField<double, " + extent + ", true, false" + packed;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      return "PrimitiveArrayField<float, " + extent + ", true, false" + packed;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "PrimitiveArrayField<bool, " + extent + ", false, false" + packed;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return "EnumArrayField<" + EnumName(field->enum_type()) + ", " + extent +
             ", " + EnumName(field->enum_type()) + "Stringizer, " +
             EnumName(field->enum_type()) + "Parser" + packed;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "StringArrayField<" + extent + ">";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return "MessageArrayField<" + MessageName(field->message_type(), true) +
             ", " + extent + ">";
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  abort();
}

std::string MessageGenerator::FieldUnionCType(
    const google::protobuf::FieldDescriptor* field) {
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
      return "UnionEnumField<" + EnumName(field->enum_type()) + ", " +
             EnumName(field->enum_type()) + "Stringizer, " +
             EnumName(field->enum_type()) + "Parser>";
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "UnionStringField";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return "UnionMessageField<" + MessageName(field->message_type(), true) +
             ">";
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  // Unreachable: every protobuf field type is handled above and GROUP exits.
  abort();
}

uint32_t MessageGenerator::FieldBinarySize(
    const google::protobuf::FieldDescriptor* field) {
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
      return 4;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      std::cerr << "Groups are not supported\n";
      exit(1);
  }
  // Unreachable: every protobuf field type is handled above and GROUP exits.
  abort();
}

bool MessageGenerator::IsAny(const google::protobuf::Descriptor* desc) {
  return desc->full_name() == "google.protobuf.Any";
}

bool MessageGenerator::IsRosTime(
    const google::protobuf::Descriptor* desc) const {
  return desc != nullptr && desc->full_name() == "google.protobuf.Timestamp";
}

bool MessageGenerator::IsRosDuration(
    const google::protobuf::Descriptor* desc) const {
  return desc != nullptr && desc->full_name() == "google.protobuf.Duration";
}

bool MessageGenerator::IsRosHeader(
    const google::protobuf::Descriptor* desc) const {
  return desc != nullptr && desc->full_name() == "std_msgs.Header";
}

bool MessageGenerator::IsRosIntrinsic(
    const google::protobuf::FieldDescriptor* field) const {
  if (field == nullptr ||
      field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    return false;
  }
  const auto* desc = field->message_type();
  return IsRosTime(desc) || IsRosDuration(desc) || IsRosHeader(desc);
}

std::string MessageGenerator::RosIntrinsicFieldType(
    const google::protobuf::FieldDescriptor* field) {
  const std::string backend = MessageName(field->message_type(), true);
  if (IsRosTime(field->message_type())) {
    return "RosTimeField<" + backend + ">";
  }
  if (IsRosDuration(field->message_type())) {
    return "RosDurationField<" + backend + ">";
  }
  assert(IsRosHeader(field->message_type()));
  return "RosHeaderField<" + backend + ">";
}

std::string MessageGenerator::RosIntrinsicCType(
    const google::protobuf::FieldDescriptor* field) {
  if (IsRosTime(field->message_type())) {
    return "::ros::Time";
  }
  if (IsRosDuration(field->message_type())) {
    return "::ros::Duration";
  }
  assert(IsRosHeader(field->message_type()));
  return "::std_msgs::Header";
}

bool MessageGenerator::IsAny(const google::protobuf::FieldDescriptor* field) {
  return field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
         field->message_type()->full_name() == "google.protobuf.Any";
}

void MessageGenerator::CompileUnions() {
  for (int i = 0; i < message_->field_count(); i++) {
    const auto& field = message_->field(i);
    const google::protobuf::OneofDescriptor* oneof = field->containing_oneof();
    if (oneof == nullptr) {
      // Not a oneof, already handled in CompileFields.
      continue;
    }
    // We will have created a UnionInfo during the first pass in CompileFields.
    auto it = unions_.find(oneof);
    assert(it != unions_.end());

    auto union_info = it->second;
    // Append field to the members of the union.
    std::string field_type = FieldUnionCType(field);
    // Append union type to the end of the the union member type
    if (union_info->member_type == "UnionField") {
      union_info->member_type += "<";
    } else {
      union_info->member_type += ", ";
    }
    union_info->member_type += "::phaser::" + field_type;
    uint32_t field_size = FieldBinarySize(field);
    union_info->members.push_back(std::make_shared<FieldInfo>(
        field, 0, union_info->id,
        MemberVariableName(std::string(field->name())), field_type,
        FieldCType(field), field_size));
    union_info->binary_size = std::max(union_info->binary_size, 4 + field_size);
    union_info->id++;
  }
  for (auto& [oneof, union_info] : unions_) {
    union_info->member_type += ">";
  }
}

void MessageGenerator::CompileFields() {
  uint32_t offset = 0;
  uint32_t id = 0;
  fields_.reserve(static_cast<size_t>(message_->field_count()));
  for (int i = 0; i < message_->field_count(); i++) {
    const auto& field = message_->field(i);
    std::string field_type;
    const google::protobuf::OneofDescriptor* oneof = field->containing_oneof();
    uint32_t field_size;
    uint32_t next_id = id;
    if (oneof != nullptr) {
      // In order to keep oneof fields in the correct position for printing so
      // that we match the protobuf printer, we create the union field here and
      // add it to the fields_in_order_ vector.  We will fill it in later during
      // the CompileUnions phase.  Since there will be multiple fields in the
      // union and we see each of them here, we only add it the first time we
      // see the oneof.
      auto it = unions_.find(oneof);
      if (it == unions_.end()) {
        auto union_info = std::make_shared<UnionInfo>(
            oneof, 4, MemberVariableName(std::string(oneof->name())),
            "UnionField");
        unions_[oneof] = union_info;
        fields_in_order_.push_back(union_info);
      }
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
    fields_.push_back(std::make_shared<FieldInfo>(
        field, offset, id, MemberVariableName(std::string(field->name())),
        field_type, FieldCType(field), field_size));
    fields_in_order_.push_back(fields_.back());
    offset += field_size;
    id = next_id;
  }
}

void MessageGenerator::FinalizeOffsetsAndSizes() {
  uint32_t size = 4;
  // Find the max field id.  This will determine the number of 32-bit words we
  // need for the presence mask.
  int32_t max_id = -1;
  for (auto& field : fields_) {
    max_id = std::max(max_id, int32_t(field->id));
  }
  presence_mask_size_ =
      max_id == -1 ? 0 : static_cast<uint32_t>(((max_id >> 5) + 1) * 4);
  size += presence_mask_size_;

  // Finalize the offsets in the fields vector now that we know the header size.
  for (auto& field : fields_) {
    field->offset += size;
  }

  // Set the offsets for the unions.
  uint32_t offset =
      fields_.empty() ? size
                      : (fields_.back()->offset + fields_.back()->binary_size);
  // Align offset to 4 bytes.
  offset = (offset + 3) & ~3u;
  size = offset;

  // Add the offset to the unions.
  for (auto& [oneof, u] : unions_) {
    u->offset = offset;
    for (auto& field : u->members) {
      field->offset += offset;
    }
    offset += u->binary_size;
    size += u->binary_size;
  }
  binary_size_ = size;
}

absl::Status MessageGenerator::GenerateHeader(std::ostream& os) {
  if (absl::Status status = ValidateFieldOptions(); !status.ok()) {
    return status;
  }
  for (const auto& nested : nested_message_gens_) {
    if (absl::Status status = nested->GenerateHeader(os); !status.ok()) {
      return status;
    }
  }
  CompileFields();
  CompileUnions();
  FinalizeOffsetsAndSizes();

  os << (IsRosFrontend() ? "struct " : "class ") << MessageName(message_)
     << " : public ::phaser::Message {\n";
  os << " public:\n";
  if (IsRosFrontend()) {
    GenerateRosOneofTypes(os);
  }
  if (generate_active_message_) {
    os << "  // Optional user-attached payload, not part of the wire format.\n";
    os << "  std::any active_message;\n\n";
  }
  for (const auto& [oneof, u] : unions_) {
    os << "  inline static constexpr uint32_t " << u->member_name
       << "_field_numbers[] = {";
    const char* separator = "";
    for (const auto& field : u->members) {
      os << separator << field->field->number();
      separator = ", ";
    }
    os << "};\n";
  }
  if (!unions_.empty()) {
    os << "\n";
  }
  // Generate constructors.
  GenerateConstructors(os, true);
  os << "  " << MessageName(message_) << "* operator->() { return this; }\n";
  os << "  const " << MessageName(message_)
     << "* operator->() const { return this; }\n";
  // Generate size functions.
  GenerateSizeFunctions(os);
  // Generate creators.
  GenerateCreators(os, true);
  // Generate clear function.
  GenerateClear(os, true);
  if (IsRosFrontend()) {
    GenerateRosSyncToPayload(os);
  }
  // Generate field metadata.
  GenerateFieldMetadata(os);
  os << "  static constexpr size_t MetadataTypeCount() { return "
     << ReachableMessageTypeCount() << "; }\n";

  os << "  static constexpr std::string_view FullName() { return \""
     << message_->full_name() << "\"; }\n";
  os << "  static constexpr std::string_view Name() { return \""
     << message_->name() << "\"; }\n\n";
  GenerateRosMetadata(os);

  os << "  std::string GetName() const override { return std::string(Name()); "
        "}\n";
  os << "  std::string GetFullName() const override { return "
        "std::string(FullName()); }\n";

  os << "  friend std::ostream &operator<<(std::ostream &os, const "
     << MessageName(message_) << " &msg);\n\n";

  os << R"XXX(  void DebugDump() const {
    runtime->pb->Dump(std::cout);
    toolbelt::Hexdump(runtime->pb, runtime->pb->hwm);
  }

)XXX";

  GenerateNestedTypes(os);
  GenerateFieldNumbers(os);

  GenerateMessageInfo(os, true);

  GenerateIndent(os);
  GenerateCopy(os, true);
  GenerateDebugString(os);

  if (IsRosFrontend()) {
    GenerateRosOwnerCopyMove(os, true);
    GeneratePublicFieldDeclarations(os);
  }

  // Generate protobuf accessors.
  if (!IsRosFrontend()) {
    GenerateProtobufAccessors(os);
  }

  GenerateProtobufSerialization(os);
  GenerateROSSerialization(os, true);

  // Generate serialized size.
  GenerateSerializedSize(os, true);
  // Generate serializer.
  GenerateSerializer(os, true);
  // Generate deserializer.
  GenerateDeserializer(os, true);

  if (!IsRosFrontend()) {
    os << " private:\n";
    GenerateFieldDeclarations(os);
  }
  os << "};\n\n";

  // Steamer outside the class.
  GenerateStreamer(os);
  GenerateCopy(os, false);
  return absl::OkStatus();
}

void MessageGenerator::GenerateRosMetadata(std::ostream& os) {
  if (!generate_ros_metadata_ &&
      !message_->options().HasExtension(phaser::ros_message)) {
    return;
  }
  os << "  static constexpr std::string_view RosDataType() { return \""
     << absl::CEscape(RosDataType(message_)) << "\"; }\n";
  os << "  static constexpr std::string_view RosDefinition() { return \""
     << absl::CEscape(RosFullDefinition(message_)) << "\"; }\n";
  os << "  static std::string RosMd5() {\n";
  os << "    std::string text;\n";

  bool first_declaration = true;
  auto generate_separator = [&os, &first_declaration]() {
    if (!first_declaration) {
      os << "    text.push_back('\\n');\n";
    }
    first_declaration = false;
  };
  auto generate_append_literal =
      [&os, &generate_separator](const std::string& line) {
        generate_separator();
        os << "    text += \"" << absl::CEscape(line) << "\";\n";
      };
  for (const auto& constant : RosConstantDeclarations(message_)) {
    generate_append_literal(constant);
  }
  for (int i = 0; i < message_->field_count(); ++i) {
    const auto* field = message_->field(i);
    const auto& field_metadata =
        field->options().GetExtension(phaser::ros_field);
    const std::string field_type = RosFieldType(field);
    const std::string field_name = RosFieldName(field);
    generate_separator();
    if (IsRosBuiltinType(field_type)) {
      os << "    text += \"" << absl::CEscape(field_type + " " + field_name)
         << "\";\n";
    } else if (!field_metadata.nested_md5_text().empty()) {
      os << "    text += ::phaser::Md5(\""
         << absl::CEscape(field_metadata.nested_md5_text()) << "\");\n";
      os << "    text += \" " << absl::CEscape(field_name) << "\";\n";
    } else {
      os << "    text += " << MessageName(field->message_type())
         << "::RosMd5();\n";
      os << "    text += \" " << absl::CEscape(field_name) << "\";\n";
    }
  }
  os << "    return ::phaser::Md5(text);\n";
  os << "  }\n\n";
}

void MessageGenerator::GenerateRosSyncToPayload(std::ostream& os) {
  os << "  void SyncToPayload() const override {\n";
  for (const auto& field : fields_) {
    if (field->field->type() !=
            google::protobuf::FieldDescriptor::TYPE_MESSAGE ||
        (!field->field->is_repeated() && IsAny(field->field))) {
      continue;
    }
    os << "    " << field->member_name << ".SyncToPayload();\n";
  }
  for (const auto& [oneof, union_info] : unions_) {
    os << "    switch (" << union_info->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < union_info->members.size(); ++i) {
      const auto& member = union_info->members[i];
      if (member->field->type() !=
          google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        continue;
      }
      os << "      case " << member->field->number() << ":\n";
      os << "        " << union_info->member_name << ".template GetReference<"
         << i << ", " << member->c_type << ">().SyncToPayload();\n";
      os << "        break;\n";
    }
    os << "      default:\n";
    os << "        break;\n";
    os << "    }\n";
  }
  os << "  }\n\n";
}

void MessageGenerator::GenerateRosOwnerCopyMove(std::ostream& os, bool decl) {
  if (!IsRosFrontend()) {
    return;
  }
  const std::string name = MessageName(message_);
  if (decl) {
    os << "  " << name << "(const " << name << "& other);\n";
    os << "  " << name << "& operator=(const " << name << "& other);\n";
    os << "  " << name << "(" << name << "&& other) noexcept;\n";
    os << "  " << name << "& operator=(" << name << "&& other) noexcept;\n\n";
    return;
  }

  os << name << "::" << name << "(const " << name
     << "& other) : Message(other)\n";
  GenerateFieldInitializers(os, ", ");
  os << R"XXX({
  if (other.runtime != nullptr && other.runtime.use_count() == 0) {
    return;
  }
  size_t initial_size = other.BinarySize() * 2;
  if (initial_size < 8192) {
    initial_size = 8192;
  }
  InitDynamicMutable(initial_size, ::phaser::Tuning::kPerformance);
  (void)CloneFrom(other);
}

)XXX";

  os << name << "& " << name << "::operator=(const " << name << "& other) {\n";
  os << "  if (this != &other) {\n";
  os << "    (void)CloneFrom(other);\n";
  os << "  }\n";
  os << "  return *this;\n";
  os << "}\n\n";

  os << name << "::" << name << "(" << name << "&& other) noexcept\n";
  os << "  : Message(std::move(other))\n";
  const char* sep = ", ";
  for (auto& field : fields_) {
    os << sep << field->member_name << "(std::move(other." << field->member_name
       << "))\n";
    sep = ", ";
  }
  for (auto& [oneof, u] : unions_) {
    os << sep << u->member_name << "(std::move(other." << u->member_name
       << "))\n";
  }
  os << "{}\n\n";

  os << name << "& " << name << "::operator=(" << name << "&& other) noexcept "
     << "{\n";
  os << "  if (this != &other) {\n";
  os << "    (void)CloneFrom(other);\n";
  os << "    other.Clear();\n";
  os << "  }\n";
  os << "  return *this;\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateSource(std::ostream& os) {
  for (const auto& nested : nested_message_gens_) {
    nested->GenerateSource(os);
  }

  GenerateConstructors(os, false);
  if (IsRosFrontend()) {
    GenerateRosOwnerCopyMove(os, false);
  }

  // Generate creators.
  GenerateCreators(os, false);
  // Generate clear function.
  GenerateClear(os, false);

  GenerateMessageInfo(os, false);

  // Generate serialized size.
  GenerateSerializedSize(os, false);
  // Generate serializer.
  GenerateSerializer(os, false);
  // Generate deserializer.
  GenerateDeserializer(os, false);
  GenerateROSSerialization(os, false);

  // Phaser bank
  GeneratePhaserBank(os);
}

void MessageGenerator::GenerateFieldDeclarations(std::ostream& os) {
  for (auto& field : fields_) {
    os << "  ::phaser::" << field->member_type << " " << field->member_name
       << ";\n";
  }
  for (auto& [oneof, u] : unions_) {
    if (IsRosFrontend()) {
      os << "  " << OneofVariantTypeName(oneof) << " " << u->member_name
         << ";\n";
    } else {
      os << "  ::phaser::" << u->member_type << " " << u->member_name << ";\n";
    }
  }
}

void MessageGenerator::GenerateRosOneofTypes(std::ostream& os) {
  for (auto& [oneof, u] : unions_) {
    const std::string variant_name = OneofVariantTypeName(oneof);
    os << "  struct " << variant_name
       << " : public ::phaser::" << u->member_type << " {\n";
    os << "    using Base = ::phaser::" << u->member_type << ";\n";
    os << "    using Base::Base;\n";
    for (size_t i = 0; i < u->members.size(); ++i) {
      const auto& field = u->members[i];
      const std::string alternative_name =
          OneofAlternativeTypeName(field->field);
      os << "    struct " << alternative_name << " {\n";
      os << "      using value_type = " << field->c_type << ";\n";
      os << "      static constexpr size_t kIndex = " << i << ";\n";
      os << "      static constexpr int kFieldNumber = "
         << field->field->number() << ";\n";
      os << "      static constexpr bool kIsMessage = "
         << (field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_MESSAGE
                 ? "true"
                 : "false")
         << ";\n";
      os << "    };\n";
    }
    os << "  };\n";
    for (const auto& field : u->members) {
      const std::string alternative_name =
          OneofAlternativeTypeName(field->field);
      os << "  using " << alternative_name << " = " << variant_name
         << "::" << alternative_name << ";\n";
    }
    os << "\n";
  }
}

void MessageGenerator::GeneratePublicFieldDeclarations(std::ostream& os) {
  if (fields_.empty() && unions_.empty()) {
    return;
  }
  os << "\n";
  GenerateFieldDeclarations(os);
}

void MessageGenerator::GenerateEnums(std::ostream& os) {
  // Nested enums.
  for (auto& msg : nested_message_gens_) {
    msg->GenerateEnums(os);
  }
  for (auto& enum_gen : enum_gens_) {
    enum_gen->GenerateHeader(os);
  }
}

void MessageGenerator::GenerateConstructors(std::ostream& os, bool decl) {
  // Generate default constructor.
  GenerateDefaultConstructor(os, decl);
  GenerateInternalDefaultConstructor(os, decl);
  // Generate main constructor.
  GenerateMainConstructor(os, decl);
}

void MessageGenerator::GenerateDefaultConstructor(std::ostream& os, bool decl) {
  if (decl) {
    os << "  " << MessageName(message_)
       << "(size_t initial_size = 8192, ::phaser::Tuning tuning = "
          "::phaser::Tuning::kPerformance);\n";
    return;
  }
  os << MessageName(message_) << "::" << MessageName(message_)
     << "(size_t initial_size, ::phaser::Tuning tuning)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os);
  os << R"XXX({
  if (BinarySize() > initial_size) {
    initial_size = BinarySize() * 2;
  }
  InitDynamicMutable(initial_size, tuning);
}

)XXX";
}

void MessageGenerator::GenerateInternalDefaultConstructor(std::ostream& os,
                                                          bool decl) {
  if (decl) {
    os << "  " << MessageName(message_) << "(::phaser::InternalDefault d);\n";
    return;
  }
  os << MessageName(message_) << "::" << MessageName(message_)
     << "(::phaser::InternalDefault)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os);
  os << "{}\n\n";
}

void MessageGenerator::GenerateMainConstructor(std::ostream& os, bool decl) {
  if (decl) {
    os << "  " << MessageName(message_)
       << "(std::shared_ptr<::phaser::MessageRuntime> runtime, "
          "::toolbelt::BufferOffset "
          "offset);\n";
    return;
  }
  os << MessageName(message_) << "::" << MessageName(message_) << "(";
  os << "std::shared_ptr<::phaser::MessageRuntime> runtime_ptr, "
        "::toolbelt::BufferOffset "
        "offset) : Message(runtime_ptr, offset)\n";
  // Generate field initializers.
  GenerateFieldInitializers(os, ", ");
  os << "{}\n\n";
}

void MessageGenerator::GenerateFieldInitializers(std::ostream& os,
                                                 const char* sep) {
  if (fields_.empty() && unions_.empty()) {
    return;
  }
  os << "#pragma clang diagnostic push\n";
  os << "#pragma clang diagnostic ignored \"-Winvalid-offsetof\"\n";
  for (auto& field : fields_) {
    os << sep << field->member_name << "(offsetof(" << MessageName(message_)
       << ", " << field->member_name << "), " << field->offset << ", "
       << field->id << ", " << field->field->number() << ")\n";
    sep = ", ";
  }
  for (auto& [oneof, u] : unions_) {
    os << sep << u->member_name << "(offsetof(" << MessageName(message_) << ", "
       << u->member_name << "), " << u->offset << ", 0, 0, "
       << "absl::MakeConstSpan(" << u->member_name << "_field_numbers))\n";
    sep = ", ";
  }
  os << "#pragma clang diagnostic pop\n\n";
}

void MessageGenerator::GenerateCreators(std::ostream& os, bool decl) {
  if (decl) {
    os << "  static " << MessageName(message_)
       << " CreateMutable(void *addr, size_t size, ::phaser::Tuning tuning = "
          "::phaser::Tuning::kPerformance);\n";
    os << "  static " << MessageName(message_)
       << " CreateReadonly(const void *addr, size_t size);\n";
    os << "  static " << MessageName(message_)
       << " CreateDynamicMutable(size_t initial_size, ::phaser::Tuning tuning "
          "= ::phaser::Tuning::kPerformance);\n";
    os << "  void InitDynamicMutable(size_t initial_size = 8192, "
          "::phaser::Tuning tuning = ::phaser::Tuning::kPerformance);\n";
    os << "  static absl::StatusOr<" << MessageName(message_)
       << "> TryCreateDynamicMutable(size_t initial_size, "
          "std::function<absl::StatusOr<void*>(size_t)> alloc, "
          "std::function<void(void*)> free, "
          "std::function<absl::StatusOr<void*>(void*, size_t, size_t)> "
          "realloc, ::phaser::Tuning tuning = "
          "::phaser::Tuning::kPerformance);\n";
    os << "  static " << MessageName(message_)
       << " CreateDynamicMutable(size_t initial_size, "
          "std::function<absl::StatusOr<void*>(size_t)> alloc, "
          "std::function<void(void*)> free, "
          "std::function<absl::StatusOr<void*>(void*, size_t, size_t)> "
          "realloc, ::phaser::Tuning tuning = "
          "::phaser::Tuning::kPerformance);\n";
    return;
  }
  os << "// Create a mutable message in the given memory.\n";
  os << MessageName(message_) << " " << MessageName(message_)
     << "::CreateMutable(void *addr, size_t size, ::phaser::Tuning tuning) {\n"
        "  ::toolbelt::PayloadBuffer *pb = new (addr) "
        "::toolbelt::PayloadBuffer(static_cast<uint32_t>(size), tuning == "
        "::phaser::Tuning::kPerformance);\n"
        "  ::toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "  ::phaser::InitializeRuntimeControl(&pb, "
     << MessageName(message_)
     << "::MetadataTypeCount());\n"
        "  ::phaser::MessageRuntime runtime(pb, true);\n"
        "  auto msg = "
     << MessageName(message_)
     << "(BorrowRuntime(runtime), pb->message);\n"
        "  msg.InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "  return msg;\n"
        "}\n"
        "\n";

  os << "// Create a readonly message that already exists at the given "
        "address with a size.\n";
  os << MessageName(message_) << " " << MessageName(message_)
     << "::CreateReadonly(const void *addr, size_t size) {\n"
        "  ::toolbelt::PayloadBuffer *pb ="
        "reinterpret_cast<::toolbelt::PayloadBuffer "
        "*>(const_cast<void*>(addr));\n"
        "  ::phaser::MessageRuntime runtime(pb, size);\n"
        "  return "
     << MessageName(message_)
     << "(BorrowRuntime(runtime), pb->message);\n"
        "}\n\n";
  os << "// Create a message in a dynamically resized buffer allocated from "
        "the heap.\n";
  os << "absl::StatusOr<" << MessageName(message_) << "> "
     << MessageName(message_)
     << "::TryCreateDynamicMutable(size_t initial_size, "
        "std::function<absl::StatusOr<void*>(size_t)> alloc, "
        "std::function<void(void*)> free,"
        "std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc, "
        "::phaser::Tuning tuning) "
        "{\n"
        "  absl::StatusOr<::toolbelt::PayloadBuffer *> pbs = "
        "::phaser::NewDynamicBuffer(initial_size, std::move(alloc), "
        "std::move(realloc), tuning);\n"
        "  if (!pbs.ok()) return pbs.status();\n"
        "  ::toolbelt::PayloadBuffer *pb = *pbs;\n"
        "  ::toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "  ::phaser::InitializeRuntimeControl(&pb, "
     << MessageName(message_)
     << "::MetadataTypeCount());\n"
        "  auto runtime = "
        "std::make_shared<::phaser::DynamicMutableMessageRuntime>(pb, "
        "std::move(free));\n"
        "  auto msg = "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "  msg.InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "  return msg;\n"
        "}\n\n";

  os << MessageName(message_) << " " << MessageName(message_)
     << "::CreateDynamicMutable(size_t initial_size, "
        "std::function<absl::StatusOr<void*>(size_t)> alloc, "
        "std::function<void(void*)> free,"
        "std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc, "
        "::phaser::Tuning tuning) "
        "{\n"
        "  auto message = TryCreateDynamicMutable(initial_size, "
        "std::move(alloc), std::move(free), std::move(realloc), tuning);\n"
        "  if (!message.ok()) abort();\n"
        "  return std::move(*message);\n"
        "}\n\n";

  os << MessageName(message_) << " " << MessageName(message_)
     << "::CreateDynamicMutable(size_t initial_size = 8192, ::phaser::Tuning "
        "tuning) {\n";
  os << "  return CreateDynamicMutable(initial_size, [](size_t size) -> "
        "absl::StatusOr<void*>{ return ::malloc(size);},"
        " ::free,"
        " [](void* p, size_t /*old_size*/, size_t new_size) -> "
        "absl::StatusOr<void*> { return ::realloc(p, new_size);}, tuning);\n";
  os << "}\n\n";

  os << "void " << MessageName(message_)
     << "::InitDynamicMutable(size_t initial_size, ::phaser::Tuning tuning) {\n"
        "  ::toolbelt::PayloadBuffer *pb = "
        "::phaser::NewDynamicBuffer(initial_size, tuning);\n"
        "  ::toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
        "  ::phaser::InitializeRuntimeControl(&pb, "
     << MessageName(message_)
     << "::MetadataTypeCount());\n"
        "  auto runtime = "
        "std::make_shared<::phaser::DynamicMutableMessageRuntime>(pb, "
        "::free);\n"
        "  this->runtime = runtime;\n"
        "  this->absolute_binary_offset = pb->message;\n"
        "  this->InstallMetadata<"
     << MessageName(message_)
     << ">();\n"
        "}\n\n";
}

void MessageGenerator::GenerateSizeFunctions(std::ostream& os) {
  os << "  static constexpr size_t BinarySize() { return HeaderSize() + "
     << binary_size_ << "; }\n";
  os << "  static constexpr size_t PresenceMaskSize() { return "
     << presence_mask_size_ << "; }\n";
  os << "  static constexpr uint32_t HeaderSize() { return 4 + "
        "PresenceMaskSize(); }\n";
}

size_t MessageGenerator::ReachableMessageTypeCount() const {
  std::set<const google::protobuf::Descriptor*> reachable;
  std::function<void(const google::protobuf::Descriptor*)> visit =
      [&](const google::protobuf::Descriptor* descriptor) {
        if (descriptor == nullptr || !reachable.insert(descriptor).second) {
          return;
        }
        for (int i = 0; i < descriptor->field_count(); ++i) {
          const auto* field = descriptor->field(i);
          if (field->type() ==
              google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
            visit(field->message_type());
          }
        }
      };
  visit(message_);
  return reachable.size();
}

void MessageGenerator::GenerateFieldMetadata(std::ostream& os) {
  // Build a vector of fields from the fields an unions, sorted by field number.
  std::vector<std::shared_ptr<FieldInfo>> all_fields;
  for (auto& field : fields_) {
    all_fields.push_back(field);
  }
  for (auto& [oneof, u] : unions_) {
    for (auto& field : u->members) {
      all_fields.push_back(field);
    }
  }

  std::sort(all_fields.begin(), all_fields.end(),
            [](const auto& a, const auto& b) {
              return a->field->number() < b->field->number();
            });

  // Find the interval with the greatest metadata-size saving. For [i, j],
  // sparse entries cost 8 bytes each while the dense representation costs
  // 4 bytes per field-number slot. Missing slots use a zero field offset.
  // Separating the start- and end-dependent terms finds the best interval in
  // O(number_of_fields).
  size_t dense_begin = 0;
  size_t dense_end = 0;
  int64_t best_saving = 0;
  int64_t best_start_score = std::numeric_limits<int64_t>::min();
  size_t best_start_index = 0;
  for (size_t end = 0; end < all_fields.size(); ++end) {
    const int64_t number = all_fields[end]->field->number();
    const int64_t start_score = -8 * static_cast<int64_t>(end) + 4 * number;
    if (start_score > best_start_score) {
      best_start_score = start_score;
      best_start_index = end;
    }

    const int64_t end_score =
        8 * static_cast<int64_t>(end + 1) - 4 * (number + 1);
    const int64_t saving = end_score + best_start_score;
    if (saving > best_saving) {
      best_saving = saving;
      dense_begin = best_start_index;
      dense_end = end;
    }
  }

  const bool has_dense_range = best_saving > 0;
  const uint32_t dense_base =
      has_dense_range
          ? static_cast<uint32_t>(all_fields[dense_begin]->field->number())
          : 0;
  const uint32_t dense_span =
      has_dense_range
          ? static_cast<uint32_t>(all_fields[dense_end]->field->number() -
                                  all_fields[dense_begin]->field->number() + 1)
          : 0;
  const size_t dense_count = has_dense_range ? dense_end - dense_begin + 1 : 0;
  const size_t sparse_count = all_fields.size() - dense_count;

  os << "  struct " << MessageName(message_) << "FieldData {";
  os << "\n    ::phaser::HybridFieldData header;\n";
  if (dense_span != 0) {
    os << "    ::phaser::FieldValue dense_fields[" << dense_span << "];\n";
  }
  if (sparse_count != 0) {
    os << "    ::phaser::SparseFieldData sparse_fields[" << sparse_count
       << "];\n";
  }
  os << "  };\n";
  const size_t metadata_size =
      4 * sizeof(uint32_t) +
      static_cast<size_t>(dense_span) * sizeof(uint32_t) +
      sparse_count * 2 * sizeof(uint32_t);
  os << "  static_assert(sizeof(" << MessageName(message_)
     << "FieldData) == " << metadata_size
     << "u, \"Unexpected hybrid field metadata padding\");\n";

  uint32_t max_offset = 0;
  uint32_t max_id = 0;
  for (const auto& field : all_fields) {
    max_offset = std::max(max_offset, field->offset);
    max_id = std::max(max_id, field->id);
  }
  os << "  static_assert(" << max_offset
     << "u <= 0x00ffffffu, \"Field offset exceeds 24 bits\");\n";
  os << "  static_assert(" << max_id
     << "u <= 0xffu, \"Field presence id exceeds 8 bits\");\n";

  os << "  static constexpr " << MessageName(message_)
     << "FieldData field_data = {\n";
  os << "    .header = {\n";
  os << "      .magic = ::phaser::kHybridFieldDataMagic,\n";
  os << "      .dense_base = " << dense_base << ",\n";
  os << "      .dense_span = " << dense_span << ",\n";
  os << "      .sparse_count = " << sparse_count << ",\n";
  os << "    },\n";

  if (dense_span != 0) {
    os << "    .dense_fields = {\n";
    size_t field_index = dense_begin;
    for (uint32_t dense_index = 0; dense_index < dense_span; ++dense_index) {
      const uint32_t field_number = dense_base + dense_index;
      if (field_index <= dense_end &&
          static_cast<uint32_t>(all_fields[field_index]->field->number()) ==
              field_number) {
        const auto& field = all_fields[field_index++];
        os << "      { .offset = " << field->offset << ", .id = " << field->id
           << " },\n";
      } else {
        os << "      {},\n";
      }
    }
    os << "    },\n";
  }

  if (sparse_count != 0) {
    os << "    .sparse_fields = {\n";
    for (size_t i = 0; i < all_fields.size(); ++i) {
      if (has_dense_range && i >= dense_begin && i <= dense_end) {
        continue;
      }
      const auto& field = all_fields[i];
      os << "      { .number = " << field->field->number()
         << ", .offset = " << field->offset << ", .id = " << field->id
         << " },\n";
    }
    os << "    },\n";
  }
  os << "  };\n";
}

void MessageGenerator::GenerateClear(std::ostream& os, bool decl) {
  if (decl) {
    os << "  void Clear() override;\n";
    return;
  }
  os << "void " << MessageName(message_) << "::Clear() {\n";
  for (auto& field : fields_) {
    os << "  " << field->member_name << ".Clear();\n";
  }
  for (auto& [oneof, u] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      os << "  " << u->member_name << ".Clear<" << i << ">();\n";
    }
  }
  os << "}\n\n";
}

void MessageGenerator::GenerateProtobufAccessors(std::ostream& os) {
  if (IsRosFrontend()) {
    return;
  }
  // Generate field accessors.
  GenerateFieldProtobufAccessors(os);
  // Union accessors.
  GenerateUnionProtobufAccessors(os);
}

void MessageGenerator::GenerateFieldProtobufAccessors(std::ostream& os) {
  for (auto& field : fields_) {
    GenerateFieldProtobufAccessors(field, nullptr, -1, os);
  }
}

void MessageGenerator::GenerateFieldProtobufAccessors(
    std::shared_ptr<FieldInfo> field, std::shared_ptr<UnionInfo> union_field,
    int union_index, std::ostream& os) {
  std::string field_name(field->field->name());
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
                  google::protobuf::FieldDescriptor::TYPE_FIXED64 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_FLOAT ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_DOUBLE
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
        os << "  void reserve_" << field_name << "(size_t num) {\n";
        os << "    " << member_name << ".reserve(num);\n";
        os << "  }\n";
        os << "  void resize_" << field_name << "(size_t num) {\n";
        os << "    " << member_name << ".resize(num);\n";
        os << "  }\n";

        // Strings have different accessors from primitive fields.
        if (field->field->type() ==
                google::protobuf::FieldDescriptor::TYPE_STRING ||
            field->field->type() ==
                google::protobuf::FieldDescriptor::TYPE_BYTES) {
          os << "  template <typename Str>\n";
          os << "  void add_" << field_name << "(Str value) {\n";
          os << "    " << member_name << ".Add(value);\n";
          os << "  }\n";
          os << "  template <typename Str>\n";
          os << "  void set_" << field_name << "(size_t index, Str value) {\n";
          os << "    " << member_name << ".Set(index, value);\n";
          os << "  }\n";
          os << "  const ::phaser::StringVectorField& " << sanitized_field_name
             << "() const {\n";
          os << "    return " << member_name << ";\n";
          os << "  }\n";
        } else {
          os << "  void add_" << field_name << "(" << field->c_type
             << " value) {\n";
          os << "    " << member_name << ".Add(value);\n";
          os << "  }\n";
          os << "  void set_" << field_name << "(size_t index, "
             << field->c_type << " value) {\n";
          os << "    " << member_name << ".Set(index, value);\n";
          os << "  }\n";
          os << "  absl::Span<" << field->c_type << ">" << field_name
             << "_as_mutable_span() {\n";
          os << "    return " << member_name << ".AsMutableSpan();\n";
          os << "  }\n";
          os << "  absl::Span<const " << field->c_type << ">" << field_name
             << "_as_span() const {\n";
          os << "    return " << member_name << ".AsSpan();\n";
          os << "  }\n";
          if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_ENUM) {
            os << "  const ::phaser::EnumVectorField<" << field->c_type << ", "
               << EnumName(field->field->enum_type()) << "Stringizer, "
               << EnumName(field->field->enum_type()) << "Parser"
               << packed_string << ">& " << sanitized_field_name
               << "() const {\n";
            os << "    return " << member_name << ";\n";
            os << "  }\n";
          } else {
            os << "  const ::phaser::PrimitiveVectorField<" << field->c_type
               << fixed_size_string << signed_string << packed_string << ">& "
               << sanitized_field_name << "() const {\n";
            os << "    return " << member_name << ";\n";
            os << "  }\n";
          }
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
        os << "  " << field->c_type << " mutable_" << field_name
           << "(size_t index) {\n";
        os << "    return " << member_name << ".Mutable(index);\n";
        os << "  }\n";
        os << "  " << field->c_type << " add_" << field_name << "() {\n";
        os << "    return " << member_name << ".Add();\n";
        os << "  }\n";
        os << "  const ::phaser::MessageVectorField<" << field->c_type << ">& "
           << sanitized_field_name << "() const {\n";
        os << "    return " << member_name << ";\n";
        os << "  }\n";
        os << "  void reserve_" << field_name << "(size_t num) {\n";
        os << "    " << member_name << ".reserve(num);\n";
        os << "  }\n";
        os << "  void resize_" << field_name << "(size_t num) {\n";
        os << "    " << member_name << ".resize(num);\n";
        os << "  }\n";
        os << "  std::vector<" << field->c_type << "> allocate_" << field_name
           << "(size_t n) {\n";
        os << "    return " << member_name << ".Allocate(n);\n";
        os << "  }\n";
        break;
      case google::protobuf::FieldDescriptor::TYPE_GROUP:
        std::cerr << "Groups are not supported\n";
        exit(1);
    }
  } else {
    // Non-repeated fields.
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
             << std::to_string(union_index) << ", " << field->c_type
             << ">();\n";
        }
        os << "  }\n";
        if (union_index == -1) {
          os << "  bool has_" << field_name << "() const {\n";
          os << "    return " << member_name << ".IsPresent();\n";
          os << "  }\n";
        } else {
          os << "  bool has_" << field_name << "() const {\n";
          os << "    return " << member_name << ".template IsPresent<"
             << std::to_string(union_index) << ">();\n";
          os << "  }\n";
        }
        os << "  void clear_" << field_name << "() {\n";
        os << "    " << member_name << ".Clear" << suffix << "();\n";
        os << "  }\n";
        if (field->field->type() ==
                google::protobuf::FieldDescriptor::TYPE_STRING ||
            field->field->type() ==
                google::protobuf::FieldDescriptor::TYPE_BYTES) {
          os << "  template <typename Str>\n";
          os << "  void set_" << field_name << "(Str value) {\n";
          if (union_index != -1) {
            // Clear all other union members.
            for (size_t i = 0; i < union_field->members.size(); i++) {
              if (i != size_t(union_index)) {
                os << "    " << member_name << ".Clear<" << i << ">();\n";
              }
            }
          }
          os << "    " << member_name << ".Set" << suffix << "(value);\n";
          os << "  }\n";
          os << "  absl::Span<char> allocate_" << field_name
             << "(size_t len) {\n";
          os << "    return " << member_name << ".Allocate" << suffix
             << "(len);\n";
          os << "  }\n";
        } else {
          os << "  void set_" << field_name << "(" << field->c_type
             << " value) {\n";
          if (union_index != -1) {
            // Clear all other union members.
            for (size_t i = 0; i < union_field->members.size(); i++) {
              if (i != size_t(union_index)) {
                os << "    " << member_name << ".Clear<" << i << ">();\n";
              }
            }
          }
          os << "    " << member_name << ".Set" << suffix << "(value);\n";
          os << "  }\n";
        }
        break;

      case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
        os << "  void clear_" << field_name << "() {\n";
        os << "    " << member_name << ".Clear" << suffix << "();\n";
        os << "  }\n";
        os << "  void set_" << field_name
           << "(toolbelt::BufferOffset offset) {\n";
        os << "    " << member_name << ".SetOffset" << suffix << "(offset);\n";
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
          // Clear all other union members.
          for (size_t i = 0; i < union_field->members.size(); i++) {
            if (i != size_t(union_index)) {
              os << "    " << member_name << ".Clear<" << i << ">();\n";
            }
          }
          os << "    return " << member_name << ".Mutable<"
             << std::to_string(union_index) << ", "
             << MessageName(field->field->message_type()) << ">();\n";
          os << "  }\n";

          os << "  bool has_" << field_name << "() const {\n";
          os << "    return " << member_name << ".template IsPresent<"
             << std::to_string(union_index) << ">();\n";
          os << "  }\n";
        }
        // Any-typed message fields reuse the standard message accessors above
        // (mutable_X() returns a phaser::AnyMessage, which exposes the full Any
        // API: PackFrom/UnpackTo/Is/etc.), so no field-specific generation is
        // needed here.

        break;
      case google::protobuf::FieldDescriptor::TYPE_GROUP:
        std::cerr << "Groups are not supported\n";
        exit(1);
    }
  }
}

void MessageGenerator::GenerateUnionProtobufAccessors(std::ostream& os) {
  for (auto& [oneof, u] : unions_) {
    os << "\n  // Oneof " << oneof->name() << "\n";
    os << "  int " << oneof->name() << "_case() const {\n";
    os << "    return " << u->member_name << ".Discriminator();\n";
    os << "  }\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      GenerateFieldProtobufAccessors(field, u, int(i), os);
    }
  }
}

void MessageGenerator::GenerateNestedTypes(std::ostream& os) {
  for (auto& msg : nested_message_gens_) {
    os << "  using " << msg->message_->name() << " = "
       << MessageName(msg->message_) << ";\n";
  }

  for (auto& enum_gen : enum_gens_) {
    os << "  using " << enum_gen->enum_->name() << " = "
       << EnumName(enum_gen->enum_) << ";\n";
    // Generate enum constant aliases.
    for (int i = 0; i < enum_gen->enum_->value_count(); i++) {
      const google::protobuf::EnumValueDescriptor* value =
          enum_gen->enum_->value(i);
      os << "  static constexpr " << enum_gen->enum_->name() << " "
         << value->name() << " = " << EnumName(enum_gen->enum_) << "_"
         << value->name() << ";\n";
    }
  }
}

void MessageGenerator::GenerateFieldNumbers(std::ostream& os) {
  for (auto& field : fields_) {
    std::string name(field->field->camelcase_name());
    name = absl::StrFormat("k%c%s", toupper(name[0]), name.substr(1));
    os << "  static constexpr int " << name
       << "FieldNumber = " << field->field->number() << ";\n";
  }
  for (auto& [oneof, u] : unions_) {
    for (auto& field : u->members) {
      std::string name(field->field->camelcase_name());
      name = absl::StrFormat("k%c%s", toupper(name[0]), name.substr(1));
      os << "  static constexpr int " << name
         << "FieldNumber = " << field->field->number() << ";\n";
    }
  }
}

std::string MessageGenerator::ROSFieldValueExpression(
    const std::shared_ptr<FieldInfo>& field,
    const std::shared_ptr<UnionInfo>& union_field, int union_index) const {
  if (union_field == nullptr) {
    return field->member_name + ".Get()";
  }
  if (field->field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    return union_field->member_name + ".template GetReference<" +
           std::to_string(union_index) + ", " + field->c_type + ">()";
  }
  return union_field->member_name + ".template GetValue<" +
         std::to_string(union_index) + ", " + field->c_type + ">()";
}

static std::string ROSBulkPrimitiveType(
    const google::protobuf::FieldDescriptor* field) {
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
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      return "";
  }
  return "";
}

void MessageGenerator::GenerateROSFieldSize(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value_expression, const std::string& indent) {
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      os << indent << "_phaser_serialized_size += 4;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      os << indent << "_phaser_serialized_size += 8;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      os << indent << "_phaser_serialized_size += 1;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << indent << "_phaser_serialized_size += 4 + (" << value_expression
         << ").size();\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (IsAny(field)) {
        // Any has no static ROS1 type. An absent Any is represented by its two
        // empty declared fields; a populated Any is rejected by the writer.
        os << indent << "_phaser_serialized_size += 8;\n";
      } else if (IsRosFrontend() && IsRosTime(field->message_type())) {
        os << indent << "_phaser_serialized_size += 8;\n";
      } else if (IsRosFrontend() && IsRosDuration(field->message_type())) {
        os << indent << "_phaser_serialized_size += 8;\n";
      } else if (IsRosFrontend() && IsRosHeader(field->message_type())) {
        os << indent << "_phaser_serialized_size += 16 + (" << value_expression
           << ").frame_id.size();\n";
      } else {
        os << indent << "_phaser_serialized_size += (" << value_expression
           << ").ROSSerializedSize();\n";
      }
      return;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      abort();
  }
  abort();
}

void MessageGenerator::GenerateROSFieldWrite(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value_expression, const std::string& indent) {
  auto write = [&](const std::string& expression) {
    os << indent << "if (absl::Status _phaser_status = _phaser_buffer.Write("
       << expression << "); !_phaser_status.ok()) return _phaser_status;\n";
  };
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      write("static_cast<int32_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      write("static_cast<int64_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      write("static_cast<uint32_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      write("static_cast<uint64_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      write("static_cast<double>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      write("static_cast<float>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      write("static_cast<bool>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      write("static_cast<int32_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << indent
         << "if (absl::Status _phaser_status = _phaser_buffer.WriteString("
         << value_expression
         << "); !_phaser_status.ok()) return _phaser_status;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (IsAny(field)) {
        os << indent << "if ((" << value_expression << ").has_type_url() || ("
           << value_expression << ").has_value()) {\n";
        os << indent
           << "  return absl::UnimplementedError(\"ROS1 serialization of a "
              "populated google.protobuf.Any is unsupported\");\n";
        os << indent << "}\n";
        os << indent
           << "if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteString({}); "
              "!_phaser_status.ok()) return _phaser_status;\n";
        os << indent
           << "if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteString({}); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      } else if (IsRosFrontend() && IsRosTime(field->message_type())) {
        write("static_cast<uint32_t>((" + value_expression + ").sec)");
        write("static_cast<uint32_t>((" + value_expression + ").nsec)");
      } else if (IsRosFrontend() && IsRosDuration(field->message_type())) {
        write("static_cast<int32_t>((" + value_expression + ").sec)");
        write("static_cast<int32_t>((" + value_expression + ").nsec)");
      } else if (IsRosFrontend() && IsRosHeader(field->message_type())) {
        write("static_cast<uint32_t>((" + value_expression + ").seq)");
        write("static_cast<uint32_t>((" + value_expression + ").stamp.sec)");
        write("static_cast<uint32_t>((" + value_expression + ").stamp.nsec)");
        os << indent
           << "if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteString(("
           << value_expression
           << ").frame_id); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << indent << "if (absl::Status _phaser_status = ("
           << value_expression
           << ").SerializeToROS(_phaser_buffer); !_phaser_status.ok()) return "
              "_phaser_status;\n";
      }
      return;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      abort();
  }
  abort();
}

void MessageGenerator::GenerateROSFieldRead(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& target_expression, const std::string& indent,
    bool append, const std::string& index_expression, int union_index) {
  auto set_value = [&](const std::string& value) {
    if (union_index >= 0) {
      os << indent << target_expression << ".template Set<" << union_index
         << ">(" << value << ");\n";
    } else if (!index_expression.empty()) {
      os << indent << target_expression << ".Set(" << index_expression << ", "
         << value << ");\n";
    } else if (append) {
      os << indent << target_expression << ".Add(" << value << ");\n";
    } else {
      os << indent << target_expression << ".Set(" << value << ");\n";
    }
  };
  auto mutable_message = [&]() {
    if (union_index >= 0) {
      return target_expression + ".template Mutable<" +
             std::to_string(union_index) + ", " +
             MessageName(field->message_type(), true) + ">()";
    }
    if (!index_expression.empty()) {
      return target_expression + ".Mutable(" + index_expression + ")";
    }
    if (append) {
      return target_expression + ".Add()";
    }
    return target_expression + ".Mutable()";
  };
  auto read_value = [&](const std::string& type,
                        const std::string& conversion = "") {
    os << indent << "{\n";
    os << indent << "  absl::StatusOr<" << type
       << "> _phaser_value = _phaser_buffer.Read<" << type << ">();\n";
    os << indent
       << "  if (!_phaser_value.ok()) return _phaser_value.status();\n";
    const std::string value =
        conversion.empty() ? "*_phaser_value" : conversion + "(*_phaser_value)";
    set_value(value);
    os << indent << "}\n";
  };

  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      read_value("int32_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      read_value("int64_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      read_value("uint32_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      read_value("uint64_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      read_value("double");
      return;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      read_value("float");
      return;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      read_value("bool");
      return;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      read_value("int32_t",
                 "static_cast<" + EnumName(field->enum_type()) + ">");
      return;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << indent << "{\n";
      os << indent
         << "  absl::StatusOr<std::string_view> _phaser_value = "
            "_phaser_buffer.ReadString();\n";
      os << indent
         << "  if (!_phaser_value.ok()) return _phaser_value.status();\n";
      set_value("*_phaser_value");
      os << indent << "}\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (IsAny(field)) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<std::string_view> _phaser_type_url = "
              "_phaser_buffer.ReadString();\n";
        os << indent
           << "  if (!_phaser_type_url.ok()) return "
              "_phaser_type_url.status();\n";
        os << indent
           << "  absl::StatusOr<std::string_view> _phaser_any_value = "
              "_phaser_buffer.ReadString();\n";
        os << indent
           << "  if (!_phaser_any_value.ok()) return "
              "_phaser_any_value.status();\n";
        os << indent
           << "  if (!_phaser_type_url->empty() || "
              "!_phaser_any_value->empty()) {\n";
        os << indent
           << "    return absl::UnimplementedError(\"ROS1 deserialization of "
              "a populated google.protobuf.Any is unsupported\");\n";
        os << indent << "  }\n";
        os << indent << "  " << mutable_message() << "->Clear();\n";
        os << indent << "}\n";
      } else if (IsRosFrontend() && IsRosTime(field->message_type())) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_sec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_nsec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
        os << indent << "  ::ros::Time _phaser_value;\n";
        os << indent << "  _phaser_value.sec = *_phaser_sec;\n";
        os << indent << "  _phaser_value.nsec = *_phaser_nsec;\n";
        set_value("_phaser_value");
        os << indent << "}\n";
      } else if (IsRosFrontend() && IsRosDuration(field->message_type())) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<int32_t> _phaser_sec = "
              "_phaser_buffer.Read<int32_t>();\n";
        os << indent
           << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
        os << indent
           << "  absl::StatusOr<int32_t> _phaser_nsec = "
              "_phaser_buffer.Read<int32_t>();\n";
        os << indent
           << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
        os << indent << "  ::ros::Duration _phaser_value;\n";
        os << indent << "  _phaser_value.sec = *_phaser_sec;\n";
        os << indent << "  _phaser_value.nsec = *_phaser_nsec;\n";
        set_value("_phaser_value");
        os << indent << "}\n";
      } else if (IsRosFrontend() && IsRosHeader(field->message_type())) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_seq = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_seq.ok()) return _phaser_seq.status();\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_sec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_nsec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
        os << indent
           << "  absl::StatusOr<std::string_view> _phaser_frame_id = "
              "_phaser_buffer.ReadString();\n";
        os << indent
           << "  if (!_phaser_frame_id.ok()) return "
              "_phaser_frame_id.status();\n";
        if (union_index < 0 && !append && index_expression.empty()) {
          os << indent << "  auto _phaser_value = " << target_expression
             << ".Mutable();\n";
          os << indent << "  _phaser_value.seq = *_phaser_seq;\n";
          os << indent << "  _phaser_value.stamp.sec = *_phaser_sec;\n";
          os << indent << "  _phaser_value.stamp.nsec = *_phaser_nsec;\n";
          os << indent << "  _phaser_value.frame_id = *_phaser_frame_id;\n";
        } else {
          os << indent << "  auto _phaser_value = " << mutable_message()
             << ";\n";
          os << indent << "  _phaser_value->seq = *_phaser_seq;\n";
          os << indent << "  _phaser_value->stamp = "
             << "::ros::Time(*_phaser_sec, *_phaser_nsec);\n";
          os << indent << "  _phaser_value->frame_id = *_phaser_frame_id;\n";
          os << indent << "  _phaser_value->SyncToPayload();\n";
        }
        os << indent << "}\n";
      } else {
        os << indent << "{\n";
        os << indent << "  auto _phaser_message = " << mutable_message()
           << ";\n";
        os << indent
           << "  if (absl::Status _phaser_status = "
              "_phaser_message->DeserializeFromROS(_phaser_buffer); "
              "!_phaser_status.ok()) "
              "return _phaser_status;\n";
        os << indent << "}\n";
      }
      return;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      abort();
  }
  abort();
}

std::string MessageGenerator::DirectProtobufValueType(
    const google::protobuf::FieldDescriptor* field) const {
  using Field = google::protobuf::FieldDescriptor;
  switch (field->type()) {
    case Field::TYPE_INT32:
    case Field::TYPE_SINT32:
    case Field::TYPE_SFIXED32:
    case Field::TYPE_ENUM:
      return "int32_t";
    case Field::TYPE_INT64:
    case Field::TYPE_SINT64:
    case Field::TYPE_SFIXED64:
      return "int64_t";
    case Field::TYPE_UINT32:
    case Field::TYPE_FIXED32:
      return "uint32_t";
    case Field::TYPE_UINT64:
    case Field::TYPE_FIXED64:
      return "uint64_t";
    case Field::TYPE_FLOAT:
      return "float";
    case Field::TYPE_DOUBLE:
      return "double";
    case Field::TYPE_BOOL:
      return "bool";
    case Field::TYPE_STRING:
    case Field::TYPE_BYTES:
    case Field::TYPE_MESSAGE:
      return "std::string_view";
    case Field::TYPE_GROUP:
      break;
  }
  abort();
}

void MessageGenerator::GenerateDirectProtobufReadValue(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& buffer, const std::string& value,
    const std::string& indent) {
  using Field = google::protobuf::FieldDescriptor;
  const std::string type = DirectProtobufValueType(field);
  if (field->type() == Field::TYPE_STRING ||
      field->type() == Field::TYPE_BYTES) {
    os << indent << "absl::StatusOr<std::string_view> ros_parsed = " << buffer
       << ".DeserializeString();\n";
    os << indent << "if (!ros_parsed.ok()) return ros_parsed.status();\n";
    os << indent << value << " = *ros_parsed;\n";
    return;
  }
  if (field->type() == Field::TYPE_MESSAGE) {
    os << indent << "absl::StatusOr<absl::Span<char>> ros_parsed = " << buffer
       << ".DeserializeLengthDelimited();\n";
    os << indent << "if (!ros_parsed.ok()) return ros_parsed.status();\n";
    os << indent << value
       << " = std::string_view(ros_parsed->data(), ros_parsed->size());\n";
    return;
  }

  const bool fixed = field->type() == Field::TYPE_FIXED32 ||
                     field->type() == Field::TYPE_SFIXED32 ||
                     field->type() == Field::TYPE_FLOAT ||
                     field->type() == Field::TYPE_FIXED64 ||
                     field->type() == Field::TYPE_SFIXED64 ||
                     field->type() == Field::TYPE_DOUBLE;
  const bool is_signed = field->type() == Field::TYPE_SINT32 ||
                         field->type() == Field::TYPE_SINT64;
  os << indent << "absl::StatusOr<" << type << "> ros_parsed = " << buffer
     << (fixed ? ".DeserializeFixed<" : ".DeserializeVarint<") << type;
  if (fixed) {
    os << ">();\n";
  } else {
    os << ", " << (is_signed ? "true" : "false") << ">();\n";
  }
  os << indent << "if (!ros_parsed.ok()) return ros_parsed.status();\n";
  os << indent << value << " = *ros_parsed;\n";
}

void MessageGenerator::GenerateDirectROSWriteValue(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value, const std::string& indent) {
  using Field = google::protobuf::FieldDescriptor;
  switch (field->type()) {
    case Field::TYPE_STRING:
    case Field::TYPE_BYTES:
      os << indent << "if (absl::Status status = output.WriteString(" << value
         << "); !status.ok()) return status;\n";
      return;
    case Field::TYPE_MESSAGE:
      if (IsAny(field)) {
        os << indent << "{\n";
        os << indent << "  ::phaser::ProtoBuffer any_scan(" << value << ");\n";
        os << indent << "  while (!any_scan.Eof()) {\n";
        os << indent
           << "    absl::StatusOr<uint32_t> any_tag = "
              "any_scan.DeserializeVarint<uint32_t, false>();\n";
        os << indent << "    if (!any_tag.ok()) return any_tag.status();\n";
        os << indent
           << "    const uint32_t any_number = "
              "*any_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
        os << indent << "    if (any_number == 1 || any_number == 2) {\n";
        os << indent
           << "      return absl::UnimplementedError(\"ROS1 serialization of "
              "a populated google.protobuf.Any is unsupported\");\n";
        os << indent << "    }\n";
        os << indent
           << "    if (absl::Status status = any_scan.SkipTag(*any_tag); "
              "!status.ok()) return status;\n";
        os << indent << "  }\n";
        os << indent
           << "  if (absl::Status status = output.WriteString({}); "
              "!status.ok()) return status;\n";
        os << indent
           << "  if (absl::Status status = output.WriteString({}); "
              "!status.ok()) return status;\n";
        os << indent << "}\n";
      } else {
        os << indent << "if (absl::Status status = "
           << MessageName(field->message_type(), true) << "::ProtobufWireToROS("
           << value << ", output); !status.ok()) return status;\n";
      }
      return;
    case Field::TYPE_ENUM:
      os << indent
         << "if (absl::Status status = output.Write(static_cast<int32_t>("
         << value << ")); !status.ok()) return status;\n";
      return;
    default:
      os << indent << "if (absl::Status status = output.Write(" << value
         << "); !status.ok()) return status;\n";
      return;
  }
}

void MessageGenerator::GenerateDirectProtobufSingularField(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& indent) {
  const std::string type = DirectProtobufValueType(field);
  os << indent << "{\n";
  os << indent << "  " << type << " ros_value{};\n";
  os << indent << "  ::phaser::ProtoBuffer ros_scan(protobuf);\n";
  os << indent << "  while (!ros_scan.Eof()) {\n";
  os << indent
     << "    absl::StatusOr<uint32_t> ros_tag = "
        "ros_scan.DeserializeVarint<uint32_t, false>();\n";
  os << indent << "    if (!ros_tag.ok()) return ros_tag.status();\n";
  os << indent
     << "    const uint32_t ros_number = "
        "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
  os << indent << "    if (ros_number == " << field->number() << ") {\n";
  os << indent << "      {\n";
  GenerateDirectProtobufReadValue(os, field, "ros_scan", "ros_value",
                                  indent + "        ");
  os << indent << "      }\n";
  os << indent << "    } else {\n";
  os << indent
     << "      if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
        "!status.ok()) return status;\n";
  os << indent << "    }\n";
  os << indent << "  }\n";
  GenerateDirectROSWriteValue(os, field, "ros_value", indent + "  ");
  os << indent << "}\n";
}

void MessageGenerator::GenerateDirectProtobufField(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& indent) {
  if (!field->is_repeated()) {
    GenerateDirectProtobufSingularField(os, field, indent);
    return;
  }

  const std::string type = DirectProtobufValueType(field);
  const int fixed_extent = GetArraySize(field);
  const bool fixed_wire_type = IsFixedWireType(field);
  os << indent << "{\n";
  if (fixed_extent <= 0) {
    os << indent << "  size_t ros_count = 0;\n";
    os << indent << "  ::phaser::ProtoBuffer ros_count_scan(protobuf);\n";
    os << indent << "  while (!ros_count_scan.Eof()) {\n";
    os << indent
       << "    absl::StatusOr<uint32_t> ros_tag = "
          "ros_count_scan.DeserializeVarint<uint32_t, false>();\n";
    os << indent << "    if (!ros_tag.ok()) return ros_tag.status();\n";
    os << indent
       << "    const uint32_t ros_number = "
          "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
    os << indent << "    if (ros_number == " << field->number() << ") {\n";
    if (field->is_packable()) {
      os << indent
         << "      if ((*ros_tag & 7u) == "
            "static_cast<uint32_t>(::phaser::WireType::kLengthDelimited)) {\n";
      os << indent
         << "        absl::StatusOr<absl::Span<char>> ros_packed = "
            "ros_count_scan.DeserializeLengthDelimited();\n";
      os << indent
         << "        if (!ros_packed.ok()) return ros_packed.status();\n";
      if (fixed_wire_type) {
        os << indent << "        if (ros_packed->size() % sizeof(" << type
           << ") != 0) {\n";
        os << indent
           << "          return absl::InvalidArgumentError("
              "\"packed fixed-width field has a partial element\");\n";
        os << indent << "        }\n";
        os << indent << "        ros_count += ros_packed->size() / sizeof("
           << type << ");\n";
      } else {
        os << indent
           << "        ::phaser::ProtoBuffer ros_values(*ros_packed);\n";
        os << indent << "        while (!ros_values.Eof()) {\n";
        os << indent << "          " << type << " ros_ignored{};\n";
        os << indent << "          {\n";
        GenerateDirectProtobufReadValue(os, field, "ros_values", "ros_ignored",
                                        indent + "            ");
        os << indent << "          }\n";
        os << indent << "          ++ros_count;\n";
        os << indent << "        }\n";
      }
      os << indent << "      } else {\n";
      os << indent << "        " << type << " ros_ignored{};\n";
      os << indent << "        {\n";
      GenerateDirectProtobufReadValue(os, field, "ros_count_scan",
                                      "ros_ignored", indent + "          ");
      os << indent << "        }\n";
      os << indent << "        ++ros_count;\n";
      os << indent << "      }\n";
    } else {
      os << indent << "      " << type << " ros_ignored{};\n";
      os << indent << "      {\n";
      GenerateDirectProtobufReadValue(os, field, "ros_count_scan",
                                      "ros_ignored", indent + "        ");
      os << indent << "      }\n";
      os << indent << "      ++ros_count;\n";
    }
    os << indent << "    } else {\n";
    os << indent
       << "      if (absl::Status status = "
          "ros_count_scan.SkipTag(*ros_tag); !status.ok()) return status;\n";
    os << indent << "    }\n";
    os << indent << "  }\n";
    os << indent
       << "  if (absl::Status status = output.WriteSequenceLength(ros_count); "
          "!status.ok()) return status;\n";
  }

  os << indent << "  size_t ros_emitted = 0;\n";
  os << indent << "  ::phaser::ProtoBuffer ros_scan(protobuf);\n";
  os << indent << "  while (!ros_scan.Eof()) {\n";
  os << indent
     << "    absl::StatusOr<uint32_t> ros_tag = "
        "ros_scan.DeserializeVarint<uint32_t, false>();\n";
  os << indent << "    if (!ros_tag.ok()) return ros_tag.status();\n";
  os << indent
     << "    const uint32_t ros_number = "
        "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
  os << indent << "    if (ros_number == " << field->number() << ") {\n";
  auto emit_value = [&](const std::string& buffer,
                        const std::string& emit_indent) {
    os << emit_indent << type << " ros_value{};\n";
    os << emit_indent << "{\n";
    GenerateDirectProtobufReadValue(os, field, buffer, "ros_value",
                                    emit_indent + "  ");
    os << emit_indent << "}\n";
    if (fixed_extent > 0) {
      os << emit_indent << "if (ros_emitted >= " << fixed_extent << ") {\n";
      os << emit_indent
         << "  return absl::InvalidArgumentError("
            "\"protobuf input exceeds fixed ROS array extent\");\n";
      os << emit_indent << "}\n";
    }
    GenerateDirectROSWriteValue(os, field, "ros_value", emit_indent);
    os << emit_indent << "++ros_emitted;\n";
  };
  if (field->is_packable()) {
    os << indent
       << "      if ((*ros_tag & 7u) == "
          "static_cast<uint32_t>(::phaser::WireType::kLengthDelimited)) {\n";
    os << indent
       << "        absl::StatusOr<absl::Span<char>> ros_packed = "
          "ros_scan.DeserializeLengthDelimited();\n";
    os << indent
       << "        if (!ros_packed.ok()) return ros_packed.status();\n";
    if (fixed_wire_type) {
      os << indent << "        if (ros_packed->size() % sizeof(" << type
         << ") != 0) {\n";
      os << indent
         << "          return absl::InvalidArgumentError("
            "\"packed fixed-width field has a partial element\");\n";
      os << indent << "        }\n";
      os << indent << "        const size_t ros_packed_count = "
         << "ros_packed->size() / sizeof(" << type << ");\n";
      if (fixed_extent > 0) {
        os << indent << "        if (ros_packed_count > " << fixed_extent
           << " - ros_emitted) {\n";
        os << indent
           << "          return absl::InvalidArgumentError("
              "\"protobuf input exceeds fixed ROS array extent\");\n";
        os << indent << "        }\n";
      }
      os << indent
         << "        if (absl::Status status = output.WriteRaw("
            "ros_packed->data(), ros_packed->size()); !status.ok()) "
            "return status;\n";
      os << indent << "        ros_emitted += ros_packed_count;\n";
    } else {
      os << indent
         << "        ::phaser::ProtoBuffer ros_values(*ros_packed);\n";
      os << indent << "        while (!ros_values.Eof()) {\n";
      emit_value("ros_values", indent + "          ");
      os << indent << "        }\n";
    }
    os << indent << "      } else {\n";
    emit_value("ros_scan", indent + "        ");
    os << indent << "      }\n";
  } else {
    emit_value("ros_scan", indent + "      ");
  }
  os << indent << "    } else {\n";
  os << indent
     << "      if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
        "!status.ok()) return status;\n";
  os << indent << "    }\n";
  os << indent << "  }\n";
  if (fixed_extent > 0) {
    os << indent << "  while (ros_emitted < " << fixed_extent << ") {\n";
    os << indent << "    " << type << " ros_value{};\n";
    GenerateDirectROSWriteValue(os, field, "ros_value", indent + "    ");
    os << indent << "    ++ros_emitted;\n";
    os << indent << "  }\n";
  }
  os << indent << "}\n";
}

void MessageGenerator::GenerateDirectProtobufToROS(std::ostream& os) {
  const std::string name = MessageName(message_);
  os << "absl::Status " << name
     << "::ProtobufWireToROS(std::string_view protobuf, "
        "::phaser::ROSBuffer& output) {\n";
  if (IsRosTime(message_) || IsRosDuration(message_)) {
    os << "  int64_t ros_seconds = 0;\n";
    os << "  int32_t ros_nanos = 0;\n";
    os << "  ::phaser::ProtoBuffer ros_scan(protobuf);\n";
    os << "  while (!ros_scan.Eof()) {\n";
    os << "    absl::StatusOr<uint32_t> ros_tag = "
          "ros_scan.DeserializeVarint<uint32_t, false>();\n";
    os << "    if (!ros_tag.ok()) return ros_tag.status();\n";
    os << "    switch (*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift) {\n";
    os << "      case 1: {\n";
    os << "        absl::StatusOr<int64_t> value = "
          "ros_scan.DeserializeVarint<int64_t, false>();\n";
    os << "        if (!value.ok()) return value.status();\n";
    os << "        ros_seconds = *value;\n";
    os << "        break;\n";
    os << "      }\n";
    os << "      case 2: {\n";
    os << "        absl::StatusOr<int32_t> value = "
          "ros_scan.DeserializeVarint<int32_t, false>();\n";
    os << "        if (!value.ok()) return value.status();\n";
    os << "        ros_nanos = *value;\n";
    os << "        break;\n";
    os << "      }\n";
    os << "      default:\n";
    os << "        if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
          "!status.ok()) return status;\n";
    os << "    }\n";
    os << "  }\n";
    const std::string wire_type = IsRosTime(message_) ? "uint32_t" : "int32_t";
    os << "  if (absl::Status status = output.Write(static_cast<" << wire_type
       << ">(ros_seconds)); !status.ok()) return status;\n";
    os << "  if (absl::Status status = output.Write(static_cast<" << wire_type
       << ">(ros_nanos)); !status.ok()) return status;\n";
  } else {
    for (const auto& item : fields_in_order_) {
      if (!item->IsUnion()) {
        GenerateDirectProtobufField(os, item->field, "  ");
        continue;
      }
      auto union_info = std::static_pointer_cast<UnionInfo>(item);
      os << "  {\n";
      os << "    uint32_t ros_discriminator = 0;\n";
      os << "    ::phaser::ProtoBuffer ros_scan(protobuf);\n";
      os << "    while (!ros_scan.Eof()) {\n";
      os << "      absl::StatusOr<uint32_t> ros_tag = "
            "ros_scan.DeserializeVarint<uint32_t, false>();\n";
      os << "      if (!ros_tag.ok()) return ros_tag.status();\n";
      os << "      const uint32_t ros_number = "
            "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
      os << "      switch (ros_number) {\n";
      for (const auto& member : union_info->members) {
        os << "        case " << member->field->number() << ":\n";
        os << "          ros_discriminator = ros_number;\n";
        os << "          break;\n";
      }
      os << "        default:\n";
      os << "          break;\n";
      os << "      }\n";
      os << "      if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
            "!status.ok()) return status;\n";
      os << "    }\n";
      os << "    if (absl::Status status = output.Write(ros_discriminator); "
            "!status.ok()) return status;\n";
      os << "    switch (ros_discriminator) {\n";
      for (const auto& member : union_info->members) {
        os << "      case " << member->field->number() << ":\n";
        GenerateDirectProtobufSingularField(os, member->field, "        ");
        os << "        break;\n";
      }
      os << "      default:\n";
      os << "        break;\n";
      os << "    }\n";
      os << "  }\n";
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateDirectROSReadValue(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& reader, const std::string& value,
    const std::string& indent) {
  using Field = google::protobuf::FieldDescriptor;
  if (field->type() == Field::TYPE_STRING ||
      field->type() == Field::TYPE_BYTES) {
    os << indent << "absl::StatusOr<std::string_view> ros_value = " << reader
       << ".ReadString();\n";
    os << indent << "if (!ros_value.ok()) return ros_value.status();\n";
    os << indent << value << " = *ros_value;\n";
    return;
  }
  if (field->type() == Field::TYPE_MESSAGE) {
    abort();
  }
  const std::string type = DirectProtobufValueType(field);
  os << indent << "absl::StatusOr<" << type << "> ros_value = " << reader
     << ".Read<" << type << ">();\n";
  os << indent << "if (!ros_value.ok()) return ros_value.status();\n";
  os << indent << value << " = *ros_value;\n";
}

void MessageGenerator::GenerateDirectProtoWriteValue(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value, const std::string& field_number,
    const std::string& indent, bool raw) {
  using Field = google::protobuf::FieldDescriptor;
  const std::string type = DirectProtobufValueType(field);
  if (field->type() == Field::TYPE_STRING ||
      field->type() == Field::TYPE_BYTES) {
    os << indent << "if (absl::Status status = output.SerializeLengthDelimited("
       << field_number << ", " << value << ".data(), " << value
       << ".size()); !status.ok()) return status;\n";
    return;
  }
  if (field->type() == Field::TYPE_MESSAGE) {
    abort();
  }
  const bool fixed = field->type() == Field::TYPE_FIXED32 ||
                     field->type() == Field::TYPE_SFIXED32 ||
                     field->type() == Field::TYPE_FLOAT ||
                     field->type() == Field::TYPE_FIXED64 ||
                     field->type() == Field::TYPE_SFIXED64 ||
                     field->type() == Field::TYPE_DOUBLE;
  const bool is_signed = field->type() == Field::TYPE_SINT32 ||
                         field->type() == Field::TYPE_SINT64;
  if (raw && fixed) {
    os << indent << "if (absl::Status status = output.SerializeRaw(&" << value
       << ", sizeof(" << value << ")); !status.ok()) return status;\n";
    return;
  }
  os << indent << "if (absl::Status status = output.";
  if (fixed) {
    os << "SerializeFixed(" << field_number << ", " << value << ")";
  } else if (raw) {
    os << "SerializeRawVarint<" << type << ", "
       << (is_signed ? "true" : "false") << ">(" << value << ")";
  } else {
    os << "SerializeVarint<" << type << ", " << (is_signed ? "true" : "false")
       << ">(" << field_number << ", " << value << ")";
  }
  os << "; !status.ok()) return status;\n";
}

void MessageGenerator::GenerateDirectROSFieldToProtobuf(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& indent) {
  const std::string type = DirectProtobufValueType(field);
  const int fixed_extent = GetArraySize(field);
  const bool fixed_wire_type = IsFixedWireType(field);
  const std::string count =
      fixed_extent > 0 ? std::to_string(fixed_extent) : "ros_count";

  os << indent << "{\n";
  if (field->is_repeated() && fixed_extent <= 0) {
    os << indent
       << "  absl::StatusOr<uint32_t> ros_count_value = "
          "ros.ReadSequenceLength();\n";
    os << indent
       << "  if (!ros_count_value.ok()) return ros_count_value.status();\n";
    os << indent << "  const size_t ros_count = *ros_count_value;\n";
  }

  auto emit_one = [&](const std::string& reader, const std::string& writer,
                      const std::string& emit_indent, bool raw) {
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      if (IsAny(field)) {
        os << emit_indent
           << "absl::StatusOr<std::string_view> ros_type_url = " << reader
           << ".ReadString();\n";
        os << emit_indent
           << "if (!ros_type_url.ok()) return ros_type_url.status();\n";
        os << emit_indent
           << "absl::StatusOr<std::string_view> ros_any_value = " << reader
           << ".ReadString();\n";
        os << emit_indent
           << "if (!ros_any_value.ok()) return ros_any_value.status();\n";
        os << emit_indent
           << "if (!ros_type_url->empty() || !ros_any_value->empty()) {\n";
        os << emit_indent
           << "  return absl::UnimplementedError(\"ROS1 conversion of a "
              "populated google.protobuf.Any is unsupported\");\n";
        os << emit_indent << "}\n";
        os << emit_indent << "if (absl::Status status = " << writer
           << ".SerializeLengthDelimitedHeader(" << field->number()
           << ", 0); !status.ok()) return status;\n";
      } else {
        const std::string nested = MessageName(field->message_type(), true);
        os << emit_indent << "::phaser::ROSReader ros_size_reader = " << reader
           << ";\n";
        os << emit_indent << "::phaser::ProtoWriter ros_size_output;\n";
        os << emit_indent << "if (absl::Status status = " << nested
           << "::ROSReaderToProtobuf(ros_size_reader, ros_size_output); "
              "!status.ok()) return status;\n";
        os << emit_indent << "if (absl::Status status = " << writer
           << ".SerializeLengthDelimitedHeader(" << field->number()
           << ", ros_size_output.Size()); !status.ok()) return status;\n";
        os << emit_indent << "if (absl::Status status = " << nested
           << "::ROSReaderToProtobuf(" << reader << ", " << writer
           << "); !status.ok()) return status;\n";
      }
      return;
    }

    os << emit_indent << type << " ros_field_value{};\n";
    os << emit_indent << "{\n";
    GenerateDirectROSReadValue(os, field, reader, "ros_field_value",
                               emit_indent + "  ");
    os << emit_indent << "}\n";
    const std::string old_output = "output";
    if (writer != old_output) {
      os << emit_indent << "{\n";
      os << emit_indent << "  auto& output = " << writer << ";\n";
      GenerateDirectProtoWriteValue(os, field, "ros_field_value",
                                    std::to_string(field->number()),
                                    emit_indent + "  ", raw);
      os << emit_indent << "}\n";
    } else {
      GenerateDirectProtoWriteValue(os, field, "ros_field_value",
                                    std::to_string(field->number()),
                                    emit_indent, raw);
    }
  };

  if (!field->is_repeated()) {
    emit_one("ros", "output", indent + "  ", false);
    os << indent << "}\n";
    return;
  }

  if (field->is_packable() && field->is_packed() && fixed_wire_type) {
    os << indent << "  const uint64_t ros_packed_byte_size = "
       << "static_cast<uint64_t>(" << count << ") * sizeof(" << type << ");\n";
    os << indent << "  if (ros_packed_byte_size > ros.Remaining()) {\n";
    os << indent
       << "    return absl::InvalidArgumentError("
          "\"truncated ROS packed fixed-width field\");\n";
    os << indent << "  }\n";
    os << indent
       << "  absl::StatusOr<absl::Span<const char>> ros_packed = "
          "ros.ReadRaw(static_cast<size_t>(ros_packed_byte_size));\n";
    os << indent << "  if (!ros_packed.ok()) return ros_packed.status();\n";
    os << indent
       << "  if (absl::Status status = output.SerializeLengthDelimited("
       << field->number()
       << ", ros_packed->data(), ros_packed->size()); !status.ok()) "
          "return status;\n";
  } else if (field->is_packable() && field->is_packed()) {
    os << indent << "  ::phaser::ROSReader ros_packed_reader = ros;\n";
    os << indent << "  ::phaser::ProtoWriter ros_packed_size;\n";
    os << indent << "  for (size_t ros_index = 0; ros_index < " << count
       << "; ++ros_index) {\n";
    emit_one("ros_packed_reader", "ros_packed_size", indent + "    ", true);
    os << indent << "  }\n";
    os << indent
       << "  if (absl::Status status = "
          "output.SerializeLengthDelimitedHeader("
       << field->number()
       << ", ros_packed_size.Size()); !status.ok()) return status;\n";
    os << indent << "  for (size_t ros_index = 0; ros_index < " << count
       << "; ++ros_index) {\n";
    emit_one("ros", "output", indent + "    ", true);
    os << indent << "  }\n";
  } else {
    os << indent << "  for (size_t ros_index = 0; ros_index < " << count
       << "; ++ros_index) {\n";
    emit_one("ros", "output", indent + "    ", false);
    os << indent << "  }\n";
  }
  os << indent << "}\n";
}

void MessageGenerator::GenerateDirectROSToProtobuf(std::ostream& os) {
  const std::string name = MessageName(message_);
  os << "absl::Status " << name
     << "::ROSReaderToProtobuf(::phaser::ROSReader& ros, "
        "::phaser::ProtoWriter& output) {\n";
  if (IsRosTime(message_) || IsRosDuration(message_)) {
    const std::string seconds_type =
        IsRosTime(message_) ? "uint32_t" : "int32_t";
    os << "  absl::StatusOr<" << seconds_type << "> ros_seconds = ros.Read<"
       << seconds_type << ">();\n";
    os << "  if (!ros_seconds.ok()) return ros_seconds.status();\n";
    os << "  absl::StatusOr<" << seconds_type << "> ros_nanos = ros.Read<"
       << seconds_type << ">();\n";
    os << "  if (!ros_nanos.ok()) return ros_nanos.status();\n";
    os << "  if (absl::Status status = "
          "output.SerializeVarint<int64_t, false>(1, "
          "static_cast<int64_t>(*ros_seconds)); !status.ok()) return status;\n";
    os << "  if (absl::Status status = "
          "output.SerializeVarint<int32_t, false>(2, "
          "static_cast<int32_t>(*ros_nanos)); !status.ok()) return status;\n";
  } else {
    for (const auto& item : fields_in_order_) {
      if (!item->IsUnion()) {
        GenerateDirectROSFieldToProtobuf(os, item->field, "  ");
        continue;
      }
      auto union_info = std::static_pointer_cast<UnionInfo>(item);
      os << "  {\n";
      os << "    absl::StatusOr<uint32_t> ros_discriminator = "
            "ros.Read<uint32_t>();\n";
      os << "    if (!ros_discriminator.ok()) return "
            "ros_discriminator.status();\n";
      os << "    switch (*ros_discriminator) {\n";
      os << "      case 0:\n";
      os << "        break;\n";
      for (const auto& member : union_info->members) {
        os << "      case " << member->field->number() << ":\n";
        GenerateDirectROSFieldToProtobuf(os, member->field, "        ");
        os << "        break;\n";
      }
      os << "      default:\n";
      os << "        return absl::InvalidArgumentError("
            "\"Unknown ROS oneof discriminator\");\n";
      os << "    }\n";
      os << "  }\n";
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ROSToProtobuf(absl::Span<const char> ros_bytes, "
        "::phaser::ProtoBuffer& output) {\n";
  os << "  ::phaser::ROSReader ros(ros_bytes);\n";
  os << "  ::phaser::ProtoWriter writer(output);\n";
  os << "  if (absl::Status status = ROSReaderToProtobuf(ros, writer); "
        "!status.ok()) return status;\n";
  os << "  if (!ros.Eof()) return absl::InvalidArgumentError("
        "\"Trailing bytes after ROS message\");\n";
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateROSSerialization(std::ostream& os, bool decl) {
  const std::string name = MessageName(message_);
  if (decl) {
    os << "  size_t ROSSerializedSize() const;\n";
    os << "  absl::Status SerializeToROS("
          "::phaser::ROSBuffer& _phaser_buffer) const;\n";
    os << "  absl::Status DeserializeFromROS("
          "::phaser::ROSReader& _phaser_buffer);\n";
    os << "  absl::Status ParseFromROS(absl::Span<const char> input);\n";
    os << R"XXX(  absl::Status SerializeToROSArray(void* data, size_t size) const {
    ::phaser::ROSBuffer buffer(data, size);
    return SerializeToROS(buffer);
  }
  absl::Status SerializeToROSString(std::string* output) const {
    if (output == nullptr) {
      return absl::InvalidArgumentError("ROS output string is null");
    }
    output->resize(ROSSerializedSize());
    ::phaser::ROSBuffer buffer(output->data(), output->size());
    absl::Status status = SerializeToROS(buffer);
    if (!status.ok()) {
      output->clear();
    }
    return status;
  }
)XXX";
    os << "  static absl::Status ProtobufToROS("
          "std::string_view protobuf, ::phaser::ROSBuffer& output);\n";
    os << "  static absl::Status ProtobufWireToROS("
          "std::string_view protobuf, ::phaser::ROSBuffer& output);\n";
    os << "  static absl::Status ROSReaderToProtobuf("
          "::phaser::ROSReader& ros, ::phaser::ProtoWriter& output);\n";
    os << "  static absl::Status ROSToProtobuf("
          "absl::Span<const char> ros, ::phaser::ProtoBuffer& output);\n";
    os << R"XXX(  static bool ROSToProtobufArray(
      absl::Span<const char> ros, void* output, size_t output_size) {
    ::phaser::ProtoBuffer buffer(static_cast<char*>(output), output_size);
    return ROSToProtobuf(ros, buffer).ok();
  }
)XXX";
    os << "  static absl::Status PhaserToROS("
          "absl::Span<const char> phaser, ::phaser::ROSBuffer& output);\n\n";
    os << "  static absl::Status ConvertToROS("
          "absl::Span<const char> input, ::phaser::ROSBuffer& output);\n\n";
    return;
  }

  os << "size_t " << name << "::ROSSerializedSize() const {\n";
  os << "  SyncToPayload();\n";
  if (IsRosTime(message_) || IsRosDuration(message_)) {
    os << "  return 8;\n";
  } else {
    os << "  size_t _phaser_serialized_size = 0;\n";
    for (const auto& item : fields_in_order_) {
      if (item->IsUnion()) {
        auto union_info = std::static_pointer_cast<UnionInfo>(item);
        os << "  _phaser_serialized_size += 4;\n";
        os << "  switch (" << union_info->member_name
           << ".Discriminator()) {\n";
        for (size_t i = 0; i < union_info->members.size(); ++i) {
          const auto& field = union_info->members[i];
          os << "    case " << field->field->number() << ":\n";
          GenerateROSFieldSize(
              os, field->field,
              ROSFieldValueExpression(field, union_info, static_cast<int>(i)),
              "      ");
          os << "      break;\n";
        }
        os << "    default:\n";
        os << "      break;\n";
        os << "  }\n";
        continue;
      }

      const auto* descriptor = item->field;
      if (!descriptor->is_repeated()) {
        GenerateROSFieldSize(os, descriptor, ROSFieldValueExpression(item),
                             "  ");
        continue;
      }

      const int fixed_extent = GetArraySize(descriptor);
      if (fixed_extent <= 0) {
        os << "  _phaser_serialized_size += 4;\n";
      }
      const std::string count = fixed_extent > 0
                                    ? std::to_string(fixed_extent)
                                    : item->member_name + ".size()";
      std::string bulk_type = ROSBulkPrimitiveType(descriptor);
      if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        bulk_type = EnumName(descriptor->enum_type());
      }
      if (!bulk_type.empty()) {
        os << "  _phaser_serialized_size += " << count << " * sizeof("
           << bulk_type << ");\n";
      } else {
        os << "  for (size_t _phaser_index = 0; _phaser_index < " << count
           << "; ++_phaser_index) {\n";
        GenerateROSFieldSize(os, descriptor,
                             item->member_name + ".Get(_phaser_index)", "    ");
        os << "  }\n";
      }
    }
    os << "  return _phaser_serialized_size;\n";
  }
  os << "}\n\n";

  os << "absl::Status " << name
     << "::SerializeToROS(::phaser::ROSBuffer& _phaser_buffer) const {\n";
  os << "  SyncToPayload();\n";
  if (IsRosTime(message_)) {
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<uint32_t>(seconds())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<uint32_t>(nanos())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
  } else if (IsRosDuration(message_)) {
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<int32_t>(seconds())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<int32_t>(nanos())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
  } else {
    for (const auto& item : fields_in_order_) {
      if (item->IsUnion()) {
        auto union_info = std::static_pointer_cast<UnionInfo>(item);
        os << "  if (absl::Status _phaser_status = "
              "_phaser_buffer.Write(static_cast<uint32_t>("
           << union_info->member_name
           << ".Discriminator())); !_phaser_status.ok()) "
              "return _phaser_status;\n";
        os << "  switch (" << union_info->member_name
           << ".Discriminator()) {\n";
        for (size_t i = 0; i < union_info->members.size(); ++i) {
          const auto& field = union_info->members[i];
          os << "    case " << field->field->number() << ":\n";
          GenerateROSFieldWrite(
              os, field->field,
              ROSFieldValueExpression(field, union_info, static_cast<int>(i)),
              "      ");
          os << "      break;\n";
        }
        os << "    default:\n";
        os << "      break;\n";
        os << "  }\n";
        continue;
      }

      const auto* descriptor = item->field;
      if (!descriptor->is_repeated()) {
        GenerateROSFieldWrite(os, descriptor, ROSFieldValueExpression(item),
                              "  ");
        continue;
      }

      const int fixed_extent = GetArraySize(descriptor);
      if (fixed_extent <= 0) {
        os << "  if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteSequenceLength("
           << item->member_name
           << ".size()); !_phaser_status.ok()) return _phaser_status;\n";
      }
      const std::string count = fixed_extent > 0
                                    ? std::to_string(fixed_extent)
                                    : item->member_name + ".size()";
      std::string bulk_type = ROSBulkPrimitiveType(descriptor);
      if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        bulk_type = EnumName(descriptor->enum_type());
      }
      if (!bulk_type.empty()) {
        if (fixed_extent > 0) {
          os << "  if (" << item->member_name << ".data() == nullptr) {\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.WriteZeros("
             << count << " * sizeof(" << bulk_type
             << ")); !_phaser_status.ok()) return _phaser_status;\n";
          os << "  } else {\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.WriteArray<"
             << bulk_type << ">(absl::Span<const " << bulk_type << ">("
             << item->member_name << ".data(), " << count
             << ")); !_phaser_status.ok()) return _phaser_status;\n";
          os << "  }\n";
        } else {
          os << "  if (absl::Status _phaser_status = "
                "_phaser_buffer.WriteArray<"
             << bulk_type << ">(absl::Span<const " << bulk_type << ">("
             << item->member_name << ".data(), " << count
             << ")); !_phaser_status.ok()) return _phaser_status;\n";
        }
      } else {
        os << "  for (size_t _phaser_index = 0; _phaser_index < " << count
           << "; ++_phaser_index) {\n";
        GenerateROSFieldWrite(
            os, descriptor, item->member_name + ".Get(_phaser_index)", "    ");
        os << "  }\n";
      }
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::DeserializeFromROS(::phaser::ROSReader& _phaser_buffer) {\n";
  os << "  Clear();\n";
  if (IsRosTime(message_)) {
    os << "  absl::StatusOr<uint32_t> _phaser_sec = "
          "_phaser_buffer.Read<uint32_t>();\n";
    os << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
    os << "  absl::StatusOr<uint32_t> _phaser_nsec = "
          "_phaser_buffer.Read<uint32_t>();\n";
    os << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
    os << "  set_seconds(static_cast<int64_t>(*_phaser_sec));\n";
    os << "  set_nanos(static_cast<int32_t>(*_phaser_nsec));\n";
  } else if (IsRosDuration(message_)) {
    os << "  absl::StatusOr<int32_t> _phaser_sec = "
          "_phaser_buffer.Read<int32_t>();\n";
    os << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
    os << "  absl::StatusOr<int32_t> _phaser_nsec = "
          "_phaser_buffer.Read<int32_t>();\n";
    os << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
    os << "  set_seconds(static_cast<int64_t>(*_phaser_sec));\n";
    os << "  set_nanos(*_phaser_nsec);\n";
  } else {
    for (const auto& item : fields_in_order_) {
      if (item->IsUnion()) {
        auto union_info = std::static_pointer_cast<UnionInfo>(item);
        os << "  {\n";
        os << "    absl::StatusOr<uint32_t> _phaser_discriminator = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << "    if (!_phaser_discriminator.ok()) return "
              "_phaser_discriminator.status();\n";
        os << "    switch (*_phaser_discriminator) {\n";
        os << "      case 0:\n";
        os << "        " << union_info->member_name << ".reset();\n";
        os << "        break;\n";
        for (size_t i = 0; i < union_info->members.size(); ++i) {
          const auto& field = union_info->members[i];
          os << "      case " << field->field->number() << ":\n";
          GenerateROSFieldRead(os, field->field, union_info->member_name,
                               "        ", false, "", static_cast<int>(i));
          os << "        break;\n";
        }
        os << "      default:\n";
        os << "        return absl::InvalidArgumentError("
              "\"Unknown ROS oneof discriminator\");\n";
        os << "    }\n";
        os << "  }\n";
        continue;
      }

      const auto* descriptor = item->field;
      if (!descriptor->is_repeated()) {
        GenerateROSFieldRead(os, descriptor, item->member_name, "  ");
        continue;
      }

      const int fixed_extent = GetArraySize(descriptor);
      std::string bulk_type = ROSBulkPrimitiveType(descriptor);
      if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        bulk_type = EnumName(descriptor->enum_type());
      }
      if (fixed_extent <= 0) {
        os << "  {\n";
        os << "    absl::StatusOr<uint32_t> _phaser_count = "
              "_phaser_buffer.ReadSequenceLength();\n";
        os << "    if (!_phaser_count.ok()) return _phaser_count.status();\n";
        if (!bulk_type.empty()) {
          os << "    if (*_phaser_count > _phaser_buffer.Remaining() / sizeof("
             << bulk_type << ")) {\n";
          os << "      return absl::InvalidArgumentError("
                "\"ROS sequence length exceeds remaining input\");\n";
          os << "    }\n";
          os << "    " << item->member_name << ".resize(*_phaser_count);\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.ReadArray<"
             << bulk_type << ">(absl::Span<" << bulk_type << ">("
             << item->member_name
             << ".data(), static_cast<size_t>(*_phaser_count))); "
                "!_phaser_status.ok()) return _phaser_status;\n";
        } else {
          os << "    for (uint32_t _phaser_index = 0; "
                "_phaser_index < *_phaser_count; ++_phaser_index) {\n";
          GenerateROSFieldRead(os, descriptor, item->member_name, "      ",
                               true);
          os << "    }\n";
        }
        os << "  }\n";
      } else if (!bulk_type.empty()) {
        os << "  if (absl::Status _phaser_status = "
              "_phaser_buffer.ReadArray<"
           << bulk_type << ">(absl::Span<" << bulk_type << ">("
           << item->member_name << ".data(), " << fixed_extent
           << ")); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "  for (size_t _phaser_index = 0; _phaser_index < "
           << fixed_extent << "; ++_phaser_index) {\n";
        GenerateROSFieldRead(os, descriptor, item->member_name, "    ", false,
                             "_phaser_index");
        os << "  }\n";
      }
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ParseFromROS(absl::Span<const char> input) {\n";
  os << "  ::phaser::ROSReader buffer(input);\n";
  os << "  if (absl::Status status = DeserializeFromROS(buffer); "
        "!status.ok()) return status;\n";
  os << "  if (!buffer.Eof()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Trailing bytes after ROS message\");\n";
  os << "  }\n";
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  GenerateDirectProtobufToROS(os);
  GenerateDirectROSToProtobuf(os);

  os << "absl::Status " << name
     << "::ProtobufToROS(std::string_view protobuf, "
        "::phaser::ROSBuffer& output) {\n";
  os << "  output.Clear();\n";
  os << "  absl::Status status = ProtobufWireToROS(protobuf, output);\n";
  os << "  if (!status.ok()) output.Clear();\n";
  os << "  return status;\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::PhaserToROS(absl::Span<const char> phaser, "
        "::phaser::ROSBuffer& output) {\n";
  os << "  if (phaser.empty()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Native Phaser payload is empty\");\n";
  os << "  }\n";
  os << "  " << name
     << " message = CreateReadonly(phaser.data(), phaser.size());\n";
  os << "  return message.SerializeToROS(output);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ConvertToROS(absl::Span<const char> input, "
        "::phaser::ROSBuffer& output) {\n";
  os << "  switch (::phaser::InferMessageWireFormat(input)) {\n";
  os << "    case ::phaser::MessageWireFormat::kProtobuf:\n";
  os << "      return ProtobufToROS("
        "std::string_view(input.data(), input.size()), output);\n";
  os << "    case ::phaser::MessageWireFormat::kPhaser:\n";
  os << "      return PhaserToROS(input, output);\n";
  os << "    case ::phaser::MessageWireFormat::kAmbiguous:\n";
  os << "      return absl::InvalidArgumentError("
        "\"Input is structurally valid as both Phaser and protobuf\");\n";
  os << "    case ::phaser::MessageWireFormat::kUnknown:\n";
  os << "      return absl::InvalidArgumentError("
        "\"Input is neither a valid Phaser payload nor protobuf wire "
        "message\");\n";
  os << "  }\n";
  os << "  return absl::InvalidArgumentError(\"Unknown input format\");\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateSerializedSize(std::ostream& os, bool decl) {
  if (decl) {
    os << "  size_t SerializedSize() const;\n";
    return;
  }
  os << "size_t " << MessageName(message_) << "::SerializedSize() const {\n";
  os << "  size_t _phaser_serialized_size = 0;\n";
  for (auto& field : fields_) {
    if (field->field->is_repeated()) {
      os << "  _phaser_serialized_size += " << field->member_name
         << ".SerializedSize();\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    _phaser_serialized_size += " << field->member_name
         << ".SerializedSize();\n";
      os << "  }\n";
    }
  }
  for (auto& [oneof, u] : unions_) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      os << "    _phaser_serialized_size += " << u->member_name
         << ".SerializedSize<" << i << ">(" << field->field->number() << ");\n";
      os << "    break;\n";
    }
    os << "  }\n";
  }
  os << "  return _phaser_serialized_size;\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateSerializer(std::ostream& os, bool decl) {
  if (decl) {
    os << "  absl::Status Serialize("
          "::phaser::ProtoBuffer &_phaser_buffer) const;\n";
    return;
  }
  os << "absl::Status " << MessageName(message_)
     << "::Serialize(::phaser::ProtoBuffer &_phaser_buffer) const {\n";
  for (auto& field : fields_) {
    if (field->field->is_repeated()) {
      os << "  if (absl::Status _phaser_status = " << field->member_name
         << ".Serialize(_phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    if (absl::Status _phaser_status = " << field->member_name
         << ".Serialize(_phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "  }\n";
    }
  }
  for (auto& [oneof, u] : unions_) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      os << "    if (absl::Status _phaser_status = " << u->member_name
         << ".Serialize<" << i << ">(" << field->field->number()
         << ", _phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "    break;\n";
    }
    os << "  }\n";
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateDeserializer(std::ostream& os, bool decl) {
  if (decl) {
    os << "  absl::Status Deserialize("
          "::phaser::ProtoBuffer &_phaser_buffer);\n";
    return;
  }
  os << "absl::Status " << MessageName(message_)
     << "::Deserialize(::phaser::ProtoBuffer &_phaser_buffer) {";
  bool has_array_fields = false;
  for (auto& field : fields_) {
    if (UsesArrayFacade(field->field)) {
      has_array_fields = true;
      break;
    }
  }
  if (has_array_fields) {
    os << "\n";
    for (auto& field : fields_) {
      if (UsesArrayFacade(field->field)) {
        os << "  " << field->member_name << ".BeginDeserialize();\n";
      }
    }
  }
  os << R"XXX(
  while (!_phaser_buffer.Eof()) {
    absl::StatusOr<uint32_t> _phaser_tag =
        _phaser_buffer.DeserializeVarint<uint32_t, false>();
    if (!_phaser_tag.ok()) {
      return _phaser_tag.status();
    }
    uint32_t _phaser_field_number =
        *_phaser_tag >> ::phaser::ProtoBuffer::kFieldIdShift;
    switch (_phaser_field_number) {
)XXX";
  for (auto& field : fields_) {
    os << "    case " << field->field->number() << ":\n";
    os << "      if (absl::Status _phaser_status = " << field->member_name
       << ".Deserialize(_phaser_buffer); !_phaser_status.ok()) "
          "return _phaser_status;\n";
    os << "      break;\n";
  }
  for (auto& [oneof, u] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "    case " << field->field->number() << ":\n";
      os << "      if (absl::Status _phaser_status = " << u->member_name
         << ".Deserialize<" << i << ">(" << field->field->number()
         << ", _phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "      break;\n";
    }
  }
  os << R"XXX(
    default:
      if (absl::Status _phaser_status =
              _phaser_buffer.SkipTag(*_phaser_tag);
          !_phaser_status.ok()) {
        return _phaser_status;
      }
    }
  }
)XXX";
  if (has_array_fields) {
    os << "\n";
    for (auto& field : fields_) {
      if (UsesArrayFacade(field->field)) {
        os << "  if (absl::Status _phaser_status = " << field->member_name
           << ".FinalizeDeserialize(); !_phaser_status.ok()) "
              "return _phaser_status;\n";
      }
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateProtobufSerialization(std::ostream& os) {
  os << R"XXX(
  // This is the size of the message on the wire.  It is not the serialized protobuf size.
  size_t ByteSizeLong() const {
    return ZeroCopySize();
  }

  int ByteSize() const {
    return static_cast<int>(ByteSizeLong());
  }

  bool SerializeToArray(char* array, size_t size) const {
    ::phaser::ProtoBuffer buffer(array, size);
    if (absl::Status status = Serialize(buffer); !status.ok()) return false;
    return true;
  }

  bool ParseFromArray(const char* array, size_t size) {
    ::phaser::ProtoBuffer buffer(array, size);
    if (absl::Status status = Deserialize(buffer); !status.ok()) return false;
    return true;
  }

  // String serialization.
  bool SerializeToString(std::string* str) const {
    size_t size = SerializedSize();
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

void MessageGenerator::GenerateIndent(std::ostream& os) {
  os << "  void Indent([[maybe_unused]] int _phaser_indent) const {\n";
  for (auto& field : fields_) {
    os << "    " << field->member_name << ".Indent(_phaser_indent);\n";
  }
  for (auto& [oneof, u] : unions_) {
    os << "    " << u->member_name << ".Indent(_phaser_indent);\n";
  }
  os << "  }\n\n";
}

void MessageGenerator::GenerateStreamer(std::ostream& os) {
  os << "inline std::ostream &operator<<(std::ostream &os, [[maybe_unused]] "
        "const "
     << MessageName(message_) << " &msg) {\n";
  // We need to print the fields in the same order as they appear in the
  // message definition.  This is to match the output from the protobuf
  // printer.
  for (auto& field : fields_in_order_) {
    if (field->IsUnion()) {
      auto u = std::static_pointer_cast<UnionInfo>(field);
      os << "  switch (msg." << u->member_name << ".Discriminator()) {\n";
      for (size_t i = 0; i < u->members.size(); i++) {
        auto& member = u->members[i];
        os << "  case " << member->field->number() << ":\n";
        os << "    msg." << u->member_name << ".PrintIndent(os);\n";
        if (member->field->type() ==
            google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "    os << \"" << member->field->name() << " \";\n";
        } else {
          os << "    os << \"" << member->field->name() << ": \";\n";
        }
        os << "    msg." << u->member_name << ".Print<" << i << ">(os);\n";
        os << "    os << std::endl;\n";
        os << "    break;\n";
      }
      os << "  }\n";
      continue;
    }

    if (field->field->is_repeated()) {
      os << "  for (auto v : msg." << field->member_name << ") {\n";
      os << "    msg." << field->member_name << ".PrintIndent(os);\n";
      if (field->field->type() ==
          google::protobuf::FieldDescriptor::TYPE_ENUM) {
        os << "    os << \"" << field->field->name() << ": \" << "
           << EnumName(field->field->enum_type())
           << "Stringizer()(v) << std::endl;\n";
      } else if (field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_STRING ||
                 field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_BYTES) {
        os << "    os << \"" << field->field->name()
           << ": \\\"\" << v << \"\\\"\" << std::endl;\n";
      } else {
        os << "    os << \"" << field->field->name()
           << ": \" << v << std::endl;\n";
      }
      os << "  }\n";
    } else {
      os << "  if (msg." << field->member_name << ".IsPresent()) {\n";
      os << "    msg." << field->member_name << ".PrintIndent(os);\n";
      // e.g.    os << "str: " << msg.str_ << std::endl;
      if (field->field->type() ==
          google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        // Prootobuf doesn't put a colon after the name.
        os << "    os << \"" << field->field->name() << " \" << msg.";
      } else {
        os << "    os << \"" << field->field->name() << ": \" << msg.";
      }
      os << field->member_name << " << std::endl;\n";
      os << "  }\n";
    }
  }

  os << "  return os;\n";
  os << "}\n\n";
}

void MessageGenerator::GenerateCopy(std::ostream& os, bool decl) {
  if (decl) {
    os << "  template <typename T>\n";
    os << "  absl::Status CloneFrom(const T& _phaser_other);\n\n";
    os << "  void CopyFrom("
          "const ::phaser::Message& _phaser_other) override {\n";
    os << "    const " << MessageName(message_)
       << "& _phaser_message = static_cast<const " << MessageName(message_)
       << "&>(_phaser_other);\n";
    os << "    (void)CloneFrom(_phaser_message);\n";
    os << "  }\n\n";
    return;
  }

  // CloneFrom.
  os << "template <typename T>\n";
  os << "inline absl::Status " << MessageName(message_)
     << "::CloneFrom([[maybe_unused]] const T& _phaser_other) {\n";
  if (IsRosFrontend()) {
    for (auto& field : fields_) {
      if (field->field->is_repeated()) {
        os << "  " << field->member_name << ".Clear();\n";
        if (UsesArrayFacade(field->field)) {
          const int array_size = GetArraySize(field->field);
          if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
            os << "  for (size_t _phaser_index = 0; "
                  "_phaser_index < static_cast<size_t>("
               << array_size << "); ++_phaser_index) {\n";
            os << "    auto _phaser_source = _phaser_other."
               << field->member_name << ".Get(_phaser_index);\n";
            os << "    if (_phaser_source.IsBound()) {\n";
            os << "      auto _phaser_destination = " << field->member_name
               << ".Mutable(_phaser_index);\n";
            os << "      if (absl::Status _phaser_status = "
                  "_phaser_destination.CloneFrom(_phaser_source); "
                  "!_phaser_status.ok()) return _phaser_status;\n";
            os << "    }\n";
            os << "  }\n";
          } else if (field->field->type() ==
                         google::protobuf::FieldDescriptor::TYPE_STRING ||
                     field->field->type() ==
                         google::protobuf::FieldDescriptor::TYPE_BYTES) {
            os << "  for (size_t _phaser_index = 0; "
                  "_phaser_index < static_cast<size_t>("
               << array_size << "); ++_phaser_index) {\n";
            os << "    " << field->member_name
               << ".Set(_phaser_index, _phaser_other." << field->member_name
               << ".Get(_phaser_index));\n";
            os << "  }\n";
          } else {
            os << "  for (size_t _phaser_index = 0; "
                  "_phaser_index < static_cast<size_t>("
               << array_size << "); ++_phaser_index) {\n";
            os << "    " << field->member_name
               << ".Set(_phaser_index, _phaser_other." << field->member_name
               << ".Get(_phaser_index));\n";
            os << "  }\n";
          }
        } else if (field->field->type() ==
                   google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "  for (auto _phaser_value : _phaser_other."
             << field->member_name << ") {\n";
          os << "    auto _phaser_message = " << field->member_name
             << ".Add();\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_message.CloneFrom(_phaser_value); "
                "!_phaser_status.ok()) return _phaser_status;\n";
          os << "  }\n";
        } else {
          os << "  for (auto _phaser_value : _phaser_other."
             << field->member_name << ") {\n";
          os << "    " << field->member_name << ".Add(_phaser_value);\n";
          os << "  }\n";
        }
      } else if (field->field->type() ==
                 google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        os << "  if (_phaser_other." << field->member_name
           << ".IsPresent()) {\n";
        if (IsRosIntrinsic(field->field)) {
          os << "    " << field->member_name << ".Set(_phaser_other."
             << field->member_name << ".Get());\n";
        } else {
          os << "    if (absl::Status _phaser_status = " << field->member_name
             << ".Mutable()->CloneFrom(_phaser_other." << field->member_name
             << ".Get()); !_phaser_status.ok()) return _phaser_status;\n";
        }
        os << "  } else {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
      } else if (field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_STRING ||
                 field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_BYTES) {
        os << "  if (_phaser_other." << field->member_name
           << ".IsPresent()) {\n";
        os << "    " << field->member_name << ".Set(_phaser_other."
           << field->member_name << ".Get());\n";
        os << "  } else {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
      } else {
        os << "  if (_phaser_other." << field->member_name
           << ".IsPresent()) {\n";
        os << "    " << field->member_name << ".Set(_phaser_other."
           << field->member_name << ".Get());\n";
        os << "  } else {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
      }
    }
    if (!unions_.empty()) {
      for (auto& [oneof, u] : unions_) {
        os << "  switch (_phaser_other." << u->member_name
           << ".Discriminator()) {\n";
        for (size_t i = 0; i < u->members.size(); i++) {
          auto& field = u->members[i];
          os << "  case " << field->field->number() << ":\n";
          if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
            os << "    if (absl::Status _phaser_status = " << u->member_name
               << ".template CloneFrom<" << i << ">(_phaser_other."
               << u->member_name << ".template GetReference<" << i << ", "
               << MessageName(field->field->message_type())
               << ">()); !_phaser_status.ok()) return _phaser_status;\n";
          } else {
            os << "    if (absl::Status _phaser_status = " << u->member_name
               << ".template CloneFrom<" << i << ">(_phaser_other."
               << u->member_name << ".template GetValue<" << i << ", "
               << field->c_type
               << ">()); !_phaser_status.ok()) return _phaser_status;\n";
          }
          os << "    break;\n";
        }
        os << "  default:\n";
        for (size_t i = 0; i < u->members.size(); i++) {
          os << "    " << u->member_name << ".Clear<" << i << ">();\n";
        }
        os << "    break;\n";
        os << "  }\n";
      }
    }
  } else {
    for (auto& field : fields_) {
      if (field->field->is_repeated()) {
        os << "  for (auto _phaser_value : _phaser_other."
           << field->field->name() << "()) {\n";
        if (field->field->type() ==
            google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "    auto _phaser_message = add_" << field->field->name()
             << "();\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_message.CloneFrom(_phaser_value); "
                "!_phaser_status.ok()) return _phaser_status;\n";
        } else {
          os << "    add_" << field->field->name() << "(_phaser_value);\n";
        }
        os << "  }\n";

      } else {
        os << "  if (_phaser_other." << field->member_name
           << ".IsPresent()) {\n";
        if (field->field->type() ==
            google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "    auto* _phaser_message = mutable_" << field->field->name()
             << "();\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_message->CloneFrom(_phaser_other."
             << field->field->name()
             << "()); !_phaser_status.ok()) return _phaser_status;\n";
        } else {
          os << "    set_" << field->field->name() << "(_phaser_other."
             << field->field->name() << "());\n";
        }
        os << "  }\n";
      }
    }
    if (!unions_.empty()) {
      for (auto& [oneof, u] : unions_) {
        os << "  switch (_phaser_other." << u->member_name
           << ".Discriminator()) {\n";
        for (size_t i = 0; i < u->members.size(); i++) {
          auto& field = u->members[i];
          os << "  case " << field->field->number() << ":\n";
          os << "    if (absl::Status _phaser_status = " << u->member_name
             << ".template CloneFrom<" << i << ">(_phaser_other."
             << field->field->name()
             << "()); !_phaser_status.ok()) return _phaser_status;\n";
          os << "    break;\n";
        }
        os << "  }\n";
      }
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

// DebugString
void MessageGenerator::GenerateDebugString(std::ostream& os) {
  os << R"XXX(
  std::string DebugString() const {
    std::ostringstream os;
    os << *this;
    return os.str();
  }

)XXX";
}

void MessageGenerator::GeneratePhaserBank(std::ostream& os) {
  os << "static void " << MessageName(message_)
     << "StreamTo(const ::phaser::Message& msg, std::ostream& os, int indent) "
        "{\n";
  os << "  const " << MessageName(message_) << " *m = static_cast<const "
     << MessageName(message_) << "*>(&msg);\n";
  os << "  m->Indent(indent);\n";
  os << "  os << *m;\n";
  os << "  m->Indent(-indent);\n";
  os << "}\n\n";

  os << "static absl::Status " << MessageName(message_)
     << "SerializeToBuffer(const ::phaser::Message& msg, ::phaser::ProtoBuffer "
        "&buffer) {\n";
  os << "  const " << MessageName(message_) << " *m = static_cast<const "
     << MessageName(message_) << "*>(&msg);\n";
  os << "  return m->Serialize(buffer);\n";
  os << "}\n\n";

  os << "static absl::Status " << MessageName(message_)
     << "DeserializeFromBuffer(::phaser::Message &msg, ::phaser::ProtoBuffer "
        "&buffer) {\n";
  os << "  " << MessageName(message_) << " *m = static_cast<"
     << MessageName(message_) << "*>(&msg);\n";
  os << "  return m->Deserialize(buffer);\n";
  os << "}\n\n";

  os << "static size_t " << MessageName(message_)
     << "SerializedSize(const ::phaser::Message& msg) {\n";
  os << "  const " << MessageName(message_) << " *m = static_cast<const "
     << MessageName(message_) << "*>(&msg);\n";
  os << "  return m->SerializedSize();\n";
  os << "}\n\n";

  os << "static absl::Status " << MessageName(message_)
     << "SerializeAtOffset(std::shared_ptr<::phaser::MessageRuntime> runtime, "
        "::toolbelt::BufferOffset offset, ::phaser::ProtoBuffer& buffer) {\n";
  os << "  const " << MessageName(message_) << " message(runtime, offset);\n";
  os << "  return message.Serialize(buffer);\n";
  os << "}\n\n";

  os << "static absl::Status " << MessageName(message_)
     << "DeserializeAtOffset("
        "std::shared_ptr<::phaser::MessageRuntime> runtime, "
        "::toolbelt::BufferOffset offset, ::phaser::ProtoBuffer& buffer) {\n";
  os << "  " << MessageName(message_) << " message(runtime, offset);\n";
  os << "  message.InstallMetadata<" << MessageName(message_) << ">();\n";
  os << "  return message.Deserialize(buffer);\n";
  os << "}\n\n";

  os << "static size_t " << MessageName(message_)
     << "SerializedSizeAtOffset("
        "std::shared_ptr<::phaser::MessageRuntime> runtime, "
        "::toolbelt::BufferOffset offset) {\n";
  os << "  const " << MessageName(message_) << " message(runtime, offset);\n";
  os << "  return message.SerializedSize();\n";
  os << "}\n\n";

  os << "static ::phaser::Message* " << MessageName(message_)
     << "AllocateAtOffset(std::shared_ptr<::phaser::MessageRuntime> runtime, "
        "::toolbelt::BufferOffset offset) {\n";
  os << "  auto msg = new " << MessageName(message_) << "(runtime, offset);\n";
  os << "  msg->InstallMetadata<" << MessageName(message_) << ">();\n";
  os << "  return msg;\n";
  os << "}\n\n";

  os << "static std::pair<::phaser::Message *, toolbelt::BufferOffset>\n";
  os << MessageName(message_)
     << "Allocate(std::shared_ptr<::phaser::MessageRuntime> runtime) {\n";
  os << "  void *addr = toolbelt::PayloadBuffer::Allocate(&runtime->pb, "
     << MessageName(message_) << "::BinarySize());\n";
  os << "  toolbelt::BufferOffset offset = runtime->pb->ToOffset(addr);\n";
  os << "  auto msg = new " << MessageName(message_) << "(runtime, offset);\n";
  os << "  msg->InstallMetadata<" << MessageName(message_) << ">();\n";
  os << "  return std::make_pair(msg, offset);\n";
  os << "}\n\n";

  os << "static void " << MessageName(message_)
     << "Clear(::phaser::Message &msg) {\n";
  os << "  " << MessageName(message_) << " *m = static_cast<"
     << MessageName(message_) << "*>(&msg);\n";
  os << "  m->Clear();\n";
  os << "}\n\n";

  os << "static absl::Status " << MessageName(message_)
     << "Copy(const ::phaser::Message &src, ::phaser::Message& dst) {\n";
  os << "  const " << MessageName(message_) << " *src_m = static_cast<const "
     << MessageName(message_) << "*>(&src);\n";
  os << "  " << MessageName(message_) << " *dst_m = static_cast<"
     << MessageName(message_) << "*>(&dst);\n";
  os << "  return dst_m->CloneFrom(*src_m);\n";
  os << "}\n\n";

  os << "static const ::phaser::Message *" << MessageName(message_)
     << "MakeExisting(std::shared_ptr<::phaser::MessageRuntime> runtime, const "
        "void *data) {\n";
  os << "  return new " << MessageName(message_)
     << "(runtime, runtime->ToOffset(data));\n";
  os << "}\n\n";

  os << "static size_t " << MessageName(message_) << "BinarySize() { return "
     << MessageName(message_) << "::BinarySize(); }\n\n";

  os << "static bool " << MessageName(message_)
     << "HasField(const ::phaser::Message &msg, int number) {\n";
  os << "  [[maybe_unused]] const " << MessageName(message_)
     << " *m = static_cast<const " << MessageName(message_) << "*>(&msg);\n";
  os << "  switch (number) {\n";
  for (auto& field : fields_) {
    os << "  case " << field->field->number() << ":\n";
    if (IsRosFrontend()) {
      if (field->field->is_repeated()) {
        os << "    return m->" << field->member_name << ".Size() > 0;\n";
      } else {
        os << "    return m->" << field->member_name << ".IsPresent();\n";
      }
    } else if (field->field->is_repeated()) {
      os << "    return m->" << field->field->name() << "_size() > 0;\n";
    } else {
      os << "    return m->has_" << field->field->name() << "();\n";
    }
  }
  for (auto& [oneof, u] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      if (IsRosFrontend()) {
        os << "    return m->" << u->member_name
           << ".Discriminator() == " << field->field->number() << ";\n";
      } else {
        os << "    return m->" << oneof->name()
           << "_case() == " << field->field->number() << ";\n";
      }
    }
  }
  os << "  }\n";
  os << "  return false;\n";
  os << "}\n\n";

  os << "static const ::phaser::MessageInfo* " << MessageName(message_)
     << "GetMessageInfo() {\n";
  os << "  return " << MessageName(message_) << "::GetMessageInfoStatic();\n";
  os << "}\n\n";

  os << "static void *" << MessageName(message_)
     << "GetFieldByNumber(::phaser::Message &msg, int number) {\n";
  os << "  if (!" << MessageName(message_) << "HasField(msg, number)) {\n";
  os << "    return nullptr;\n";
  os << "  }\n";
  os << "  const ::phaser::MessageInfo *info = " << MessageName(message_)
     << "::GetMessageInfoStatic();\n";
  os << R"XXX(
  auto it = info->fields_by_number.find(number);
  if (it != info->fields_by_number.end()) {
    char *m = reinterpret_cast<char *>(&msg);
    return m + it->second->offset;
  }
  return nullptr;
  }
)XXX";

  os << "static void *" << MessageName(message_)
     << "GetFieldByName(::phaser::Message &msg, const std::string &name) {\n";
  os << "  const ::phaser::MessageInfo *info = " << MessageName(message_)
     << "::GetMessageInfoStatic();\n";
  os << "  auto it = info->fields_by_name.find(name);\n";
  os << "  if (it != info->fields_by_name.end()) {\n";
  os << "    if (!" << MessageName(message_)
     << "HasField(msg, it->second->number)) {\n";
  os << "      return nullptr;\n";
  os << "    }\n";
  os << "    char *m = reinterpret_cast<char *>(&msg);\n";
  os << "    return m + it->second->offset;\n";
  os << "  }\n";
  os << "  return nullptr;\n";
  os << "}\n\n";

  os << "static ::phaser::BankInfo " << MessageName(message_)
     << "BankInfo = {\n";
  os << "  .stream_to = " << MessageName(message_) << "StreamTo,\n";
  os << "  .serialize_to_buffer = " << MessageName(message_)
     << "SerializeToBuffer,\n";
  os << "  .deserialize_from_buffer = " << MessageName(message_)
     << "DeserializeFromBuffer,\n";
  os << "  .serialized_size = " << MessageName(message_) << "SerializedSize,\n";
  os << "  .serialize_at_offset = " << MessageName(message_)
     << "SerializeAtOffset,\n";
  os << "  .deserialize_at_offset = " << MessageName(message_)
     << "DeserializeAtOffset,\n";
  os << "  .serialized_size_at_offset = " << MessageName(message_)
     << "SerializedSizeAtOffset,\n";
  os << "  .allocate_at_offset = " << MessageName(message_)
     << "AllocateAtOffset,\n";
  os << "  .allocate = " << MessageName(message_) << "Allocate,\n";
  os << "  .clear = " << MessageName(message_) << "Clear,\n";
  os << "  .copy = " << MessageName(message_) << "Copy,\n";
  os << "  .make_existing = " << MessageName(message_) << "MakeExisting,\n";
  os << "  .binary_size = " << MessageName(message_) << "BinarySize,\n";
  os << "  .message_info = " << MessageName(message_) << "GetMessageInfo,\n";
  os << "  .has_field = " << MessageName(message_) << "HasField,\n";
  os << "  .get_field_by_name = " << MessageName(message_)
     << "GetFieldByName,\n";
  os << "  .get_field_by_number = " << MessageName(message_)
     << "GetFieldByNumber,\n";
  os << "};\n\n";

  os << "static struct " << MessageName(message_) << "BankInitializer {\n";
  os << "  " << MessageName(message_) << "BankInitializer() {\n";
  os << "    ::phaser::PhaserBankRegisterMessage(" << MessageName(message_)
     << "::FullName(), " << MessageName(message_) << "BankInfo);\n";
  os << "  }\n";
  os << "} " << MessageName(message_) << "BankInitializer;\n";
}

void MessageGenerator::GenerateFieldInfo(int index,
                                         std::shared_ptr<FieldInfo> field,
                                         std::shared_ptr<UnionInfo> union_field,
                                         int union_index, std::ostream& os) {
  std::string field_type = FieldInfoType(field->field);
  std::string fixed_size_string =
      field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_SFIXED32 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_SFIXED64 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_FIXED32 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_FIXED64 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_FLOAT ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_DOUBLE
          ? ", true"
          : ", false";
  std::string signed_string =
      field->field->type() == google::protobuf::FieldDescriptor::TYPE_SINT32 ||
              field->field->type() ==
                  google::protobuf::FieldDescriptor::TYPE_SINT64
          ? ", true"
          : ", false";
  std::string packed_string = field->field->is_packed() ? ", true" : ", false";

  std::string field_info_string =
      union_index == -1 ? "PrimitiveFieldInfo" : "UnionFieldInfo";
  if (union_index == -1) {
    os << "  info.";
  } else {
    os << "  u->";
  }
  os << "fields_in_order[" << index
     << "] = std::make_shared<::phaser::" << field_info_string << ">(\""
     << field->field->name() << "\", " << field_type << ", "
     << field->field->number();
  if (union_index != -1) {
    os << ", offsetof(" << MessageName(message_) << ", "
       << union_field->member_name << "), " << union_index;
  } else {
    os << ", offsetof(" << MessageName(message_) << ", " << field->member_name
       << ")";
  }
  if (field->field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    os << ", \"" << MessageName(field->field->message_type()) << "\"";
  } else if (field->field->type() ==
             google::protobuf::FieldDescriptor::TYPE_ENUM) {
    os << ", \"" << EnumName(field->field->enum_type()) << "\"";
  } else {
    os << fixed_size_string << signed_string;
  }
  if (union_index == -1) {
    if (field->field->is_repeated()) {
      os << ", true";
    } else {
      os << ", false";
    }
    os << packed_string;
  }
  os << ");\n";
}

void MessageGenerator::GenerateMessageInfo(std::ostream& os, bool decl) {
  if (decl) {
    os << "  static const ::phaser::MessageInfo* GetMessageInfoStatic();\n";
    os << "  const ::phaser::MessageInfo* GetMessageInfo() const override {\n";
    os << "    return GetMessageInfoStatic();\n";
    os << "  }\n";
    return;
  }
  os << "const ::phaser::MessageInfo* " << MessageName(message_)
     << "::GetMessageInfoStatic() {\n";
  os << "  static ::phaser::MessageInfo info;\n";
  os << "  if (!info.full_name.empty()) {\n";
  os << "    return &info;\n";
  os << "  }\n";

  os << "#pragma clang diagnostic push\n";
  os << "#pragma clang diagnostic ignored \"-Winvalid-offsetof\"\n";

  // Generate fields_in_order.
  int index = 0;
  os << "  info.fields_in_order.resize(" << fields_in_order_.size() << ");\n";
  for (auto& field : fields_in_order_) {
    if (field->IsUnion()) {
      auto u = std::static_pointer_cast<UnionInfo>(field);
      os << "  info.fields_in_order[" << index++
         << "] = std::make_shared<::phaser::UnionInfo>(\"" << u->oneof->name()
         << "\", offsetof(" << MessageName(message_) << ", "
         << field->member_name << "));\n";
      continue;
    }
    GenerateFieldInfo(index++, field, nullptr, -1, os);
  }

  os << R"XXX(  for (auto &f : info.fields_in_order) {
    info.fields_by_number[f->number] = f;
    info.fields_by_name[f->name] = f;
  }
)XXX";
  // Generate oneof fields.
  index = 0;
  for (auto& field : fields_in_order_) {
    if (field->IsUnion()) {
      auto u = std::static_pointer_cast<UnionInfo>(field);
      os << "  {\n";
      os << "    auto u = "
            "std::static_pointer_cast<::phaser::UnionInfo>(info.fields_in_"
            "order["
         << index << "]);\n";
      os << "    u->fields_in_order.resize(" << u->members.size() << ");\n";
      for (size_t i = 0; i < u->members.size(); i++) {
        auto& member = u->members[i];
        GenerateFieldInfo(static_cast<int>(i), member, u, static_cast<int>(i),
                          os);
      }
      os << R"XXX(  for (auto &f : u->fields_in_order) {
    info.fields_by_number[f->number] = f;
    info.fields_by_name[f->name] = f;
  }
)XXX";
      os << "  }\n";
    }
    index++;
  }

  os << "  return &info;\n";
  os << "}\n\n";
  os << "#pragma clang diagnostic pop\n";
}

}  // namespace phaser
