// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/plugin_pybind.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <pybind11/pybind11.h>

namespace phaser {
namespace {

std::vector<PluginPybindFn>& PluginPybinds() {
  static auto* fns = new std::vector<PluginPybindFn>();
  return *fns;
}

}  // namespace

void RegisterPluginPybind(PluginPybindFn fn) {
  if (fn == nullptr) {
    std::fprintf(stderr, "phaser: plugin pybind function is null\n");
    std::abort();
  }
  PluginPybinds().push_back(fn);
}

void InitPluginPybindModules(pybind11::module_& m) {
  for (PluginPybindFn fn : PluginPybinds()) {
    fn(m);
  }
}

}  // namespace phaser
