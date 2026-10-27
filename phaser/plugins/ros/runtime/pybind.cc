// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>

#include "phaser/runtime/plugin_pybind.h"
#include "phaser/runtime/ros_wireformat.h"

namespace py = pybind11;

namespace {

void InitRosPluginPybind(py::module_& m) {
  py::module_ ros = m.def_submodule("ros", "ROS wire-format plugin");
  ros.attr("FORMAT") =
      py::str(::phaser::kRosFormat.data(), ::phaser::kRosFormat.size());
}

struct RosPluginPybindRegistrar {
  RosPluginPybindRegistrar() {
    ::phaser::RegisterPluginPybind(InitRosPluginPybind);
  }
};

const RosPluginPybindRegistrar kRosPluginPybindRegistrar;

}  // namespace
