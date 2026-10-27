// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>

#include "phaser/plugins/avro/avro.h"
#include "phaser/runtime/plugin_pybind.h"

namespace py = pybind11;

namespace {

void InitAvroPluginPybind(py::module_& m) {
  py::module_ avro = m.def_submodule("avro", "Avro columnar plugin");
  avro.attr("FORMAT") =
      py::str(::phaser::avro::kFormat.data(), ::phaser::avro::kFormat.size());
}

struct AvroPluginPybindRegistrar {
  AvroPluginPybindRegistrar() {
    ::phaser::RegisterPluginPybind(InitAvroPluginPybind);
  }
};

const AvroPluginPybindRegistrar kAvroPluginPybindRegistrar;

}  // namespace
