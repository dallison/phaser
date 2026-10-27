// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

namespace pybind11 {
class module_;
}

namespace phaser {

// Registers Python bindings for a plugin. Link the registering translation
// unit into phaser_bank_py with alwayslink = True.
using PluginPybindFn = void (*)(pybind11::module_& m);

void RegisterPluginPybind(PluginPybindFn fn);

void InitPluginPybindModules(pybind11::module_& m);

}  // namespace phaser
