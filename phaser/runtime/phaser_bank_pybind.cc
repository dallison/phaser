// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "phaser/runtime/phaser_bank.h"
#include "phaser/runtime/ros_avro_record.h"
#include "phaser/runtime/ros_wireformat.h"
#include "phaser/runtime/wireformat.h"

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

py::bytes RosBufferBytes(::phaser::ROSBuffer& buffer) {
  absl::Span<const char> span = buffer.AsSpan();
  return py::bytes(span.data(), span.size());
}

py::bytes ProtoBufferBytes(::phaser::ProtoBuffer& buffer) {
  std::string value = buffer.AsString();
  return py::bytes(value.data(), value.size());
}

}  // namespace

#ifndef PHASER_BANK_PY_MODULE
#define PHASER_BANK_PY_MODULE phaser_bank_py
#endif

PYBIND11_MODULE(PHASER_BANK_PY_MODULE, m) {
  m.doc() = "Phaser bank conversions and ROS connection metadata";

  py::enum_<::phaser::BankInputType>(m, "BankInputType")
      .value("PHASER", ::phaser::BankInputType::kPhaser)
      .value("PROTOBUF", ::phaser::BankInputType::kProtobuf)
      .value("ROS", ::phaser::BankInputType::kRos);

  py::enum_<::phaser::BankOutputType>(m, "BankOutputType")
      .value("PROTOBUF", ::phaser::BankOutputType::kProtobuf)
      .value("ROS", ::phaser::BankOutputType::kRos)
      .value("AVRO", ::phaser::BankOutputType::kAvro)
      .value("PARQUET", ::phaser::BankOutputType::kParquet)
      .value("STAMPED_ROS_BYTES_AVRO_RECORD",
             ::phaser::BankOutputType::kStampedRosBytesAvroRecord);

  m.attr("STAMPED_ROS_BYTES_AVRO_SCHEMA_JSON") =
      py::str(::phaser::ros_avro_record::kStampedRosBytesAvroSchemaJson);

  m.def(
      "protobuf_to_ros",
      [](const std::string& full_name, py::bytes payload) {
        std::string_view view = payload;
        ::phaser::ROSBuffer buffer;
        RaiseIfError(::phaser::PhaserBankProtobufToROS(full_name, view, buffer));
        return RosBufferBytes(buffer);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_ros",
      [](const std::string& full_name, py::bytes payload) {
        std::string_view view = payload;
        ::phaser::ROSBuffer buffer;
        RaiseIfError(
            ::phaser::PhaserBankPhaserToROS(full_name, view, buffer));
        return RosBufferBytes(buffer);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "ros_to_protobuf",
      [](const std::string& full_name, py::bytes payload) {
        std::string_view view = payload;
        ::phaser::ProtoBuffer buffer;
        RaiseIfError(::phaser::PhaserBankRosToProtobuf(full_name, view, buffer));
        return ProtoBufferBytes(buffer);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_protobuf",
      [](const std::string& full_name, py::bytes payload) {
        std::string_view view = payload;
        ::phaser::ProtoBuffer buffer;
        RaiseIfError(
            ::phaser::PhaserBankPhaserToProtobuf(full_name, view, buffer));
        return ProtoBufferBytes(buffer);
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "protobuf_to_avro",
      [](const std::string& full_name, py::bytes payload) {
        std::string out;
        RaiseIfError(::phaser::PhaserBankProtobufToAvro(
            full_name, std::string_view(payload), &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_avro",
      [](const std::string& full_name, py::bytes payload) {
        std::string out;
        std::string_view view = payload;
        RaiseIfError(::phaser::PhaserBankPhaserToAvro(
            full_name, view, &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "protobuf_to_parquet",
      [](const std::string& full_name, py::bytes payload) {
        std::string out;
        RaiseIfError(::phaser::PhaserBankProtobufToParquet(
            full_name, std::string_view(payload), &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "phaser_to_parquet",
      [](const std::string& full_name, py::bytes payload) {
        std::string out;
        std::string_view view = payload;
        RaiseIfError(::phaser::PhaserBankPhaserToParquet(full_name, view, &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("payload"));

  m.def(
      "convert",
      [](const std::string& full_name, ::phaser::BankInputType input,
         py::bytes payload, ::phaser::BankOutputType output) {
        std::string out;
        std::string_view view = payload;
        RaiseIfError(::phaser::PhaserBankConvert(full_name, input, view, output,
                                                 &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("input"), py::arg("payload"),
      py::arg("output"));

  m.def(
      "convert_to_stamped_ros_bytes_avro_record",
      [](const std::string& full_name, ::phaser::BankInputType input,
         py::bytes payload, int stamp_secs, int stamp_nsecs) {
        std::string out;
        std::string_view view = payload;
        RaiseIfError(::phaser::PhaserBankConvertToStampedRosBytesAvroRecord(
            full_name, input, view, stamp_secs, stamp_nsecs, &out));
        return FinishString(std::move(out));
      },
      py::arg("full_name"), py::arg("input"), py::arg("payload"),
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
        return std::string(
            Unwrap(::phaser::PhaserBankRosDataType(full_name)));
      },
      py::arg("full_name"));

  m.def(
      "ros_md5",
      [](const std::string& full_name) {
        return Unwrap(::phaser::PhaserBankRosMd5(full_name));
      },
      py::arg("full_name"));

  m.def(
      "ros_definition",
      [](const std::string& full_name) {
        return std::string(
            Unwrap(::phaser::PhaserBankRosDefinition(full_name)));
      },
      py::arg("full_name"));

  m.def(
      "protobuf_full_name",
      [](const std::string& full_name) {
        return std::string(
            Unwrap(::phaser::PhaserBankProtobufFullName(full_name)));
      },
      py::arg("full_name"));

  m.def(
      "protobuf_name",
      [](const std::string& full_name) {
        return std::string(Unwrap(::phaser::PhaserBankProtobufName(full_name)));
      },
      py::arg("full_name"));

  m.def(
      "binary_size",
      [](const std::string& full_name) {
        return Unwrap(::phaser::PhaserBankBinarySize(full_name));
      },
      py::arg("full_name"));
}
