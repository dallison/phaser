// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "phaser/plugins/arrow/arrow.h"
#include "phaser/runtime/plugin_pybind.h"

namespace py = pybind11;

namespace {

void InitArrowPluginPybind(py::module_& m) {
  py::module_ arrow = m.def_submodule("arrow", "Arrow columnar plugin");
  arrow.attr("OP_COLLECT_TYPES") = py::str(
      std::string(::phaser::arrow::kArrowPluginCollectTypes));
  arrow.attr("OP_WRITE") =
      py::str(std::string(::phaser::arrow::kArrowPluginWrite));
  arrow.attr("OP_READ") =
      py::str(std::string(::phaser::arrow::kArrowPluginRead));
  arrow.attr("OP_APPEND_SCHEMA") =
      py::str(std::string(::phaser::arrow::kArrowPluginAppendSchema));
  arrow.def("operations", []() { return ::phaser::arrow::ListPluginOperations(); });
}

struct ArrowPluginPybindRegistrar {
  ArrowPluginPybindRegistrar() {
    ::phaser::RegisterPluginPybind(InitArrowPluginPybind);
  }
};

const ArrowPluginPybindRegistrar kArrowPluginPybindRegistrar;

}  // namespace
