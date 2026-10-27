// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <iosfwd>

namespace phaser {

class MessageCodegenHost;

namespace ros_wire_codegen {

void GeneratePhaserWriter(MessageCodegenHost& host, std::ostream& os);
void GenerateROSSerialization(MessageCodegenHost& host, std::ostream& os,
                              bool decl);
void GenerateROSDirectSerialization(MessageCodegenHost& host, std::ostream& os,
                                    bool decl);
void GenerateROSDirectDeserialization(MessageCodegenHost& host, std::ostream& os,
                                      bool decl);

}  // namespace ros_wire_codegen
}  // namespace phaser
