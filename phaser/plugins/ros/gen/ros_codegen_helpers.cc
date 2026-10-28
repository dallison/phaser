// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/ros/ros_codegen_helpers.h"

#include <cstdlib>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "phaser/options.pb.h"

namespace phaser {
namespace codegen {

bool ReachesThroughSingularFields(
    const google::protobuf::Descriptor* desc,
    const google::protobuf::Descriptor* target,
    absl::flat_hash_set<const google::protobuf::Descriptor*>* visited) {
  if (!visited->insert(desc).second) {
    return false;
  }
  for (int i = 0; i < desc->field_count(); ++i) {
    const auto* field = desc->field(i);
    if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE ||
        field->is_repeated() || field->containing_oneof() != nullptr) {
      continue;
    }
    if (field->message_type() == target ||
        ReachesThroughSingularFields(field->message_type(), target, visited)) {
      return true;
    }
  }
  return false;
}
bool ClearReleasesNothing(const google::protobuf::Descriptor* desc) {
  for (int i = 0; i < desc->field_count(); ++i) {
    const auto* field = desc->field(i);
    if (field->is_repeated() || field->real_containing_oneof() != nullptr) {
      return false;
    }
    switch (field->type()) {
      case google::protobuf::FieldDescriptor::TYPE_STRING:
      case google::protobuf::FieldDescriptor::TYPE_BYTES:
      case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      case google::protobuf::FieldDescriptor::TYPE_GROUP:
        return false;
      default:
        break;
    }
  }
  return true;
}

// Whether every field of 'desc' is a singular scalar outside a oneof, so a
// PhaserWriter can set them all.
bool HasOnlyPlainScalars(const google::protobuf::Descriptor* desc) {
  for (int i = 0; i < desc->field_count(); ++i) {
    if (desc->field(i)->containing_oneof() != nullptr) {
      return false;
    }
  }
  return ClearReleasesNothing(desc);
}
bool IsFixedWireType(const google::protobuf::FieldDescriptor* field) {
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

bool IsRosBuiltinType(const std::string& type) {
  static const absl::flat_hash_set<std::string> builtin_types = {
      "bool",  "byte",   "char",   "duration", "float32", "float64",
      "int8",  "int16",  "int32",  "int64",    "string",  "time",
      "uint8", "uint16", "uint32", "uint64",
  };
  return builtin_types.contains(RosBaseType(type));
}

std::string RosDataType(const google::protobuf::Descriptor* message) {
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

std::string RosArraySuffix(
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

std::string RosFieldType(
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

std::string RosFieldName(
    const google::protobuf::FieldDescriptor* field) {
  if (field->options().HasExtension(phaser::ros_field)) {
    const auto& metadata = field->options().GetExtension(phaser::ros_field);
    if (!metadata.name().empty()) {
      return metadata.name();
    }
  }
  return std::string(field->name());
}

bool IsRosConstantOnlyMessageField(
    const google::protobuf::FieldDescriptor* field) {
  if (field->type() != google::protobuf::FieldDescriptor::TYPE_ENUM ||
      !field->options().HasExtension(phaser::ros_field)) {
    return false;
  }
  const auto& metadata = field->options().GetExtension(phaser::ros_field);
  const std::string& definition = metadata.nested_md5_text();
  if (metadata.nested_data_type().empty() || definition.empty()) {
    return false;
  }
  bool saw_constant = false;
  size_t line_start = 0;
  while (line_start < definition.size()) {
    const size_t line_end = definition.find('\n', line_start);
    const std::string_view line(
        definition.data() + line_start,
        (line_end == std::string::npos ? definition.size() : line_end) -
            line_start);
    if (!line.empty()) {
      if (line.find('=') == std::string_view::npos) {
        return false;
      }
      saw_constant = true;
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return saw_constant;
}

std::vector<std::string> RosConstantDeclarations(
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

std::string RosSourceDefinition(
    const google::protobuf::Descriptor* message) {
  const auto& message_metadata =
      message->options().GetExtension(phaser::ros_message);
  if (!message_metadata.source_definition().empty()) {
    return message_metadata.source_definition();
  }
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
      source_definition = field_metadata.nested_source_definition().empty()
                              ? field_metadata.nested_md5_text() + "\n"
                              : field_metadata.nested_source_definition();
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
    if (!definition->empty() && definition->back() == '\n' &&
        (definition->size() == 1 ||
         (*definition)[definition->size() - 2] != '\n')) {
      definition->push_back('\n');
    }
    *definition += std::string(80, '=') + "\n";
    *definition += "MSG: " + data_type + "\n";
    *definition += source_definition;
    if (dependency != nullptr) {
      AppendRosDependencies(dependency, seen, definition);
    }
  }
}

std::string RosFullDefinition(
    const google::protobuf::Descriptor* message) {
  std::string definition = RosSourceDefinition(message);
  absl::flat_hash_set<std::string> seen;
  AppendRosDependencies(message, &seen, &definition);
  return definition;
}

std::string ROSPrimitiveCppType(
    const google::protobuf::FieldDescriptor* field) {
  switch (field->type()) {
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
      break;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      return "";
  }
  std::string ros_type = RosFieldType(field);
  if (const size_t array = ros_type.find('['); array != std::string::npos) {
    ros_type.resize(array);
  }
  if (ros_type == "bool") {
    return "bool";
  }
  if (ros_type == "byte" || ros_type == "int8") {
    return "int8_t";
  }
  if (ros_type == "char" || ros_type == "uint8") {
    return "uint8_t";
  }
  if (ros_type == "int16") {
    return "int16_t";
  }
  if (ros_type == "uint16") {
    return "uint16_t";
  }
  if (ros_type == "int32") {
    return "int32_t";
  }
  if (ros_type == "uint32") {
    return "uint32_t";
  }
  if (ros_type == "int64") {
    return "int64_t";
  }
  if (ros_type == "uint64") {
    return "uint64_t";
  }
  if (ros_type == "float32") {
    return "float";
  }
  if (ros_type == "float64") {
    return "double";
  }
  return "";
}

std::string ProtobufPrimitiveCppType(
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

// The size of a C++ type that ROSPrimitiveCppType() or
// ProtobufPrimitiveCppType() returns.
size_t ROSCppTypeSize(const std::string& type) {
  static const absl::flat_hash_map<std::string, size_t> kSizes = {
      {"bool", 1},     {"int8_t", 1},   {"uint8_t", 1},  {"int16_t", 2},
      {"uint16_t", 2}, {"int32_t", 4},  {"uint32_t", 4}, {"float", 4},
      {"int64_t", 8},  {"uint64_t", 8}, {"double", 8},
  };
  const auto it = kSizes.find(type);
  if (it == kSizes.end()) {
    abort();
  }
  return it->second;
}
std::string ROSBulkPrimitiveType(
    const google::protobuf::FieldDescriptor* field) {
  const std::string protobuf_type = ProtobufPrimitiveCppType(field);
  return ROSPrimitiveCppType(field) == protobuf_type ? protobuf_type : "";
}

}  // namespace codegen
}  // namespace phaser
