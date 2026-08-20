// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <string>
#include <string_view>

namespace phaser {

/// Computes the lowercase hexadecimal MD5 digest of `input`.
std::string Md5(std::string_view input);

}  // namespace phaser
