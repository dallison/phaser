// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_codegen_host.h"
#include "phaser/plugins/ros/gen/ros_wire_codegen.h"

#include "phaser/options.pb.h"

namespace phaser {


namespace {

void EmitRosWireMessageHeader(MessageGenerator& generator, std::ostream& os) {
  MessageCodegenHost host(generator);
  ros_wire_codegen::GeneratePhaserWriter(host, os);
  ros_wire_codegen::GenerateROSSerialization(host, os, true);
  ros_wire_codegen::GenerateROSDirectSerialization(host, os, true);
  ros_wire_codegen::GenerateROSDirectDeserialization(host, os, true);
}

void EmitRosWireMessageSource(MessageGenerator& generator, std::ostream& os) {
  MessageCodegenHost host(generator);
  ros_wire_codegen::GenerateROSSerialization(host, os, false);
  ros_wire_codegen::GenerateROSDirectSerialization(host, os, false);
  ros_wire_codegen::GenerateROSDirectDeserialization(host, os, false);
}

void EmitRosBankInfoFields(MessageGenerator& generator, std::ostream& os) {
  MessageCodegenHost host(generator);
  const std::string name = host.MessageName(host.message());
  os << "  .protobuf_to_ros = " << name << "::ProtobufToROS,\n";
  os << "  .phaser_to_ros = " << name << "::PhaserToROS,\n";
  const bool has_ros_metadata =
      host.generate_ros_metadata() ||
      host.message()->options().HasExtension(phaser::ros_message);
  if (has_ros_metadata) {
    os << "  .ros_data_type = " << name << "::RosDataType,\n";
    os << "  .ros_md5 = " << name << "::RosMd5,\n";
    os << "  .ros_definition = " << name << "::RosDefinition,\n";
  } else {
    os << "  .ros_data_type = nullptr,\n";
    os << "  .ros_md5 = nullptr,\n";
    os << "  .ros_definition = nullptr,\n";
  }
  os << "  .ros_to_protobuf = " << name << "::ROSToProtobuf,\n";
}

const GeneratorPlugin kRosGeneratorPlugin = {
    "ros",
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    EmitRosBankInfoFields,
    EmitRosWireMessageHeader,
    EmitRosWireMessageSource,
};

struct RosGeneratorPluginRegistrar {
  RosGeneratorPluginRegistrar() { RegisterGeneratorPlugin(kRosGeneratorPlugin); }
};

RosGeneratorPluginRegistrar g_ros_generator_plugin_registrar;

}  // namespace

}  // namespace phaser
