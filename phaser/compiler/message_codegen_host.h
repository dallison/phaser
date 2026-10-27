// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Include this header from a generator plugin target. Link the plugin library
// into the phaser compiler binary with alwayslink = True.
//
// Plugin code typically:
// 1. Defines a GeneratorPlugin with hooks and a static registrar calling
//    RegisterGeneratorPlugin.
// 2. Uses friend access via codegen::*MessageCodegen helpers, or calls public
//    MessageGenerator methods.
// 3. Depends on //phaser/compiler:message_generator only (not the phaser
//    binary).
//
// Set priority so BankInfo and wire hooks emit fields in struct order (built-in
// wire plugins use 0=ros, 1=protobuf; columnar uses 20+).

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_gen.h"
#include "phaser/compiler/ros_codegen_helpers.h"
