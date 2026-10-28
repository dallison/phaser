// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>

#include "phaser/runtime/plugin_pybind.h"
#include "phaser/runtime/wireformat.h"

namespace py = pybind11;

namespace {

void InitProtobufPluginPybind(py::module_& m) {
  py::module_ protobuf =
      m.def_submodule("protobuf", "Protobuf wire-format plugin");
  protobuf.attr("FORMAT") = py::str(::phaser::kProtobufFormat.data(),
                                    ::phaser::kProtobufFormat.size());
}

struct ProtobufPluginPybindRegistrar {
  ProtobufPluginPybindRegistrar() {
    ::phaser::RegisterPluginPybind(InitProtobufPluginPybind);
  }
};

const ProtobufPluginPybindRegistrar kProtobufPluginPybindRegistrar;

}  // namespace
