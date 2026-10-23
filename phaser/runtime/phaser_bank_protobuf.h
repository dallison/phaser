// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <string_view>

#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.h"

namespace phaser {

// Registers the serialized FileDescriptorProto embedded in a generated Phaser
// source. Generated code calls this from a static initializer. The bytes must
// remain valid for the rest of the process.
void RegisterProtobufFileDescriptor(std::string_view serialized_file);

// The protobuf descriptor for a message registered in the Phaser bank.
// Descriptors are built from the FileDescriptorProtos embedded in the linked
// Phaser libraries. A linked .pb.cc is not required. descriptor.proto, and any
// file already registered by libprotobuf, are taken from the generated
// descriptor pool.
absl::StatusOr<const google::protobuf::Descriptor*>
PhaserBankProtobufDescriptor(std::string_view full_name);

}  // namespace phaser
