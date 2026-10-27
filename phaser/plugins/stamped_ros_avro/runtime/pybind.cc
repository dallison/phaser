// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>

#include "phaser/runtime/plugin_pybind.h"
#include "phaser/runtime/ros_avro_record.h"

namespace py = pybind11;

namespace {

void InitStampedRosAvroPluginPybind(py::module_& m) {
  py::module_ plugin = m.def_submodule("stamped_ros_avro",
                                       "Stamped ROS Avro record plugin");
  plugin.attr("FORMAT") = py::str(::phaser::ros_avro_record::kFormat.data(),
                                  ::phaser::ros_avro_record::kFormat.size());
}

struct StampedRosAvroPluginPybindRegistrar {
  StampedRosAvroPluginPybindRegistrar() {
    ::phaser::RegisterPluginPybind(InitStampedRosAvroPluginPybind);
  }
};

const StampedRosAvroPluginPybindRegistrar kStampedRosAvroPluginPybindRegistrar;

}  // namespace
