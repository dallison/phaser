// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Generator plugins depend on //phaser/compiler:message_generator and include
// this header. Register a GeneratorPlugin with alwayslink = True on the phaser
// binary.

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_gen.h"

namespace phaser {

// Grants generator plugins access to MessageGenerator internals without
// plugin-named friend classes in the core header.
class MessageCodegenHost {
 public:
  explicit MessageCodegenHost(MessageGenerator& generator)
      : generator_(generator) {}

  const google::protobuf::Descriptor* message() const {
    return generator_.message_;
  }

  bool generate_ros_metadata() const {
    return generator_.generate_ros_metadata_;
  }

  const std::vector<std::shared_ptr<FieldInfo>>& fields_in_order() const {
    return generator_.fields_in_order_;
  }

  std::string MessageName(const google::protobuf::Descriptor* desc,
                          bool is_ref = false) const {
    return generator_.MessageName(desc, is_ref);
  }

  std::string FieldCType(const google::protobuf::FieldDescriptor* field) const {
    return generator_.FieldCType(field);
  }

  std::string EnumName(const google::protobuf::EnumDescriptor* desc) const {
    return generator_.EnumName(desc);
  }

  bool IsAny(const google::protobuf::FieldDescriptor* field) const {
    return generator_.IsAny(field);
  }

  bool UsesArrayFacade(const google::protobuf::FieldDescriptor* field) const {
    return generator_.UsesArrayFacade(field);
  }

  int GetArraySize(const google::protobuf::FieldDescriptor* field) const {
    return generator_.GetArraySize(field);
  }

  bool IsRosFrontend() const { return generator_.IsRosFrontend(); }

  bool IsRosIntrinsic(const google::protobuf::FieldDescriptor* field) const {
    return generator_.IsRosIntrinsic(field);
  }

  bool IsRosHeader(const google::protobuf::Descriptor* desc) const {
    return generator_.IsRosHeader(desc);
  }

  bool IsRosDuration(const google::protobuf::Descriptor* desc) const {
    return generator_.IsRosDuration(desc);
  }

  void GenerateROSSerialization(std::ostream& os, bool decl) {
    generator_.GenerateROSSerialization(os, decl);
  }

  void GenerateROSDirectSerialization(std::ostream& os, bool decl) {
    generator_.GenerateROSDirectSerialization(os, decl);
  }

  void GenerateROSDirectDeserialization(std::ostream& os, bool decl) {
    generator_.GenerateROSDirectDeserialization(os, decl);
  }

  void GenerateArrowSchemaField(std::ostream& os,
                                const google::protobuf::FieldDescriptor* field,
                                const std::string& name, bool nullable) {
    generator_.GenerateArrowSchemaField(os, field, name, nullable);
  }

  void GenerateArrowWriteField(std::ostream& os, const FieldInfo& field,
                               int index) {
    generator_.GenerateArrowWriteField(os, field, index);
  }

  void GenerateArrowReadField(std::ostream& os, const FieldInfo& field,
                              int index) {
    generator_.GenerateArrowReadField(os, field, index);
  }

  void GenerateArrowCollectField(std::ostream& os, const FieldInfo& field) {
    generator_.GenerateArrowCollectField(os, field);
  }

  bool ArrowFieldNullable(
      const google::protobuf::FieldDescriptor* field) const {
    return generator_.ArrowFieldNullable(field);
  }

  const char* ArrowScalarAdd(
      const google::protobuf::FieldDescriptor* field) const {
    return generator_.ArrowScalarAdd(field);
  }

  const char* ArrowScalarWrite(
      const google::protobuf::FieldDescriptor* field) const {
    return generator_.ArrowScalarWrite(field);
  }

  const char* ArrowScalarRead(
      const google::protobuf::FieldDescriptor* field) const {
    return generator_.ArrowScalarRead(field);
  }

  std::string ArrowQuoted(std::string_view text) const {
    return generator_.ArrowQuoted(text);
  }

 private:
  MessageGenerator& generator_;
};

}  // namespace phaser
