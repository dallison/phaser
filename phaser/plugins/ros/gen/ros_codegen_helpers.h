// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "google/protobuf/descriptor.h"

namespace phaser {
namespace codegen {

bool ReachesThroughSingularFields(
    const google::protobuf::Descriptor* desc,
    const google::protobuf::Descriptor* target,
    absl::flat_hash_set<const google::protobuf::Descriptor*>* visited);

bool ClearReleasesNothing(const google::protobuf::Descriptor* desc);
bool HasOnlyPlainScalars(const google::protobuf::Descriptor* desc);

bool IsFixedWireType(const google::protobuf::FieldDescriptor* field);

std::string RosDataType(const google::protobuf::Descriptor* message);
std::string RosFieldType(const google::protobuf::FieldDescriptor* field);
std::string RosFieldName(const google::protobuf::FieldDescriptor* field);
bool IsRosBuiltinType(const std::string& type);
bool IsRosConstantOnlyMessageField(
    const google::protobuf::FieldDescriptor* field);
std::vector<std::string> RosConstantDeclarations(
    const google::protobuf::Descriptor* message);
std::string RosSourceDefinition(const google::protobuf::Descriptor* message);
std::string RosFullDefinition(const google::protobuf::Descriptor* message);

std::string ROSPrimitiveCppType(
    const google::protobuf::FieldDescriptor* field);
std::string ProtobufPrimitiveCppType(
    const google::protobuf::FieldDescriptor* field);
size_t ROSCppTypeSize(const std::string& type);
std::string ROSBulkPrimitiveType(
    const google::protobuf::FieldDescriptor* field);

}  // namespace codegen
}  // namespace phaser
