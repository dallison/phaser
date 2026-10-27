// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <iosfwd>

namespace phaser {

class MessageCodegenHost;

namespace protobuf_wire_codegen {

void GenerateProtobufSerialization(MessageCodegenHost& host, std::ostream& os);
void GenerateSerializedSize(MessageCodegenHost& host, std::ostream& os,
                            bool decl);
void GenerateSerializer(MessageCodegenHost& host, std::ostream& os, bool decl);
void GenerateDeserializer(MessageCodegenHost& host, std::ostream& os, bool decl);

}  // namespace protobuf_wire_codegen
}  // namespace phaser
