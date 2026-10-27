// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace phaser {

// One named property of a message. `name` and `invoke` stay valid for the
// process. `invoke` returns the property value for a protobuf full name.
using MessagePropertyFn = absl::StatusOr<std::string> (*)(std::string_view full_name);

struct MessagePropertyOp {
  const char* name;
  MessagePropertyFn invoke;
};

inline constexpr std::string_view kMessagePropertyRosDataType = "ros-datatype";
inline constexpr std::string_view kMessagePropertyRosMd5 = "ros-md5";
inline constexpr std::string_view kMessagePropertyRosDefinition = "ros-definition";
inline constexpr std::string_view kMessagePropertyProtobufFullName =
    "protobuf-full-name";
inline constexpr std::string_view kMessagePropertyProtobufName = "protobuf-name";

// Adds properties addressed by name. A second registration of the same name
// fails. The built-in ROS and protobuf properties are installed on the first
// call.
absl::Status RegisterMessagePropertyPlugin(
    absl::Span<const MessagePropertyOp> properties);

// Looks up `property` and returns it for the message named `full_name`.
absl::StatusOr<std::string> CallMessageProperty(std::string_view property,
                                                std::string_view full_name);

std::vector<std::string> ListMessageProperties();

}  // namespace phaser
