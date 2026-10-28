// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>

#include "phaser/plugins/parquet/parquet.h"
#include "phaser/runtime/plugin_pybind.h"

namespace py = pybind11;

namespace {

void InitParquetPluginPybind(py::module_& m) {
  py::module_ parquet = m.def_submodule("parquet", "Parquet columnar plugin");
  parquet.attr("FORMAT") = py::str(::phaser::parquet::kFormat.data(),
                                   ::phaser::parquet::kFormat.size());
}

struct ParquetPluginPybindRegistrar {
  ParquetPluginPybindRegistrar() {
    ::phaser::RegisterPluginPybind(InitParquetPluginPybind);
  }
};

const ParquetPluginPybindRegistrar kParquetPluginPybindRegistrar;

}  // namespace
