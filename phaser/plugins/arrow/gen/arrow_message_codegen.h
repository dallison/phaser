// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace google::protobuf {
class FieldDescriptor;
}

namespace phaser {

class MessageCodegenHost;
struct FieldInfo;

namespace arrow_codegen {

std::string ArrowQuoted(MessageCodegenHost& host, std::string_view text);
bool ArrowFieldNullable(MessageCodegenHost& host,
                        const google::protobuf::FieldDescriptor* field);
const char* ArrowScalarAdd(MessageCodegenHost& host,
                           const google::protobuf::FieldDescriptor* field);
const char* ArrowScalarWrite(MessageCodegenHost& host,
                             const google::protobuf::FieldDescriptor* field);
const char* ArrowScalarRead(MessageCodegenHost& host,
                            const google::protobuf::FieldDescriptor* field);
void GenerateArrowSchemaField(MessageCodegenHost& host, std::ostream& os,
                             const google::protobuf::FieldDescriptor* field,
                             const std::string& name, bool nullable);
void GenerateArrowWriteField(MessageCodegenHost& host, std::ostream& os,
                             const FieldInfo& field, int index);
void GenerateArrowReadField(MessageCodegenHost& host, std::ostream& os,
                            const FieldInfo& field, int index);
void GenerateArrowCollectField(MessageCodegenHost& host, std::ostream& os,
                               const FieldInfo& field);

}  // namespace arrow_codegen
}  // namespace phaser
