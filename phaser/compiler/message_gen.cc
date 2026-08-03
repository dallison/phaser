// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/message_gen.h"

#include <ctype.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>

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
  const std::string context = absl::StrFormat("%s.%s", message_->full_name(),
                                              field->name());
  if (array_size <= 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("phaser.array_size must be positive on field %s",
                        context));
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
    if (absl::Status status = ValidateArraySizeOption(field);
        !status.ok()) {
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
      return "PrimitiveArrayField<double, " + extent + ", true, false" +
             packed;
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
  return desc != nullptr &&
         desc->full_name() == "google.protobuf.Timestamp";
}

bool MessageGenerator::IsRosDuration(
    const google::protobuf::Descriptor* desc) const {
  return desc != nullptr &&
         desc->full_name() == "google.protobuf.Duration";
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
            oneof, 4, MemberVariableName(std::string(oneof->name())), "UnionField");
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
  // Generate constructors.
  GenerateConstructors(os, true);
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

  os << "  static std::string FullName() { return \"" << message_->full_name()
     << "\"; }\n";
  os << "  static std::string Name() { return \"" << message_->name()
     << "\"; }\n\n";

  os << "  std::string GetName() const override { return Name(); }\n";
  os << "  std::string GetFullName() const override { return FullName(); }\n";

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
    os << "    switch (" << union_info->member_name
       << ".Discriminator()) {\n";
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

  os << name << "::" << name << "(const " << name << "& other)\n";
  GenerateFieldInitializers(os);
  os << R"XXX({
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
    os << "  struct " << variant_name << " : public ::phaser::"
       << u->member_type << " {\n";
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
      os << "  using " << alternative_name << " = " << variant_name << "::"
         << alternative_name << ";\n";
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
       << u->member_name << "), " << u->offset << ", 0, 0, {";
    const char* num_sep = "";
    for (auto& field : u->members) {
      os << num_sep << field->field->number();
      num_sep = ",";
    }
    os << "})\n";
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
        "  auto runtime = "
        "std::make_shared<::phaser::MutableMessageRuntime>(pb);\n"
        "  auto msg = "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
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
        "  auto runtime = std::make_shared<::phaser::MessageRuntime>(pb, "
        "size);\n"
        "  return "
     << MessageName(message_)
     << "(runtime, pb->message);\n"
        "}\n\n";
  os << "// Create a message in a dynamically resized buffer allocated from "
        "the heap.\n";
  os << MessageName(message_) << " " << MessageName(message_)
     << "::CreateDynamicMutable(size_t initial_size, "
        "std::function<absl::StatusOr<void*>(size_t)> alloc, "
        "std::function<void(void*)> free,"
        "std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc, "
        "::phaser::Tuning tuning) "
        "{\n"
        "  absl::StatusOr<::toolbelt::PayloadBuffer *> pbs = "
        "::phaser::NewDynamicBuffer(initial_size, std::move(alloc), "
        "std::move(realloc), tuning);\n"
        "  if (!pbs.ok()) abort();\n"
        "  ::toolbelt::PayloadBuffer *pb = *pbs;\n"
        "  ::toolbelt::PayloadBuffer::AllocateMainMessage(&pb, "
     << MessageName(message_)
     << "::BinarySize());\n"
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

  os << "  struct " << MessageName(message_) << "FieldData {";
  os << R"(
    uint32_t num;
    struct Field {
      uint32_t number;
      uint32_t offset : 24;
      uint32_t id : 8;
)";
  // A message with no fields would otherwise emit `fields[0]`, a zero-length
  // array (a non-standard extension). Reserve one (zero-initialized, never read
  // because num == 0) element instead so the generated header stays clean.
  os << "    } fields[" << (all_fields.empty() ? size_t{1} : all_fields.size())
     << "];\n";
  os << "  };\n";

  // Generate the field data.
  os << "  static constexpr " << MessageName(message_)
     << "FieldData field_data = {\n";
  os << "    .num = " << all_fields.size() << ",\n";
  os << "    .fields = {\n";
  for (auto& field : all_fields) {
    os << "      { .number = " << field->field->number()
       << ", .offset = " << field->offset << ", .id = " << field->id << " },\n";
  }
  os << "    }\n";
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
          os << "    " << member_name << ".Populate();\n";
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
        os << "  const " << field->c_type << "& " << sanitized_field_name
           << "(size_t index) const {\n";
        os << "    return " << member_name << ".Get(index);\n";
        os << "  }\n";
        os << "  " << field->c_type << "* mutable_" << field_name
           << "(size_t index) {\n";
        os << "    return " << member_name << ".Mutable(index);\n";
        os << "  }\n";
        os << "  " << field->c_type << "* add_" << field_name << "() {\n";
        os << "    return " << member_name << ".Add();\n";
        os << "  }\n";
        os << "  const ::phaser::MessageVectorField<" << field->c_type << ">& "
           << sanitized_field_name << "() const {\n";
        os << "    " << member_name << ".Populate();\n";
        os << "    return " << member_name << ";\n";
        os << "  }\n";
        os << "  void reserve_" << field_name << "(size_t num) {\n";
        os << "    " << member_name << ".reserve(num);\n";
        os << "  }\n";
        os << "  void resize_" << field_name << "(size_t num) {\n";
        os << "    " << member_name << ".resize(num);\n";
        os << "  }\n";
        os << "  std::vector<" << field->c_type << "*> allocate_" << field_name
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

void MessageGenerator::GenerateSerializedSize(std::ostream& os, bool decl) {
  if (decl) {
    os << "  size_t SerializedSize() const;\n";
    return;
  }
  os << "size_t " << MessageName(message_) << "::SerializedSize() const {\n";
  os << "  size_t size = 0;\n";
  for (auto& field : fields_) {
    if (field->field->is_repeated()) {
      os << "  size += " << field->member_name << ".SerializedSize();\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    size += " << field->member_name << ".SerializedSize();\n";
      os << "  }\n";
    }
  }
  for (auto& [oneof, u] : unions_) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
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

void MessageGenerator::GenerateSerializer(std::ostream& os, bool decl) {
  if (decl) {
    os << "  absl::Status Serialize(::phaser::ProtoBuffer &buffer) const;\n";
    return;
  }
  os << "absl::Status " << MessageName(message_)
     << "::Serialize(::phaser::ProtoBuffer &buffer) const {\n";
  for (auto& field : fields_) {
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
  for (auto& [oneof, u] : unions_) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
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

void MessageGenerator::GenerateDeserializer(std::ostream& os, bool decl) {
  if (decl) {
    os << "  absl::Status Deserialize(::phaser::ProtoBuffer &buffer);\n";
    return;
  }
  os << "absl::Status " << MessageName(message_)
     << "::Deserialize(::phaser::ProtoBuffer &buffer) {";
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
  while (!buffer.Eof()) {
    absl::StatusOr<uint32_t> tag =
        buffer.DeserializeVarint<uint32_t, false>();
    if (!tag.ok()) {
      return tag.status();
    }
    uint32_t field_number = *tag >> ::phaser::ProtoBuffer::kFieldIdShift;
    switch (field_number) {
)XXX";
  for (auto& field : fields_) {
    os << "    case " << field->field->number() << ":\n";
    os << "      if (absl::Status status = " << field->member_name
       << ".Deserialize(buffer); !status.ok()) return status;\n";
    os << "      break;\n";
  }
  for (auto& [oneof, u] : unions_) {
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
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
  if (has_array_fields) {
    os << "\n";
    for (auto& field : fields_) {
      if (UsesArrayFacade(field->field)) {
        os << "  if (absl::Status status = " << field->member_name
           << ".FinalizeDeserialize(); !status.ok()) return status;\n";
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
  os << "  void Indent([[maybe_unused]] int indent) const {\n";
  for (auto& field : fields_) {
    os << "    " << field->member_name << ".Indent(indent);\n";
  }
  for (auto& [oneof, u] : unions_) {
    os << "    " << u->member_name << ".Indent(indent);\n";
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
      os << "  for (auto& v : msg." << field->member_name << ") {\n";
      os << "    msg." << field->member_name << ".PrintIndent(os);\n";
      if (field->field->type() ==
          google::protobuf::FieldDescriptor::TYPE_ENUM) {
        os << "    os << \"" << field->field->name() << ": \" << "
           << EnumName(field->field->enum_type())
           << "Stringizer()(v) << std::endl;\n";
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
    os << "  absl::Status CloneFrom(const T & other);\n\n";
    os << "  void CopyFrom(const ::phaser::Message& other) override{\n";
    os << "    const " << MessageName(message_) << "& m = static_cast<const "
       << MessageName(message_) << "&>(other);\n";
    os << "    (void)CloneFrom(m);\n";
    os << "  }\n\n";
    return;
  }

  // CloneFrom.
  os << "template <typename T>\n";
  os << "inline absl::Status " << MessageName(message_)
     << "::CloneFrom([[maybe_unused]] const T & other) {\n";
  if (IsRosFrontend()) {
    for (auto& field : fields_) {
      if (field->field->is_repeated()) {
        os << "  " << field->member_name << ".Clear();\n";
        if (UsesArrayFacade(field->field)) {
          const int array_size = GetArraySize(field->field);
          if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
            os << "  for (size_t i = 0; i < static_cast<size_t>(" << array_size
               << "); i++) {\n";
            os << "    if (!other." << field->member_name << "[i].empty()) {\n";
            os << "      if (absl::Status s = " << field->member_name
               << "[i].Mutable()->CloneFrom(other." << field->member_name
               << ".Get(i)); !s.ok()) return s;\n";
            os << "    }\n";
            os << "  }\n";
          } else if (field->field->type() ==
                         google::protobuf::FieldDescriptor::TYPE_STRING ||
                     field->field->type() ==
                         google::protobuf::FieldDescriptor::TYPE_BYTES) {
            os << "  for (size_t i = 0; i < static_cast<size_t>(" << array_size
               << "); i++) {\n";
            os << "    " << field->member_name << ".Set(i, other."
               << field->member_name << ".Get(i));\n";
            os << "  }\n";
          } else {
            os << "  for (size_t i = 0; i < static_cast<size_t>(" << array_size
               << "); i++) {\n";
            os << "    " << field->member_name << ".Set(i, other."
               << field->member_name << ".Get(i));\n";
            os << "  }\n";
          }
        } else if (field->field->type() ==
            google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "  for (auto& v : other." << field->member_name << ") {\n";
          os << "    auto* m = " << field->member_name << ".Add();\n";
          os << "    if (absl::Status s = m->CloneFrom(v.Msg()); !s.ok()) return "
                "s;\n";
          os << "  }\n";
        } else {
          os << "  for (auto& v : other." << field->member_name << ") {\n";
          os << "    " << field->member_name << ".Add(v);\n";
          os << "  }\n";
        }
      } else if (field->field->type() ==
                 google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        os << "  if (other." << field->member_name << ".IsPresent()) {\n";
        if (IsRosIntrinsic(field->field)) {
          os << "    " << field->member_name << ".Set(other."
             << field->member_name << ".Get());\n";
        } else {
          os << "    if (absl::Status s = " << field->member_name
             << ".Mutable()->CloneFrom(other." << field->member_name
             << ".Get()); !s.ok()) return s;\n";
        }
        os << "  } else {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
      } else if (field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_STRING ||
                 field->field->type() ==
                     google::protobuf::FieldDescriptor::TYPE_BYTES) {
        os << "  if (other." << field->member_name << ".IsPresent()) {\n";
        os << "    " << field->member_name << ".Set(other."
           << field->member_name << ".Get());\n";
        os << "  } else {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
      } else {
        os << "  if (other." << field->member_name << ".IsPresent()) {\n";
        os << "    " << field->member_name << ".Set(other."
           << field->member_name << ".Get());\n";
        os << "  } else {\n";
        os << "    " << field->member_name << ".Clear();\n";
        os << "  }\n";
      }
    }
    if (!unions_.empty()) {
      for (auto& [oneof, u] : unions_) {
        os << "  switch (other." << u->member_name << ".Discriminator()) {\n";
        for (size_t i = 0; i < u->members.size(); i++) {
          auto& field = u->members[i];
          os << "  case " << field->field->number() << ":\n";
          if (field->field->type() ==
              google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
            os << "    if (absl::Status s = " << u->member_name
               << ".template CloneFrom<" << i << ">(other." << u->member_name
               << ".template GetReference<" << i << ", "
               << MessageName(field->field->message_type()) << ">()); !s.ok()) "
                  "return s;\n";
          } else {
            os << "    if (absl::Status s = " << u->member_name
               << ".template CloneFrom<" << i << ">(other." << u->member_name
               << ".template GetValue<" << i << ", " << field->c_type
               << ">()); !s.ok()) return s;\n";
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
        os << "  for (auto& v : other." << field->field->name() << "()) {\n";
        if (field->field->type() ==
            google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "    auto* m = add_" << field->field->name() << "();\n";
          os << "    if (absl::Status s = m->CloneFrom(v.Msg()); !s.ok()) return "
                "s;\n";
        } else {
          os << "    add_" << field->field->name() << "(v);\n";
        }
        os << "  }\n";

      } else {
        os << "  if (other." << field->member_name << ".IsPresent()) {\n";
        if (field->field->type() ==
            google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
          os << "    auto* m = mutable_" << field->field->name() << "();\n";
          os << "    if (absl::Status s = m->CloneFrom(other."
             << field->field->name() << "()); !s.ok()) return s;\n";
        } else {
          os << "    set_" << field->field->name() << "(other."
             << field->field->name() << "());\n";
        }
        os << "  }\n";
      }
    }
    if (!unions_.empty()) {
      for (auto& [oneof, u] : unions_) {
        os << "  switch (other." << u->member_name << ".Discriminator()) {\n";
        for (size_t i = 0; i < u->members.size(); i++) {
          auto& field = u->members[i];
          os << "  case " << field->field->number() << ":\n";
          os << "    if (absl::Status s = " << u->member_name
             << ".template CloneFrom<" << i << ">(other." << field->field->name()
             << "()); !s.ok()) return s;\n";
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
        os << "    return m->" << u->member_name << ".Discriminator() == "
           << field->field->number() << ";\n";
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
