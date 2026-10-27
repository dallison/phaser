// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/plugins/avro/avro.h"
#include "phaser/runtime/message_property.h"
#include "phaser/plugins/parquet/parquet.h"
#include "phaser/runtime/phaser_bank.h"
#include "phaser/runtime/plugin_pybind.h"
#include "phaser/runtime/ros_avro_record.h"

namespace py = pybind11;

namespace {

void RaiseIfError(const absl::Status& status) {
  if (!status.ok()) {
    throw std::runtime_error(std::string(status.message()));
  }
}

template <typename T>
T Unwrap(absl::StatusOr<T> value) {
  if (!value.ok()) {
    throw std::runtime_error(std::string(value.status().message()));
  }
  return std::move(value.value());
}

py::bytes FinishString(std::string value) {
  return py::bytes(value.data(), value.size());
}

py::bytes ConvertNamed(const std::string& full_name, std::string_view from,
                       py::bytes payload, std::string_view to) {
  std::string_view view = payload;
  std::string out;
  RaiseIfError(::phaser::PhaserBankConvert(
      full_name, from, absl::Span<const char>(view.data(), view.size()), to,
      &out));
  return FinishString(std::move(out));
}

}  // namespace

#ifndef PHASER_BANK_PY_MODULE
#define PHASER_BANK_PY_MODULE phaser_bank_py
#endif

PYBIND11_MODULE(PHASER_BANK_PY_MODULE, m) {
  m.doc() = "Phaser bank conversions and ROS connection metadata";

  m.attr("FORMAT_PHASER") = py::str(::phaser::kPhaserFormat.data(),
                                   ::phaser::kPhaserFormat.size());
  m.attr("FORMAT_PROTOBUF") = py::str(::phaser::kProtobufFormat.data(),
                                      ::phaser::kProtobufFormat.size());
  m.attr("FORMAT_ROS") =
      py::str(::phaser::kRosFormat.data(), ::phaser::kRosFormat.size());
  m.attr("FORMAT_AVRO") = py::str(::phaser::avro::kFormat.data(),
                                  ::phaser::avro::kFormat.size());
  m.attr("FORMAT_PARQUET") = py::str(::phaser::parquet::kFormat.data(),
                                     ::phaser::parquet::kFormat.size());
  m.attr("FORMAT_STAMPED_ROS_AVRO") =
      py::str(::phaser::ros_avro_record::kFormat.data(),
              ::phaser::ros_avro_record::kFormat.size());

  m.attr("STAMPED_ROS_BYTES_AVRO_SCHEMA_JSON") =
      py::str(::phaser::ros_avro_record::kStampedRosBytesAvroSchemaJson);

  m.def(
      "protobuf_to_ros",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kProtobufFormat, payload,
                            ::phaser::kRosFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_ros",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kPhaserFormat, payload,
                            ::phaser::kRosFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "ros_to_protobuf",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kRosFormat, payload,
                            ::phaser::kProtobufFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_protobuf",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kPhaserFormat, payload,
                            ::phaser::kProtobufFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "protobuf_to_avro",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kProtobufFormat, payload,
                            ::phaser::avro::kFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_avro",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kPhaserFormat, payload,
                            ::phaser::avro::kFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "protobuf_to_parquet",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kProtobufFormat, payload,
                            ::phaser::parquet::kFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_parquet",
      [](const std::string& full_name, py::bytes payload) {
        return ConvertNamed(full_name, ::phaser::kPhaserFormat, payload,
                            ::phaser::parquet::kFormat);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "convert",
      [](const std::string& full_name, const std::string& from, py::bytes payload,
         const std::string& to) {
        return ConvertNamed(full_name, from, payload, to);
      },
      py::arg("full_name"), py::arg("from_format"), py::arg("payload"),
      py::arg("to_format"));

  m.def(
      "convert_to_stamped_ros_bytes_avro_record",
      [](const std::string& full_name, const std::string& from, py::bytes payload,
         int stamp_secs, int stamp_nsecs) {
        std::string out;
        std::string_view view = payload;
        RaiseIfError(::phaser::ros_avro_record::ConvertToStampedRosBytesAvroRecord(
            full_name, from, view, stamp_secs, stamp_nsecs, &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("from_format"), py::arg("payload"),
      py::arg("stamp_secs"), py::arg("stamp_nsecs"));

  m.def(
      "ros_connection_metadata",
      [](const std::string& full_name) {
        ::phaser::ros_avro_record::RosConnectionMetadata metadata =
            Unwrap(::phaser::ros_avro_record::PhaserBankRosConnectionMetadata(
                full_name));
        py::dict result;
        result["ros_datatype"] = metadata.ros_datatype;
        result["ros_md5"] = metadata.ros_md5;
        result["ros_message_definition"] = metadata.ros_message_definition;
        result["protobuf_full_name"] = metadata.protobuf_full_name;
        return result;
      },
      py::arg("full_name"));

  m.def(
      "ros_datatype",
      [](const std::string& full_name) {
        return Unwrap(::phaser::CallMessageProperty(
            ::phaser::kMessagePropertyRosDataType, full_name));
      },
      py::arg("full_name"));

  m.def(
      "ros_md5",
      [](const std::string& full_name) {
        return Unwrap(::phaser::CallMessageProperty(
            ::phaser::kMessagePropertyRosMd5, full_name));
      },
      py::arg("full_name"));

  m.def(
      "ros_definition",
      [](const std::string& full_name) {
        return Unwrap(::phaser::CallMessageProperty(
            ::phaser::kMessagePropertyRosDefinition, full_name));
      },
      py::arg("full_name"));

  m.def(
      "protobuf_full_name",
      [](const std::string& full_name) {
        return Unwrap(::phaser::CallMessageProperty(
            ::phaser::kMessagePropertyProtobufFullName, full_name));
      },
      py::arg("full_name"));

  m.def(
      "protobuf_name",
      [](const std::string& full_name) {
        return Unwrap(::phaser::CallMessageProperty(
            ::phaser::kMessagePropertyProtobufName, full_name));
      },
      py::arg("full_name"));

  m.def(
      "binary_size",
      [](const std::string& full_name) {
        return Unwrap(::phaser::PhaserBankBinarySize(full_name));
      },
      py::arg("full_name"));

  m.def("registered_formats",
        []() { return ::phaser::PhaserBankRegisteredFormats(); });
  m.def("has_conversion", [](const std::string& from, const std::string& to) {
    return ::phaser::PhaserBankHasConversion(from, to);
  });
  m.def("conversion_edges", []() {
    py::list edges;
    for (const auto& [from, to] : ::phaser::PhaserBankConversionEdges()) {
      edges.append(py::make_tuple(from, to));
    }
    return edges;
  });

  m.def(
      "message_property",
      [](const std::string& property, const std::string& full_name) {
        return Unwrap(
            ::phaser::CallMessageProperty(property, full_name));
      },
      py::arg("property"), py::arg("full_name"));
  m.def("message_properties",
        []() { return ::phaser::ListMessageProperties(); });

  m.attr("PROP_ROS_DATATYPE") =
      py::str(::phaser::kMessagePropertyRosDataType.data(),
              ::phaser::kMessagePropertyRosDataType.size());
  m.attr("PROP_ROS_MD5") = py::str(::phaser::kMessagePropertyRosMd5.data(),
                                    ::phaser::kMessagePropertyRosMd5.size());
  m.attr("PROP_ROS_DEFINITION") =
      py::str(::phaser::kMessagePropertyRosDefinition.data(),
              ::phaser::kMessagePropertyRosDefinition.size());
  m.attr("PROP_PROTOBUF_FULL_NAME") =
      py::str(::phaser::kMessagePropertyProtobufFullName.data(),
              ::phaser::kMessagePropertyProtobufFullName.size());
  m.attr("PROP_PROTOBUF_NAME") =
      py::str(::phaser::kMessagePropertyProtobufName.data(),
              ::phaser::kMessagePropertyProtobufName.size());

  ::phaser::InitPluginPybindModules(m);
}
