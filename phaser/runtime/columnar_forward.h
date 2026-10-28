// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Forward declarations for columnar APIs declared on generated messages. The
// implementations live in the generated .phaser.cc and link the plugins.

struct ArrowSchema;
struct ArrowArray;

namespace phaser {
namespace arrow {
class SchemaBuilder;
class TypeSet;
class FieldWriter;
class FieldReader;
}  // namespace arrow
}  // namespace phaser
