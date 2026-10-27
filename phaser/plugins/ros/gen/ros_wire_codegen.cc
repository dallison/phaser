// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/ros/gen/ros_wire_codegen.h"

#include "phaser/compiler/message_codegen_host.h"
#include "phaser/plugins/ros/ros_codegen_helpers.h"

#include "phaser/options.pb.h"

namespace phaser {
namespace ros_wire_codegen {

void GenerateROSFieldSize(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value_expression, const std::string& indent) {
  if (codegen::IsRosConstantOnlyMessageField(field)) {
    return;
  }
  if (const std::string ros_type = codegen::ROSPrimitiveCppType(field);
      !ros_type.empty()) {
    os << indent << "_phaser_serialized_size += sizeof(" << ros_type << ");\n";
    return;
  }
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      os << indent << "_phaser_serialized_size += 4;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      os << indent << "_phaser_serialized_size += 8;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      os << indent << "_phaser_serialized_size += 1;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << indent << "_phaser_serialized_size += 4 + (" << value_expression
         << ").size();\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (host.IsAny(field)) {
        // Any has no static ROS1 type. An absent Any is represented by its two
        // empty declared fields; a populated Any is rejected by the writer.
        os << indent << "_phaser_serialized_size += 8;\n";
      } else if (host.IsRosFrontend() && host.IsRosTime(field->message_type())) {
        os << indent << "_phaser_serialized_size += 8;\n";
      } else if (host.IsRosFrontend() && host.IsRosDuration(field->message_type())) {
        os << indent << "_phaser_serialized_size += 8;\n";
      } else if (host.IsRosFrontend() && host.IsRosHeader(field->message_type())) {
        os << indent << "_phaser_serialized_size += 16 + (" << value_expression
           << ").frame_id.size();\n";
      } else {
        const std::string type = host.MessageName(field->message_type(), true);
        os << indent << "if constexpr (" << type
           << "::PhaserROSFixedSize() != ::phaser::kROSVariableSize) {\n";
        os << indent << "  _phaser_serialized_size += " << type
           << "::PhaserROSFixedSize();\n";
        os << indent << "} else {\n";
        os << indent << "  _phaser_serialized_size += (" << value_expression
           << ").ROSSerializedSize();\n";
        os << indent << "}\n";
      }
      return;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      abort();
  }
  abort();
}

std::optional<std::string> ROSFixedSizeMessageType(MessageCodegenHost& host, 
    const google::protobuf::FieldDescriptor* field) {
  if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE ||
      host.IsAny(field)) {
    return std::nullopt;
  }
  const auto* type = field->message_type();
  if (host.IsRosFrontend() &&
      (host.IsRosTime(type) || host.IsRosDuration(type) || host.IsRosHeader(type))) {
    return std::nullopt;
  }
  return host.MessageName(type, true);
}

std::optional<std::string> ROSFixedElementSize(MessageCodegenHost& host, 
    const google::protobuf::FieldDescriptor* field) {
  if (codegen::IsRosConstantOnlyMessageField(field)) {
    return "0";
  }
  if (const std::string ros_type = codegen::ROSPrimitiveCppType(field);
      !ros_type.empty()) {
    return "sizeof(" + ros_type + ")";
  }
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return "4";
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return "8";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "1";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE: {
      if (host.IsAny(field)) {
        return "8";
      }
      const auto* type = field->message_type();
      if (host.IsRosFrontend() && (host.IsRosTime(type) || host.IsRosDuration(type))) {
        return "8";
      }
      std::optional<std::string> message_type =
          ROSFixedSizeMessageType(host, field);
      if (!message_type.has_value()) {
        return std::nullopt;
      }
      absl::flat_hash_set<const google::protobuf::Descriptor*> visited;
      if (type == host.message() ||
          codegen::ReachesThroughSingularFields(type, host.message(), &visited)) {
        return std::nullopt;
      }
      return *message_type + "::PhaserROSFixedSize()";
    }
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      return std::nullopt;
  }
  return std::nullopt;
}

void GenerateROSFixedSize(MessageCodegenHost& host, std::ostream& os) {
  os << "  static constexpr size_t PhaserROSFixedSize() {\n";
  if (host.IsRosTime(host.message()) || host.IsRosDuration(host.message())) {
    os << "    return 8;\n  }\n";
    return;
  }
  std::string terms;
  for (const auto& item : host.fields_in_order()) {
    std::optional<std::string> term;
    if (!item->IsUnion()) {
      const auto* field = item->field;
      if (!field->is_repeated()) {
        term = ROSFixedElementSize(host, field);
      } else if (const int extent = host.GetArraySize(field); extent > 0) {
        // Repeated enums are written as a bulk array of 32-bit values.
        std::optional<std::string> element =
            field->type() == google::protobuf::FieldDescriptor::TYPE_ENUM
                ? std::optional<std::string>("4")
                : ROSFixedElementSize(host, field);
        if (element.has_value()) {
          term = "::phaser::internal::ROSFixedProduct(" +
                 std::to_string(extent) + ", " + *element + ")";
        }
      }
    }
    if (!term.has_value()) {
      os << "    return ::phaser::kROSVariableSize;\n  }\n";
      return;
    }
    terms += (terms.empty() ? "" : ", ") + *term;
  }
  if (terms.empty()) {
    os << "    return 0;\n  }\n";
    return;
  }
  os << "    return ::phaser::internal::ROSFixedSum({" << terms << "});\n";
  os << "  }\n";
}

void GenerateROSFieldWrite(MessageCodegenHost& host,
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value_expression, const std::string& indent,
    bool to_cursor = false) {
  if (codegen::IsRosConstantOnlyMessageField(field)) {
    return;
  }
  auto write = [&](const std::string& expression) {
    if (to_cursor) {
      os << indent
         << "if ((_phaser_out = ::phaser::internal::WriteROSValue("
            "_phaser_out, _phaser_end, "
         << expression << ")) == nullptr) return nullptr;\n";
      return;
    }
    os << indent << "if (absl::Status _phaser_status = _phaser_buffer.Write("
       << expression << "); !_phaser_status.ok()) return _phaser_status;\n";
  };
  if (const std::string ros_type = codegen::ROSPrimitiveCppType(field);
      !ros_type.empty()) {
    write("static_cast<" + ros_type + ">(" + value_expression + ")");
    return;
  }
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      write("static_cast<int32_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      write("static_cast<int64_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      write("static_cast<uint32_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      write("static_cast<uint64_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      write("static_cast<double>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      write("static_cast<float>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      write("static_cast<bool>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      write("static_cast<int32_t>(" + value_expression + ")");
      return;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      if (to_cursor) {
        os << indent
           << "if ((_phaser_out = ::phaser::internal::WriteROSString("
              "_phaser_out, _phaser_end, "
           << value_expression << ")) == nullptr) return nullptr;\n";
        return;
      }
      os << indent
         << "if (absl::Status _phaser_status = _phaser_buffer.WriteString("
         << value_expression
         << "); !_phaser_status.ok()) return _phaser_status;\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      // The direct writers write messages themselves.
      assert(!to_cursor);
      if (host.IsAny(field)) {
        os << indent << "if ((" << value_expression << ").has_type_url() || ("
           << value_expression << ").has_value()) {\n";
        os << indent
           << "  return absl::UnimplementedError(\"ROS1 serialization of a "
              "populated google.protobuf.Any is unsupported\");\n";
        os << indent << "}\n";
        os << indent
           << "if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteString({}); "
              "!_phaser_status.ok()) return _phaser_status;\n";
        os << indent
           << "if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteString({}); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      } else if (host.IsRosFrontend() && host.IsRosTime(field->message_type())) {
        write("static_cast<uint32_t>((" + value_expression + ").sec)");
        write("static_cast<uint32_t>((" + value_expression + ").nsec)");
      } else if (host.IsRosFrontend() && host.IsRosDuration(field->message_type())) {
        write("static_cast<int32_t>((" + value_expression + ").sec)");
        write("static_cast<int32_t>((" + value_expression + ").nsec)");
      } else if (host.IsRosFrontend() && host.IsRosHeader(field->message_type())) {
        write("static_cast<uint32_t>((" + value_expression + ").seq)");
        write("static_cast<uint32_t>((" + value_expression + ").stamp.sec)");
        write("static_cast<uint32_t>((" + value_expression + ").stamp.nsec)");
        os << indent
           << "if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteString(("
           << value_expression
           << ").frame_id); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << indent << "if (absl::Status _phaser_status = ("
           << value_expression
           << ").PhaserSerializeToROS(_phaser_buffer); !_phaser_status.ok()) "
              "return _phaser_status;\n";
      }
      return;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      abort();
  }
  abort();
}

void GenerateROSFieldRead(MessageCodegenHost& host,
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& target_expression, const std::string& indent,
    bool append = false, const std::string& index_expression = "",
    int union_index = -1) {
  auto set_value = [&](const std::string& value) {
    if (union_index >= 0) {
      os << indent << target_expression << ".template Set<" << union_index
         << ">(" << value << ");\n";
    } else if (!index_expression.empty()) {
      os << indent << target_expression << ".Set(" << index_expression << ", "
         << value << ");\n";
    } else if (append) {
      os << indent << target_expression << ".Add(" << value << ");\n";
    } else {
      os << indent << target_expression << ".Set(" << value << ");\n";
    }
  };
  auto mutable_message = [&]() {
    if (union_index >= 0) {
      return target_expression + ".template Mutable<" +
             std::to_string(union_index) + ", " +
             host.MessageName(field->message_type(), true) + ">()";
    }
    if (!index_expression.empty()) {
      return target_expression + ".Mutable(" + index_expression + ")";
    }
    if (append) {
      return target_expression + ".Add()";
    }
    return target_expression + ".Mutable()";
  };
  auto read_value = [&](const std::string& type,
                        const std::string& conversion = "") {
    os << indent << "{\n";
    os << indent << "  absl::StatusOr<" << type
       << "> _phaser_value = _phaser_buffer.Read<" << type << ">();\n";
    os << indent
       << "  if (!_phaser_value.ok()) return _phaser_value.status();\n";
    const std::string value =
        conversion.empty() ? "*_phaser_value" : conversion + "(*_phaser_value)";
    set_value(value);
    os << indent << "}\n";
  };

  if (codegen::IsRosConstantOnlyMessageField(field)) {
    set_value("static_cast<" + host.EnumName(field->enum_type()) + ">(0)");
    return;
  }
  if (const std::string ros_type = codegen::ROSPrimitiveCppType(field);
      !ros_type.empty()) {
    read_value(ros_type);
    return;
  }

  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      read_value("int32_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      read_value("int64_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      read_value("uint32_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      read_value("uint64_t");
      return;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      read_value("double");
      return;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      read_value("float");
      return;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      read_value("bool");
      return;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      read_value("int32_t",
                 "static_cast<" + host.EnumName(field->enum_type()) + ">");
      return;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      os << indent << "{\n";
      os << indent
         << "  absl::StatusOr<std::string_view> _phaser_value = "
            "_phaser_buffer.ReadString();\n";
      os << indent
         << "  if (!_phaser_value.ok()) return _phaser_value.status();\n";
      set_value("*_phaser_value");
      os << indent << "}\n";
      return;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (host.IsAny(field)) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<std::string_view> _phaser_type_url = "
              "_phaser_buffer.ReadString();\n";
        os << indent
           << "  if (!_phaser_type_url.ok()) return "
              "_phaser_type_url.status();\n";
        os << indent
           << "  absl::StatusOr<std::string_view> _phaser_any_value = "
              "_phaser_buffer.ReadString();\n";
        os << indent
           << "  if (!_phaser_any_value.ok()) return "
              "_phaser_any_value.status();\n";
        os << indent
           << "  if (!_phaser_type_url->empty() || "
              "!_phaser_any_value->empty()) {\n";
        os << indent
           << "    return absl::UnimplementedError(\"ROS1 deserialization of "
              "a populated google.protobuf.Any is unsupported\");\n";
        os << indent << "  }\n";
        os << indent << "  " << mutable_message() << "->Clear();\n";
        os << indent << "}\n";
      } else if (host.IsRosFrontend() && host.IsRosTime(field->message_type())) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_sec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_nsec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
        os << indent << "  ::ros::Time _phaser_value;\n";
        os << indent << "  _phaser_value.sec = *_phaser_sec;\n";
        os << indent << "  _phaser_value.nsec = *_phaser_nsec;\n";
        set_value("_phaser_value");
        os << indent << "}\n";
      } else if (host.IsRosFrontend() && host.IsRosDuration(field->message_type())) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<int32_t> _phaser_sec = "
              "_phaser_buffer.Read<int32_t>();\n";
        os << indent
           << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
        os << indent
           << "  absl::StatusOr<int32_t> _phaser_nsec = "
              "_phaser_buffer.Read<int32_t>();\n";
        os << indent
           << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
        os << indent << "  ::ros::Duration _phaser_value;\n";
        os << indent << "  _phaser_value.sec = *_phaser_sec;\n";
        os << indent << "  _phaser_value.nsec = *_phaser_nsec;\n";
        set_value("_phaser_value");
        os << indent << "}\n";
      } else if (host.IsRosFrontend() && host.IsRosHeader(field->message_type())) {
        os << indent << "{\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_seq = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_seq.ok()) return _phaser_seq.status();\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_sec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
        os << indent
           << "  absl::StatusOr<uint32_t> _phaser_nsec = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << indent
           << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
        os << indent
           << "  absl::StatusOr<std::string_view> _phaser_frame_id = "
              "_phaser_buffer.ReadString();\n";
        os << indent
           << "  if (!_phaser_frame_id.ok()) return "
              "_phaser_frame_id.status();\n";
        if (union_index < 0 && !append && index_expression.empty()) {
          os << indent << "  auto _phaser_value = " << target_expression
             << ".Mutable();\n";
          os << indent << "  _phaser_value.seq = *_phaser_seq;\n";
          os << indent << "  _phaser_value.stamp.sec = *_phaser_sec;\n";
          os << indent << "  _phaser_value.stamp.nsec = *_phaser_nsec;\n";
          os << indent << "  _phaser_value.frame_id = *_phaser_frame_id;\n";
        } else {
          os << indent << "  auto _phaser_value = " << mutable_message()
             << ";\n";
          os << indent << "  _phaser_value->seq = *_phaser_seq;\n";
          os << indent << "  _phaser_value->stamp = "
             << "::ros::Time(*_phaser_sec, *_phaser_nsec);\n";
          os << indent << "  _phaser_value->frame_id = *_phaser_frame_id;\n";
          os << indent << "  _phaser_value->SyncToPayload();\n";
        }
        os << indent << "}\n";
      } else {
        os << indent << "{\n";
        os << indent << "  auto _phaser_message = " << mutable_message()
           << ";\n";
        // The message being read is clear, so the messages in it are too.
        os << indent << "  if (absl::Status _phaser_status = "
           << "_phaser_message->DeserializeFromROSUncleared(_phaser_buffer); "
              "!_phaser_status.ok()) return _phaser_status;\n";
        os << indent << "}\n";
      }
      return;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      abort();
  }
  abort();
}

std::string DirectProtobufValueType(MessageCodegenHost& host, 
    const google::protobuf::FieldDescriptor* field) {
  using Field = google::protobuf::FieldDescriptor;
  switch (field->type()) {
    case Field::TYPE_INT32:
    case Field::TYPE_SINT32:
    case Field::TYPE_SFIXED32:
    case Field::TYPE_ENUM:
      return "int32_t";
    case Field::TYPE_INT64:
    case Field::TYPE_SINT64:
    case Field::TYPE_SFIXED64:
      return "int64_t";
    case Field::TYPE_UINT32:
    case Field::TYPE_FIXED32:
      return "uint32_t";
    case Field::TYPE_UINT64:
    case Field::TYPE_FIXED64:
      return "uint64_t";
    case Field::TYPE_FLOAT:
      return "float";
    case Field::TYPE_DOUBLE:
      return "double";
    case Field::TYPE_BOOL:
      return "bool";
    case Field::TYPE_STRING:
    case Field::TYPE_BYTES:
    case Field::TYPE_MESSAGE:
      return "std::string_view";
    case Field::TYPE_GROUP:
      break;
  }
  abort();
}

std::string ROSFieldValueExpression(
    MessageCodegenHost& host, const std::shared_ptr<FieldInfo>& field,
    const std::shared_ptr<UnionInfo>& union_field = nullptr,
    int union_index = -1) {
  (void)host;
  if (union_field == nullptr) {
    return field->member_name + ".Get()";
  }
  if (field->field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    return union_field->member_name + ".template GetReference<" +
           std::to_string(union_index) + ", " + field->c_type + ">()";
  }
  return union_field->member_name + ".template GetValue<" +
         std::to_string(union_index) + ", " + field->c_type + ">()";
}

void GenerateDirectProtobufReadValue(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& buffer, const std::string& value,
    const std::string& indent) {
  using Field = google::protobuf::FieldDescriptor;
  const std::string type = DirectProtobufValueType(host, field);
  if (field->type() == Field::TYPE_STRING ||
      field->type() == Field::TYPE_BYTES) {
    os << indent << "absl::StatusOr<std::string_view> ros_parsed = " << buffer
       << ".DeserializeString();\n";
    os << indent << "if (!ros_parsed.ok()) return ros_parsed.status();\n";
    os << indent << value << " = *ros_parsed;\n";
    return;
  }
  if (field->type() == Field::TYPE_MESSAGE) {
    os << indent << "absl::StatusOr<absl::Span<char>> ros_parsed = " << buffer
       << ".DeserializeLengthDelimited();\n";
    os << indent << "if (!ros_parsed.ok()) return ros_parsed.status();\n";
    os << indent << value
       << " = std::string_view(ros_parsed->data(), ros_parsed->size());\n";
    return;
  }

  const bool fixed = field->type() == Field::TYPE_FIXED32 ||
                     field->type() == Field::TYPE_SFIXED32 ||
                     field->type() == Field::TYPE_FLOAT ||
                     field->type() == Field::TYPE_FIXED64 ||
                     field->type() == Field::TYPE_SFIXED64 ||
                     field->type() == Field::TYPE_DOUBLE;
  const bool is_signed = field->type() == Field::TYPE_SINT32 ||
                         field->type() == Field::TYPE_SINT64;
  os << indent << "absl::StatusOr<" << type << "> ros_parsed = " << buffer
     << (fixed ? ".DeserializeFixed<" : ".DeserializeVarint<") << type;
  if (fixed) {
    os << ">();\n";
  } else {
    os << ", " << (is_signed ? "true" : "false") << ">();\n";
  }
  os << indent << "if (!ros_parsed.ok()) return ros_parsed.status();\n";
  os << indent << value << " = *ros_parsed;\n";
}

void GenerateDirectROSWriteValue(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value, const std::string& indent) {
  using Field = google::protobuf::FieldDescriptor;
  if (codegen::IsRosConstantOnlyMessageField(field)) {
    os << indent << "static_cast<void>(" << value << ");\n";
    return;
  }
  switch (field->type()) {
    case Field::TYPE_STRING:
    case Field::TYPE_BYTES:
      os << indent << "if (absl::Status status = output.WriteString(" << value
         << "); !status.ok()) return status;\n";
      return;
    case Field::TYPE_MESSAGE:
      if (host.IsAny(field)) {
        os << indent << "{\n";
        os << indent << "  ::phaser::ProtoBuffer any_scan(" << value << ");\n";
        os << indent << "  while (!any_scan.Eof()) {\n";
        os << indent
           << "    absl::StatusOr<uint32_t> any_tag = "
              "any_scan.DeserializeVarint<uint32_t, false>();\n";
        os << indent << "    if (!any_tag.ok()) return any_tag.status();\n";
        os << indent
           << "    const uint32_t any_number = "
              "*any_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
        os << indent << "    if (any_number == 1 || any_number == 2) {\n";
        os << indent
           << "      return absl::UnimplementedError(\"ROS1 serialization of "
              "a populated google.protobuf.Any is unsupported\");\n";
        os << indent << "    }\n";
        os << indent
           << "    if (absl::Status status = any_scan.SkipTag(*any_tag); "
              "!status.ok()) return status;\n";
        os << indent << "  }\n";
        os << indent
           << "  if (absl::Status status = output.WriteString({}); "
              "!status.ok()) return status;\n";
        os << indent
           << "  if (absl::Status status = output.WriteString({}); "
              "!status.ok()) return status;\n";
        os << indent << "}\n";
      } else {
        os << indent << "if (absl::Status status = "
           << host.MessageName(field->message_type(), true) << "::ProtobufWireToROS("
           << value << ", output); !status.ok()) return status;\n";
      }
      return;
    case Field::TYPE_ENUM:
      os << indent
         << "if (absl::Status status = output.Write(static_cast<int32_t>("
         << value << ")); !status.ok()) return status;\n";
      return;
    default:
      if (const std::string ros_type = codegen::ROSPrimitiveCppType(field);
          !ros_type.empty()) {
        os << indent << "if (absl::Status status = output.Write(static_cast<"
           << ros_type << ">(" << value << ")); !status.ok()) return status;\n";
        return;
      }
      os << indent << "if (absl::Status status = output.Write(" << value
         << "); !status.ok()) return status;\n";
      return;
  }
}

void GenerateDirectProtobufSingularField(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& indent) {
  const std::string type = DirectProtobufValueType(host, field);
  os << indent << "{\n";
  os << indent << "  " << type << " ros_value{};\n";
  os << indent << "  ::phaser::ProtoBuffer ros_scan(protobuf);\n";
  os << indent << "  while (!ros_scan.Eof()) {\n";
  os << indent
     << "    absl::StatusOr<uint32_t> ros_tag = "
        "ros_scan.DeserializeVarint<uint32_t, false>();\n";
  os << indent << "    if (!ros_tag.ok()) return ros_tag.status();\n";
  os << indent
     << "    const uint32_t ros_number = "
        "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
  os << indent << "    if (ros_number == " << field->number() << ") {\n";
  os << indent << "      {\n";
  GenerateDirectProtobufReadValue(host, os, field, "ros_scan", "ros_value",
                                  indent + "        ");
  os << indent << "      }\n";
  os << indent << "    } else {\n";
  os << indent
     << "      if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
        "!status.ok()) return status;\n";
  os << indent << "    }\n";
  os << indent << "  }\n";
  GenerateDirectROSWriteValue(host, os, field, "ros_value", indent + "  ");
  os << indent << "}\n";
}

void GenerateDirectProtobufField(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& indent) {
  if (!field->is_repeated()) {
    GenerateDirectProtobufSingularField(host, os, field, indent);
    return;
  }

  const std::string type = DirectProtobufValueType(host, field);
  const int fixed_extent = host.GetArraySize(field);
  const bool fixed_wire_type = codegen::IsFixedWireType(field);
  os << indent << "{\n";
  if (fixed_extent <= 0) {
    os << indent << "  size_t ros_count = 0;\n";
    os << indent << "  ::phaser::ProtoBuffer ros_count_scan(protobuf);\n";
    os << indent << "  while (!ros_count_scan.Eof()) {\n";
    os << indent
       << "    absl::StatusOr<uint32_t> ros_tag = "
          "ros_count_scan.DeserializeVarint<uint32_t, false>();\n";
    os << indent << "    if (!ros_tag.ok()) return ros_tag.status();\n";
    os << indent
       << "    const uint32_t ros_number = "
          "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
    os << indent << "    if (ros_number == " << field->number() << ") {\n";
    if (field->is_packable()) {
      os << indent
         << "      if ((*ros_tag & 7u) == "
            "static_cast<uint32_t>(::phaser::WireType::kLengthDelimited)) {\n";
      os << indent
         << "        absl::StatusOr<absl::Span<char>> ros_packed = "
            "ros_count_scan.DeserializeLengthDelimited();\n";
      os << indent
         << "        if (!ros_packed.ok()) return ros_packed.status();\n";
      if (fixed_wire_type) {
        os << indent << "        if (ros_packed->size() % sizeof(" << type
           << ") != 0) {\n";
        os << indent
           << "          return absl::InvalidArgumentError("
              "\"packed fixed-width field has a partial element\");\n";
        os << indent << "        }\n";
        os << indent << "        ros_count += ros_packed->size() / sizeof("
           << type << ");\n";
      } else {
        os << indent
           << "        ::phaser::ProtoBuffer ros_values(*ros_packed);\n";
        os << indent << "        while (!ros_values.Eof()) {\n";
        os << indent << "          [[maybe_unused]] " << type
           << " _phaser_ignored{};\n";
        os << indent << "          {\n";
        GenerateDirectProtobufReadValue(host, os, field, "ros_values",
                                        "_phaser_ignored",
                                        indent + "            ");
        os << indent << "          }\n";
        os << indent << "          ++ros_count;\n";
        os << indent << "        }\n";
      }
      os << indent << "      } else {\n";
      os << indent << "        [[maybe_unused]] " << type
         << " _phaser_ignored{};\n";
      os << indent << "        {\n";
      GenerateDirectProtobufReadValue(host, os, field, "ros_count_scan",
                                      "_phaser_ignored", indent + "          ");
      os << indent << "        }\n";
      os << indent << "        ++ros_count;\n";
      os << indent << "      }\n";
    } else {
      os << indent << "      [[maybe_unused]] " << type
         << " _phaser_ignored{};\n";
      os << indent << "      {\n";
      GenerateDirectProtobufReadValue(host, os, field, "ros_count_scan",
                                      "_phaser_ignored", indent + "        ");
      os << indent << "      }\n";
      os << indent << "      ++ros_count;\n";
    }
    os << indent << "    } else {\n";
    os << indent
       << "      if (absl::Status status = "
          "ros_count_scan.SkipTag(*ros_tag); !status.ok()) return status;\n";
    os << indent << "    }\n";
    os << indent << "  }\n";
    os << indent
       << "  if (absl::Status status = output.WriteSequenceLength(ros_count); "
          "!status.ok()) return status;\n";
  }

  if (fixed_extent > 0) {
    os << indent << "  size_t ros_emitted = 0;\n";
  }
  os << indent << "  ::phaser::ProtoBuffer ros_scan(protobuf);\n";
  os << indent << "  while (!ros_scan.Eof()) {\n";
  os << indent
     << "    absl::StatusOr<uint32_t> ros_tag = "
        "ros_scan.DeserializeVarint<uint32_t, false>();\n";
  os << indent << "    if (!ros_tag.ok()) return ros_tag.status();\n";
  os << indent
     << "    const uint32_t ros_number = "
        "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
  os << indent << "    if (ros_number == " << field->number() << ") {\n";
  auto emit_value = [&](const std::string& buffer,
                        const std::string& emit_indent) {
    os << emit_indent << type << " ros_value{};\n";
    os << emit_indent << "{\n";
    GenerateDirectProtobufReadValue(host, os, field, buffer, "ros_value",
                                    emit_indent + "  ");
    os << emit_indent << "}\n";
    if (fixed_extent > 0) {
      os << emit_indent << "if (ros_emitted >= " << fixed_extent << ") {\n";
      os << emit_indent
         << "  return absl::InvalidArgumentError("
            "\"protobuf input exceeds fixed ROS array extent\");\n";
      os << emit_indent << "}\n";
    }
    GenerateDirectROSWriteValue(host, os, field, "ros_value", emit_indent);
    if (fixed_extent > 0) {
      os << emit_indent << "++ros_emitted;\n";
    }
  };
  if (field->is_packable()) {
    os << indent
       << "      if ((*ros_tag & 7u) == "
          "static_cast<uint32_t>(::phaser::WireType::kLengthDelimited)) {\n";
    os << indent
       << "        absl::StatusOr<absl::Span<char>> ros_packed = "
          "ros_scan.DeserializeLengthDelimited();\n";
    os << indent
       << "        if (!ros_packed.ok()) return ros_packed.status();\n";
    if (fixed_wire_type && !codegen::ROSBulkPrimitiveType(field).empty()) {
      os << indent << "        if (ros_packed->size() % sizeof(" << type
         << ") != 0) {\n";
      os << indent
         << "          return absl::InvalidArgumentError("
            "\"packed fixed-width field has a partial element\");\n";
      os << indent << "        }\n";
      if (fixed_extent > 0) {
        os << indent << "        const size_t ros_packed_count = "
           << "ros_packed->size() / sizeof(" << type << ");\n";
        os << indent << "        if (ros_packed_count > " << fixed_extent
           << " - ros_emitted) {\n";
        os << indent
           << "          return absl::InvalidArgumentError("
              "\"protobuf input exceeds fixed ROS array extent\");\n";
        os << indent << "        }\n";
      }
      os << indent
         << "        if (absl::Status status = output.WriteRaw("
            "ros_packed->data(), ros_packed->size()); !status.ok()) "
            "return status;\n";
      if (fixed_extent > 0) {
        os << indent << "        ros_emitted += ros_packed_count;\n";
      }
    } else {
      os << indent
         << "        ::phaser::ProtoBuffer ros_values(*ros_packed);\n";
      os << indent << "        while (!ros_values.Eof()) {\n";
      emit_value("ros_values", indent + "          ");
      os << indent << "        }\n";
    }
    os << indent << "      } else {\n";
    emit_value("ros_scan", indent + "        ");
    os << indent << "      }\n";
  } else {
    emit_value("ros_scan", indent + "      ");
  }
  os << indent << "    } else {\n";
  os << indent
     << "      if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
        "!status.ok()) return status;\n";
  os << indent << "    }\n";
  os << indent << "  }\n";
  if (fixed_extent > 0) {
    os << indent << "  while (ros_emitted < " << fixed_extent << ") {\n";
    os << indent << "    " << type << " ros_value{};\n";
    GenerateDirectROSWriteValue(host, os, field, "ros_value", indent + "    ");
    os << indent << "    ++ros_emitted;\n";
    os << indent << "  }\n";
  }
  os << indent << "}\n";
}

void GenerateDirectProtobufToROS(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
  os << "absl::Status " << name
     << "::ProtobufWireToROS(std::string_view protobuf, "
        "::phaser::ROSBuffer& output) {\n";
  if (host.IsRosTime(host.message()) || host.IsRosDuration(host.message())) {
    os << "  int64_t ros_seconds = 0;\n";
    os << "  int32_t ros_nanos = 0;\n";
    os << "  ::phaser::ProtoBuffer ros_scan(protobuf);\n";
    os << "  while (!ros_scan.Eof()) {\n";
    os << "    absl::StatusOr<uint32_t> ros_tag = "
          "ros_scan.DeserializeVarint<uint32_t, false>();\n";
    os << "    if (!ros_tag.ok()) return ros_tag.status();\n";
    os << "    switch (*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift) {\n";
    os << "      case 1: {\n";
    os << "        absl::StatusOr<int64_t> value = "
          "ros_scan.DeserializeVarint<int64_t, false>();\n";
    os << "        if (!value.ok()) return value.status();\n";
    os << "        ros_seconds = *value;\n";
    os << "        break;\n";
    os << "      }\n";
    os << "      case 2: {\n";
    os << "        absl::StatusOr<int32_t> value = "
          "ros_scan.DeserializeVarint<int32_t, false>();\n";
    os << "        if (!value.ok()) return value.status();\n";
    os << "        ros_nanos = *value;\n";
    os << "        break;\n";
    os << "      }\n";
    os << "      default:\n";
    os << "        if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
          "!status.ok()) return status;\n";
    os << "    }\n";
    os << "  }\n";
    const std::string wire_type = host.IsRosTime(host.message()) ? "uint32_t" : "int32_t";
    os << "  if (absl::Status status = output.Write(static_cast<" << wire_type
       << ">(ros_seconds)); !status.ok()) return status;\n";
    os << "  if (absl::Status status = output.Write(static_cast<" << wire_type
       << ">(ros_nanos)); !status.ok()) return status;\n";
  } else {
    for (const auto& item : host.fields_in_order()) {
      if (!item->IsUnion()) {
        GenerateDirectProtobufField(host, os, item->field, "  ");
        continue;
      }
      auto union_info = std::static_pointer_cast<UnionInfo>(item);
      os << "  {\n";
      os << "    uint32_t ros_discriminator = 0;\n";
      os << "    ::phaser::ProtoBuffer ros_scan(protobuf);\n";
      os << "    while (!ros_scan.Eof()) {\n";
      os << "      absl::StatusOr<uint32_t> ros_tag = "
            "ros_scan.DeserializeVarint<uint32_t, false>();\n";
      os << "      if (!ros_tag.ok()) return ros_tag.status();\n";
      os << "      const uint32_t ros_number = "
            "*ros_tag >> ::phaser::ProtoBuffer::kFieldIdShift;\n";
      os << "      switch (ros_number) {\n";
      for (const auto& member : union_info->members) {
        os << "        case " << member->field->number() << ":\n";
        os << "          ros_discriminator = ros_number;\n";
        os << "          break;\n";
      }
      os << "        default:\n";
      os << "          break;\n";
      os << "      }\n";
      os << "      if (absl::Status status = ros_scan.SkipTag(*ros_tag); "
            "!status.ok()) return status;\n";
      os << "    }\n";
      os << "    if (absl::Status status = output.Write(ros_discriminator); "
            "!status.ok()) return status;\n";
      os << "    switch (ros_discriminator) {\n";
      for (const auto& member : union_info->members) {
        os << "      case " << member->field->number() << ":\n";
        GenerateDirectProtobufSingularField(host, os, member->field, "        ");
        os << "        break;\n";
      }
      os << "      default:\n";
      os << "        break;\n";
      os << "    }\n";
      os << "  }\n";
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void GenerateDirectROSReadValue(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& reader, const std::string& value,
    const std::string& indent) {
  using Field = google::protobuf::FieldDescriptor;
  if (codegen::IsRosConstantOnlyMessageField(field)) {
    os << indent << value << " = {};\n";
    return;
  }
  if (field->type() == Field::TYPE_STRING ||
      field->type() == Field::TYPE_BYTES) {
    os << indent << "absl::StatusOr<std::string_view> ros_value = " << reader
       << ".ReadString();\n";
    os << indent << "if (!ros_value.ok()) return ros_value.status();\n";
    os << indent << value << " = *ros_value;\n";
    return;
  }
  if (field->type() == Field::TYPE_MESSAGE) {
    abort();
  }
  const std::string type = DirectProtobufValueType(host, field);
  std::string ros_type = codegen::ROSPrimitiveCppType(field);
  if (ros_type.empty()) {
    ros_type = type;
  }
  os << indent << "absl::StatusOr<" << ros_type << "> ros_value = " << reader
     << ".Read<" << ros_type << ">();\n";
  os << indent << "if (!ros_value.ok()) return ros_value.status();\n";
  os << indent << value << " = static_cast<" << type << ">(*ros_value);\n";
}

void GenerateDirectProtoWriteValue(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& value, const std::string& field_number,
    const std::string& indent, bool raw) {
  using Field = google::protobuf::FieldDescriptor;
  const std::string type = DirectProtobufValueType(host, field);
  if (field->type() == Field::TYPE_STRING ||
      field->type() == Field::TYPE_BYTES) {
    os << indent << "if (absl::Status status = output.SerializeLengthDelimited("
       << field_number << ", " << value << ".data(), " << value
       << ".size()); !status.ok()) return status;\n";
    return;
  }
  if (field->type() == Field::TYPE_MESSAGE) {
    abort();
  }
  const bool fixed = field->type() == Field::TYPE_FIXED32 ||
                     field->type() == Field::TYPE_SFIXED32 ||
                     field->type() == Field::TYPE_FLOAT ||
                     field->type() == Field::TYPE_FIXED64 ||
                     field->type() == Field::TYPE_SFIXED64 ||
                     field->type() == Field::TYPE_DOUBLE;
  const bool is_signed = field->type() == Field::TYPE_SINT32 ||
                         field->type() == Field::TYPE_SINT64;
  if (raw && fixed) {
    os << indent << "if (absl::Status status = output.SerializeRaw(&" << value
       << ", sizeof(" << value << ")); !status.ok()) return status;\n";
    return;
  }
  os << indent << "if (absl::Status status = output.";
  if (fixed) {
    os << "SerializeFixed(" << field_number << ", " << value << ")";
  } else if (raw) {
    os << "SerializeRawVarint<" << type << ", "
       << (is_signed ? "true" : "false") << ">(" << value << ")";
  } else {
    os << "SerializeVarint<" << type << ", " << (is_signed ? "true" : "false")
       << ">(" << field_number << ", " << value << ")";
  }
  os << "; !status.ok()) return status;\n";
}

void GenerateDirectROSFieldToProtobuf(MessageCodegenHost& host, 
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& indent) {
  const std::string type = DirectProtobufValueType(host, field);
  const int fixed_extent = host.GetArraySize(field);
  const bool fixed_wire_type = codegen::IsFixedWireType(field);
  const std::string count =
      fixed_extent > 0 ? std::to_string(fixed_extent) : "ros_count";

  os << indent << "{\n";
  if (field->is_repeated() && fixed_extent <= 0) {
    os << indent
       << "  absl::StatusOr<uint32_t> ros_count_value = "
          "ros.ReadSequenceLength();\n";
    os << indent
       << "  if (!ros_count_value.ok()) return ros_count_value.status();\n";
    os << indent << "  const size_t ros_count = *ros_count_value;\n";
  }

  auto emit_one = [&](const std::string& reader, const std::string& writer,
                      const std::string& emit_indent, bool raw) {
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      if (host.IsAny(field)) {
        os << emit_indent
           << "absl::StatusOr<std::string_view> ros_type_url = " << reader
           << ".ReadString();\n";
        os << emit_indent
           << "if (!ros_type_url.ok()) return ros_type_url.status();\n";
        os << emit_indent
           << "absl::StatusOr<std::string_view> ros_any_value = " << reader
           << ".ReadString();\n";
        os << emit_indent
           << "if (!ros_any_value.ok()) return ros_any_value.status();\n";
        os << emit_indent
           << "if (!ros_type_url->empty() || !ros_any_value->empty()) {\n";
        os << emit_indent
           << "  return absl::UnimplementedError(\"ROS1 conversion of a "
              "populated google.protobuf.Any is unsupported\");\n";
        os << emit_indent << "}\n";
        os << emit_indent << "if (absl::Status status = " << writer
           << ".SerializeLengthDelimitedHeader(" << field->number()
           << ", 0); !status.ok()) return status;\n";
      } else {
        const std::string nested = host.MessageName(field->message_type(), true);
        os << emit_indent << "::phaser::ROSReader ros_size_reader = " << reader
           << ";\n";
        os << emit_indent << "::phaser::ProtoWriter ros_size_output;\n";
        os << emit_indent << "if (absl::Status status = " << nested
           << "::ROSReaderToProtobuf(ros_size_reader, ros_size_output); "
              "!status.ok()) return status;\n";
        os << emit_indent << "if (absl::Status status = " << writer
           << ".SerializeLengthDelimitedHeader(" << field->number()
           << ", ros_size_output.Size()); !status.ok()) return status;\n";
        os << emit_indent << "if (absl::Status status = " << nested
           << "::ROSReaderToProtobuf(" << reader << ", " << writer
           << "); !status.ok()) return status;\n";
      }
      return;
    }

    os << emit_indent << type << " ros_field_value{};\n";
    os << emit_indent << "{\n";
    GenerateDirectROSReadValue(host, os, field, reader, "ros_field_value",
                               emit_indent + "  ");
    os << emit_indent << "}\n";
    const std::string old_output = "output";
    if (writer != old_output) {
      os << emit_indent << "{\n";
      os << emit_indent << "  auto& output = " << writer << ";\n";
      GenerateDirectProtoWriteValue(host, os, field, "ros_field_value",
                                    std::to_string(field->number()),
                                    emit_indent + "  ", raw);
      os << emit_indent << "}\n";
    } else {
      GenerateDirectProtoWriteValue(host, os, field, "ros_field_value",
                                    std::to_string(field->number()),
                                    emit_indent, raw);
    }
  };

  if (!field->is_repeated()) {
    emit_one("ros", "output", indent + "  ", false);
    os << indent << "}\n";
    return;
  }

  if (field->is_packable() && field->is_packed() && fixed_wire_type &&
      !codegen::ROSBulkPrimitiveType(field).empty()) {
    os << indent << "  const uint64_t ros_packed_byte_size = "
       << "static_cast<uint64_t>(" << count << ") * sizeof(" << type << ");\n";
    os << indent << "  if (ros_packed_byte_size > ros.Remaining()) {\n";
    os << indent
       << "    return absl::InvalidArgumentError("
          "\"truncated ROS packed fixed-width field\");\n";
    os << indent << "  }\n";
    os << indent
       << "  absl::StatusOr<absl::Span<const char>> ros_packed = "
          "ros.ReadRaw(static_cast<size_t>(ros_packed_byte_size));\n";
    os << indent << "  if (!ros_packed.ok()) return ros_packed.status();\n";
    os << indent
       << "  if (absl::Status status = output.SerializeLengthDelimited("
       << field->number()
       << ", ros_packed->data(), ros_packed->size()); !status.ok()) "
          "return status;\n";
  } else if (field->is_packable() && field->is_packed()) {
    os << indent << "  ::phaser::ROSReader ros_packed_reader = ros;\n";
    os << indent << "  ::phaser::ProtoWriter ros_packed_size;\n";
    os << indent << "  for (size_t ros_index = 0; ros_index < " << count
       << "; ++ros_index) {\n";
    emit_one("ros_packed_reader", "ros_packed_size", indent + "    ", true);
    os << indent << "  }\n";
    os << indent
       << "  if (absl::Status status = "
          "output.SerializeLengthDelimitedHeader("
       << field->number()
       << ", ros_packed_size.Size()); !status.ok()) return status;\n";
    os << indent << "  for (size_t ros_index = 0; ros_index < " << count
       << "; ++ros_index) {\n";
    emit_one("ros", "output", indent + "    ", true);
    os << indent << "  }\n";
  } else {
    os << indent << "  for (size_t ros_index = 0; ros_index < " << count
       << "; ++ros_index) {\n";
    emit_one("ros", "output", indent + "    ", false);
    os << indent << "  }\n";
  }
  os << indent << "}\n";
}

void GenerateDirectROSToProtobuf(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
  os << "absl::Status " << name
     << "::ROSReaderToProtobuf(::phaser::ROSReader& ros, "
        "::phaser::ProtoWriter& output) {\n";
  if (host.IsRosTime(host.message()) || host.IsRosDuration(host.message())) {
    const std::string seconds_type =
        host.IsRosTime(host.message()) ? "uint32_t" : "int32_t";
    os << "  absl::StatusOr<" << seconds_type << "> ros_seconds = ros.Read<"
       << seconds_type << ">();\n";
    os << "  if (!ros_seconds.ok()) return ros_seconds.status();\n";
    os << "  absl::StatusOr<" << seconds_type << "> ros_nanos = ros.Read<"
       << seconds_type << ">();\n";
    os << "  if (!ros_nanos.ok()) return ros_nanos.status();\n";
    os << "  if (absl::Status status = "
          "output.SerializeVarint<int64_t, false>(1, "
          "static_cast<int64_t>(*ros_seconds)); !status.ok()) return status;\n";
    os << "  if (absl::Status status = "
          "output.SerializeVarint<int32_t, false>(2, "
          "static_cast<int32_t>(*ros_nanos)); !status.ok()) return status;\n";
  } else {
    for (const auto& item : host.fields_in_order()) {
      if (!item->IsUnion()) {
        GenerateDirectROSFieldToProtobuf(host, os, item->field, "  ");
        continue;
      }
      auto union_info = std::static_pointer_cast<UnionInfo>(item);
      os << "  {\n";
      os << "    absl::StatusOr<uint32_t> ros_discriminator = "
            "ros.Read<uint32_t>();\n";
      os << "    if (!ros_discriminator.ok()) return "
            "ros_discriminator.status();\n";
      os << "    switch (*ros_discriminator) {\n";
      os << "      case 0:\n";
      os << "        break;\n";
      for (const auto& member : union_info->members) {
        os << "      case " << member->field->number() << ":\n";
        GenerateDirectROSFieldToProtobuf(host, os, member->field, "        ");
        os << "        break;\n";
      }
      os << "      default:\n";
      os << "        return absl::InvalidArgumentError("
            "\"Unknown ROS oneof discriminator\");\n";
      os << "    }\n";
      os << "  }\n";
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ROSToProtobuf(absl::Span<const char> ros_bytes, "
        "::phaser::ProtoBuffer& output) {\n";
  os << "  ::phaser::ROSReader ros(ros_bytes);\n";
  os << "  ::phaser::ProtoWriter writer(output);\n";
  os << "  if (absl::Status status = ROSReaderToProtobuf(ros, writer); "
        "!status.ok()) return status;\n";
  os << "  if (!ros.Eof()) return absl::InvalidArgumentError("
        "\"Trailing bytes after ROS message\");\n";
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

bool HasROSDirectVector(MessageCodegenHost& host, 
    const google::protobuf::FieldDescriptor* field) {
  return field->is_repeated() &&
         field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
         !host.IsAny(field) && host.GetArraySize(field) <= 0;
}

bool CanSerializeROSDirectly(MessageCodegenHost& host) {
  if (host.IsRosTime(host.message()) || host.IsRosDuration(host.message())) {
    return true;
  }
  for (const auto& item : host.fields_in_order()) {
    if (item->IsUnion()) {
      return false;
    }
    const auto* field = item->field;
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_GROUP ||
        host.IsAny(field)) {
      return false;
    }
    if (field->is_repeated() &&
        (host.GetArraySize(field) > 0 || codegen::IsRosConstantOnlyMessageField(field))) {
      return false;
    }
  }
  return true;
}

std::string ROSDirectValueExpression(MessageCodegenHost& host, 
    const FieldInfo& field) {
  const auto* descriptor = field.field;
  const std::string location = "_phaser_offset + " + host.FieldOffset(field);
  if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
    return "::phaser::internal::StringAt(_phaser_pb, " + location + ")";
  }
  const std::string type =
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM
          ? host.EnumName(descriptor->enum_type())
          : codegen::ProtobufPrimitiveCppType(descriptor);
  return "::phaser::internal::LoadAt<" + type + ">(_phaser_pb, " + location +
         ")";
}

void GenerateROSDirectFieldSize(MessageCodegenHost& host, std::ostream& os,
                                                  const FieldInfo& field) {
  const auto* descriptor = field.field;
  const std::string location = "_phaser_offset + " + host.FieldOffset(field);
  const bool is_message =
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
  if (!descriptor->is_repeated()) {
    if (!is_message) {
      GenerateROSFieldSize(host, os, descriptor, ROSDirectValueExpression(host, field),
                           "    ");
      return;
    }
    os << "    _phaser_serialized_size += "
          "::phaser::internal::ROSSerializedSizeAt<"
       << host.MessageName(descriptor->message_type(), true)
       << ">(_phaser_runtime, ::phaser::internal::MessageAt(_phaser_pb, "
       << location << ", " << (field.inline_message ? "true" : "false")
       << "));\n";
    return;
  }
  os << "    _phaser_serialized_size += 4;\n";
  if (is_message) {
    os << "    _phaser_serialized_size += "
          "::phaser::internal::MessageVectorROSSize<"
       << host.MessageName(descriptor->message_type(), true)
       << ">(_phaser_runtime, " << location << ");\n";
    return;
  }
  if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
    os << "    for (::phaser::BufferOffset _phaser_entry : "
          "::phaser::internal::VectorAt<::phaser::BufferOffset>(_phaser_pb, "
       << location << ")) {\n";
    os << "      _phaser_serialized_size += 4 + "
          "::phaser::internal::StringAt(_phaser_pb, _phaser_entry).size();\n";
    os << "    }\n";
    return;
  }
  const std::optional<std::string> element = ROSFixedElementSize(host, descriptor);
  assert(element.has_value());
  os << "    _phaser_serialized_size += "
        "size_t{::phaser::internal::VectorSizeAt(_phaser_pb, "
     << location << ")} * " << *element << ";\n";
}

void GenerateROSDirectFieldWrite(MessageCodegenHost& host, std::ostream& os,
                                                   const FieldInfo& field) {
  const auto* descriptor = field.field;
  const std::string location = "_phaser_offset + " + host.FieldOffset(field);
  const bool is_message =
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
  const auto write = [&](const std::string& indent, const std::string& call) {
    os << indent << "if ((_phaser_out = " << call
       << ") == nullptr) return nullptr;\n";
  };
  if (!descriptor->is_repeated()) {
    if (!is_message) {
      GenerateROSFieldWrite(host, os, descriptor, ROSDirectValueExpression(host, field),
                            "  ", true);
      return;
    }
    write("  ", "::phaser::internal::WriteROSAt<" +
                    host.MessageName(descriptor->message_type(), true) +
                    ">(_phaser_runtime, ::phaser::internal::MessageAt("
                    "_phaser_pb, " +
                    location + ", " +
                    (field.inline_message ? "true" : "false") +
                    "), _phaser_out, _phaser_end)");
    return;
  }
  write("  ",
        "::phaser::internal::WriteROSValue(_phaser_out, _phaser_end, "
        "::phaser::internal::VectorSizeAt(_phaser_pb, " +
            location + "))");
  if (is_message) {
    write("  ", "::phaser::internal::WriteMessageVectorROS<" +
                    host.MessageName(descriptor->message_type(), true) +
                    ">(_phaser_runtime, " + location +
                    ", _phaser_out, _phaser_end)");
    return;
  }
  if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
    os << "  for (::phaser::BufferOffset _phaser_entry : "
          "::phaser::internal::VectorAt<::phaser::BufferOffset>(_phaser_pb, "
       << location << ")) {\n";
    write("    ",
          "::phaser::internal::WriteROSString(_phaser_out, _phaser_end, "
          "::phaser::internal::StringAt(_phaser_pb, _phaser_entry))");
    os << "  }\n";
    return;
  }
  const bool is_enum =
      descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM;
  const std::string storage_type = is_enum
                                       ? host.EnumName(descriptor->enum_type())
                                       : codegen::ProtobufPrimitiveCppType(descriptor);
  const std::string bulk_type =
      is_enum ? storage_type : codegen::ROSBulkPrimitiveType(descriptor);
  if (!bulk_type.empty()) {
    write("  ", "::phaser::internal::WriteROSArray<" + bulk_type +
                    ">(_phaser_out, _phaser_end, "
                    "::phaser::internal::VectorAt<" +
                    bulk_type + ">(_phaser_pb, " + location + "))");
    return;
  }
  os << "  for (" << storage_type << " _phaser_value : "
     << "::phaser::internal::VectorAt<" << storage_type << ">(_phaser_pb, "
     << location << ")) {\n";
  GenerateROSFieldWrite(host, os, descriptor, "_phaser_value", "    ", true);
  os << "  }\n";
}

void GenerateROSDirectSerialization(MessageCodegenHost& host, std::ostream& os,
                                                      bool decl) {
  if (!CanSerializeROSDirectly(host)) {
    return;
  }
  const std::string name = host.MessageName(host.message());
  if (decl) {
    os << "  // ROS serialization of the message at '_phaser_offset' in the "
          "mutable\n"
          "  // buffer of '_phaser_runtime', read straight from the payload.\n";
    os << "  static size_t PhaserROSSerializedSizeAt("
          "const ::phaser::RuntimeHandle& _phaser_runtime, "
          "::phaser::BufferOffset _phaser_offset);\n";
    os << "  // Writes at '_phaser_out', in space ending at '_phaser_end', and "
          "returns\n"
          "  // the end of what it wrote, or null if that does not fit.\n";
    os << "  static char* PhaserWriteROSAt("
          "const ::phaser::RuntimeHandle& _phaser_runtime, "
          "::phaser::BufferOffset _phaser_offset, char* _phaser_out, "
          "char* _phaser_end);\n";
    return;
  }

  const bool is_time = host.IsRosTime(host.message());
  if (is_time || host.IsRosDuration(host.message())) {
    const FieldInfo* seconds = nullptr;
    const FieldInfo* nanos = nullptr;
    for (const auto& field : host.fields()) {
      if (field->field->name() == "seconds") {
        seconds = field.get();
      } else if (field->field->name() == "nanos") {
        nanos = field.get();
      }
    }
    assert(seconds != nullptr && nanos != nullptr);
    const std::string cast = is_time ? "uint32_t" : "int32_t";
    os << "size_t " << name
       << "::PhaserROSSerializedSizeAt(const ::phaser::RuntimeHandle&, "
          "::phaser::BufferOffset) {\n";
    os << "  return 8;\n";
    os << "}\n\n";
    os << "char* " << name
       << "::PhaserWriteROSAt(const ::phaser::RuntimeHandle& "
          "_phaser_runtime, ::phaser::BufferOffset _phaser_offset, "
          "char* _phaser_out, char* _phaser_end) {\n";
    os << "  const ::phaser::PayloadBuffer* _phaser_pb = "
          "_phaser_runtime->pb;\n";
    for (const FieldInfo* field : {seconds, nanos}) {
      os << "  if ((_phaser_out = ::phaser::internal::WriteROSValue("
            "_phaser_out, _phaser_end, static_cast<"
         << cast << ">(" << ROSDirectValueExpression(host, *field)
         << "))) == nullptr) return nullptr;\n";
    }
    os << "  return _phaser_out;\n";
    os << "}\n\n";
    return;
  }

  os << "size_t " << name
     << "::PhaserROSSerializedSizeAt([[maybe_unused]] const "
        "::phaser::RuntimeHandle& _phaser_runtime, [[maybe_unused]] "
        "::phaser::BufferOffset _phaser_offset) {\n";
  os << "  if constexpr (PhaserROSFixedSize() != ::phaser::kROSVariableSize) "
        "{\n";
  os << "    return PhaserROSFixedSize();\n";
  os << "  } else {\n";
  os << "    [[maybe_unused]] const ::phaser::PayloadBuffer* _phaser_pb = "
        "_phaser_runtime->pb;\n";
  os << "    size_t _phaser_serialized_size = 0;\n";
  for (const auto& item : host.fields_in_order()) {
    GenerateROSDirectFieldSize(host, os, *item);
  }
  os << "    return _phaser_serialized_size;\n";
  os << "  }\n";
  os << "}\n\n";

  os << "char* " << name
     << "::PhaserWriteROSAt([[maybe_unused]] const "
        "::phaser::RuntimeHandle& _phaser_runtime, [[maybe_unused]] "
        "::phaser::BufferOffset _phaser_offset, char* _phaser_out, "
        "[[maybe_unused]] char* _phaser_end) {\n";
  os << "  [[maybe_unused]] const ::phaser::PayloadBuffer* _phaser_pb = "
        "_phaser_runtime->pb;\n";
  for (const auto& item : host.fields_in_order()) {
    GenerateROSDirectFieldWrite(host, os, *item);
  }
  os << "  return _phaser_out;\n";
  os << "}\n\n";
}

bool CanDeserializeROSDirectly(MessageCodegenHost& host) {
  if (host.IsRosTime(host.message()) || host.IsRosDuration(host.message())) {
    return true;
  }
  for (const auto& item : host.fields_in_order()) {
    if (item->IsUnion()) {
      return false;
    }
    const auto* field = item->field;
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_GROUP ||
        host.IsAny(field)) {
      return false;
    }
    if (field->is_repeated()) {
      if (host.GetArraySize(field) > 0 || codegen::IsRosConstantOnlyMessageField(field) ||
          field->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
          field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
        return false;
      }
      continue;
    }
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
        !item->inline_message) {
      // The minimum size of a message that can hold itself would depend on
      // itself.
      absl::flat_hash_set<const google::protobuf::Descriptor*> visited;
      if (field->message_type() == host.message() ||
          codegen::ReachesThroughSingularFields(field->message_type(), host.message(),
                                       &visited)) {
        return false;
      }
    }
  }
  return true;
}

void GenerateROSDirectDeserialization(MessageCodegenHost& host, std::ostream& os,
                                                        bool decl) {
  if (!CanDeserializeROSDirectly(host)) {
    return;
  }

  // A constant field stands for a ROS message of only constants, which has
  // no bytes.
  enum class Kind {
    kScalar,
    kConstant,
    kString,
    kMessage,
    kVector,
    kMessageVector
  };
  struct Value {
    const FieldInfo* field;
    Kind kind;
    // The ROS type of a scalar or vector element and the type it is stored
    // as, or the message type of a message or message vector.
    std::string ros_type;
    std::string storage_type;
  };
  std::vector<Value> values;
  const bool is_time = host.IsRosTime(host.message());
  if (is_time || host.IsRosDuration(host.message())) {
    const FieldInfo* seconds = nullptr;
    const FieldInfo* nanos = nullptr;
    for (const auto& field : host.fields()) {
      if (field->field->name() == "seconds") {
        seconds = field.get();
      } else if (field->field->name() == "nanos") {
        nanos = field.get();
      }
    }
    assert(seconds != nullptr && nanos != nullptr);
    const std::string ros_type = is_time ? "uint32_t" : "int32_t";
    values.push_back({seconds, Kind::kScalar, ros_type, "int64_t"});
    values.push_back({nanos, Kind::kScalar, ros_type, "int32_t"});
  } else {
    for (const auto& item : host.fields_in_order()) {
      const auto* field = item->field;
      const bool repeated = field->is_repeated();
      if (codegen::IsRosConstantOnlyMessageField(field)) {
        values.push_back({item.get(), Kind::kConstant, "", ""});
        continue;
      }
      switch (field->type()) {
        case google::protobuf::FieldDescriptor::TYPE_STRING:
        case google::protobuf::FieldDescriptor::TYPE_BYTES:
          values.push_back({item.get(), Kind::kString, "", ""});
          break;
        case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
          values.push_back(
              {item.get(), repeated ? Kind::kMessageVector : Kind::kMessage,
               "", host.MessageName(field->message_type(), true)});
          break;
        case google::protobuf::FieldDescriptor::TYPE_ENUM:
          if (repeated) {
            const std::string type = host.EnumName(field->enum_type());
            values.push_back({item.get(), Kind::kVector, type, type});
          } else {
            values.push_back({item.get(), Kind::kScalar, "int32_t", "int32_t"});
          }
          break;
        default: {
          const std::string storage_type = codegen::ProtobufPrimitiveCppType(field);
          const std::string ros_type = codegen::ROSPrimitiveCppType(field);
          values.push_back({item.get(), repeated ? Kind::kVector : Kind::kScalar,
                            ros_type.empty() ? storage_type : ros_type,
                            storage_type});
          break;
        }
      }
    }
  }
  const auto is_fixed = [](const Value& value) {
    return value.kind == Kind::kScalar || value.kind == Kind::kConstant;
  };
  const bool fixed_size =
      std::any_of(values.begin(), values.end(),
                  [](const Value& value) { return value.kind == Kind::kScalar; }) &&
      std::all_of(values.begin(), values.end(), is_fixed);
  // The number of ROS bytes of values [begin, end), which are all fixed.
  const auto run_size = [&](size_t begin, size_t end) {
    size_t size = 0;
    for (size_t i = begin; i < end; ++i) {
      if (values[i].kind == Kind::kScalar) {
        size += codegen::ROSCppTypeSize(values[i].ros_type);
      }
    }
    return size;
  };
  // Stores values [begin, end), which are all fixed, from the ROS bytes at
  // '_phaser_input' into the message at '_phaser_element'.
  const auto write_run = [&](size_t begin, size_t end,
                             const std::string& indent) {
    size_t input_offset = 0;
    for (size_t i = begin; i < end; ++i) {
      if (values[i].kind == Kind::kConstant) {
        continue;
      }
      os << indent << "const " << values[i].ros_type << " _phaser_value" << i
         << " = ::phaser::ROSReader::Decode<" << values[i].ros_type
         << ">(_phaser_input + " << input_offset << ");\n";
      input_offset += codegen::ROSCppTypeSize(values[i].ros_type);
    }
    // A constant field's value is already zero.
    std::map<uint32_t, uint32_t> presence;
    for (size_t i = begin; i < end; ++i) {
      const Value& value = values[i];
      presence[value.field->id / 32] |= uint32_t{1} << (value.field->id % 32);
      if (value.kind == Kind::kConstant) {
        continue;
      }
      os << indent << "{\n";
      os << indent << "  const " << value.storage_type
         << " _phaser_stored = static_cast<" << value.storage_type
         << ">(_phaser_value" << i << ");\n";
      os << indent << "  memcpy(_phaser_element + " << host.FieldOffset(*value.field)
         << ", &_phaser_stored, sizeof(_phaser_stored));\n";
      os << indent << "}\n";
    }
    for (const auto& [word, bits] : presence) {
      os << indent << "{\n";
      os << indent
         << "  char* _phaser_mask = _phaser_element + "
            "::phaser::Field::kPresenceMaskOffset + "
         << word * sizeof(uint32_t) << ";\n";
      os << indent << "  uint32_t _phaser_bits;\n";
      os << indent
         << "  memcpy(&_phaser_bits, _phaser_mask, sizeof(_phaser_bits));\n";
      os << indent << "  _phaser_bits |= " << bits << "u;\n";
      os << indent
         << "  memcpy(_phaser_mask, &_phaser_bits, sizeof(_phaser_bits));\n";
      os << indent << "}\n";
    }
  };

  if (decl) {
    os << "  // Reads the ROS fields of the empty message at '_phaser_offset' "
          "in the\n"
          "  // mutable buffer of '_phaser_runtime' straight into the "
          "payload.\n";
    os << "  static absl::Status PhaserDeserializeFromROSAt("
          "const ::phaser::RuntimeHandle& _phaser_runtime, "
          "::phaser::BufferOffset _phaser_offset, "
          "::phaser::ROSReader& _phaser_buffer);\n";
    os << "  static constexpr size_t PhaserROSMinSize() { return 0";
    for (const Value& value : values) {
      switch (value.kind) {
        case Kind::kScalar:
          os << " + sizeof(" << value.ros_type << ")";
          break;
        case Kind::kConstant:
          break;
        case Kind::kMessage:
          os << " + ::phaser::internal::ROSParseMinSize<" << value.storage_type
             << ">()";
          break;
        case Kind::kString:
        case Kind::kVector:
        case Kind::kMessageVector:
          os << " + sizeof(uint32_t)";
          break;
      }
    }
    os << "; }\n";
    if (!fixed_size) {
      return;
    }
    os << "  // PhaserDeserializeFromROSAt() for the PhaserROSMinSize() bytes "
          "at\n"
          "  // '_phaser_input', into the empty message at '_phaser_element'.  "
          "The\n"
          "  // stores could alias the input, so they follow all the reads.\n";
    os << "  static void PhaserDeserializeFromROSUncheckedAt("
          "char* _phaser_element, const char* _phaser_input) {\n";
    write_run(0, values.size(), "    ");
    os << "  }\n";
    os << "  // PhaserWriteROSAt() of the message at '_phaser_element' "
          "into the\n"
          "  // PhaserROSFixedSize() bytes at '_phaser_output'.  The stores "
          "could alias\n"
          "  // the message, so they follow all the reads.\n";
    os << "  static void PhaserSerializeToROSUncheckedAt("
          "const char* _phaser_element, char* _phaser_output) {\n";
    os << "    static_assert(PhaserROSFixedSize() == PhaserROSMinSize());\n";
    for (size_t i = 0; i < values.size(); ++i) {
      if (values[i].kind == Kind::kConstant) {
        continue;
      }
      os << "    " << values[i].storage_type << " _phaser_value" << i << ";\n";
      os << "    memcpy(&_phaser_value" << i << ", _phaser_element + "
         << host.FieldOffset(*values[i].field) << ", sizeof(_phaser_value" << i
         << "));\n";
    }
    size_t output_offset = 0;
    for (size_t i = 0; i < values.size(); ++i) {
      if (values[i].kind == Kind::kConstant) {
        continue;
      }
      os << "    ::phaser::ROSBuffer::Encode<" << values[i].ros_type
         << ">(_phaser_output + " << output_offset << ", static_cast<"
         << values[i].ros_type << ">(_phaser_value" << i << "));\n";
      output_offset += codegen::ROSCppTypeSize(values[i].ros_type);
    }
    os << "  }\n";
    return;
  }

  const std::string name = host.MessageName(host.message());
  os << "absl::Status " << name
     << "::PhaserDeserializeFromROSAt([[maybe_unused]] const "
        "::phaser::RuntimeHandle& _phaser_runtime, [[maybe_unused]] "
        "::phaser::BufferOffset _phaser_offset, [[maybe_unused]] "
        "::phaser::ROSReader& _phaser_buffer) {\n";
  os << "  [[maybe_unused]] ::phaser::PayloadBuffer** _phaser_pb = "
        "&_phaser_runtime->pb;\n";
  if (fixed_size) {
    os << "  static_assert(PhaserROSFixedSize() == PhaserROSMinSize());\n";
    os << "  if (_phaser_buffer.Remaining() < PhaserROSMinSize()) {\n";
    os << "    return _phaser_buffer.TruncatedError(PhaserROSMinSize());\n";
    os << "  }\n";
    os << "  PhaserDeserializeFromROSUncheckedAt("
          "(*_phaser_pb)->AddressOf<char>(_phaser_offset), "
          "_phaser_buffer.ReadRawUnchecked(PhaserROSMinSize()));\n";
    os << "  return absl::OkStatus();\n";
    os << "}\n\n";
    return;
  }
  // Each value is stored as its field's setter stores it.
  for (size_t index = 0; index < values.size(); ++index) {
    const Value& value = values[index];
    const std::string location = "_phaser_offset + " + host.FieldOffset(*value.field);
    os << "  {\n";
    switch (value.kind) {
      case Kind::kScalar:
      case Kind::kConstant: {
        // Consecutive scalars are checked for and read together.
        size_t end = index + 1;
        while (end < values.size() && is_fixed(values[end])) {
          ++end;
        }
        const size_t size = run_size(index, end);
        if (size != 0) {
          os << "    if (_phaser_buffer.Remaining() < " << size << ") {\n";
          os << "      return _phaser_buffer.TruncatedError(" << size
             << ");\n";
          os << "    }\n";
          os << "    const char* _phaser_input = "
                "_phaser_buffer.ReadRawUnchecked("
             << size << ");\n";
        }
        os << "    char* _phaser_element = "
              "(*_phaser_pb)->AddressOf<char>(_phaser_offset);\n";
        write_run(index, end, "    ");
        index = end - 1;
        break;
      }
      case Kind::kString:
        os << "    if (absl::Status _phaser_status = "
              "::phaser::internal::ReadNewString(_phaser_pb, "
           << location
           << ", _phaser_buffer); !_phaser_status.ok()) return "
              "_phaser_status;\n";
        break;
      case Kind::kMessage:
        if (value.field->inline_message) {
          os << "    const ::phaser::BufferOffset _phaser_message = "
             << location << ";\n";
        } else {
          os << "    void* _phaser_address = "
                "::phaser::PayloadBuffer::Allocate(_phaser_pb, "
             << value.storage_type << "::BinarySize());\n";
          os << "    if (_phaser_address == nullptr) return "
                "::phaser::internal::AllocationFailedError();\n";
          os << "    const ::phaser::BufferOffset _phaser_message = "
                "(*_phaser_pb)->UncheckedOffset(_phaser_address);\n";
          os << "    (*_phaser_pb)->Set<::phaser::BufferOffset>(" << location
             << ", _phaser_message);\n";
        }
        // Finding the metadata can move the buffer.
        os << "    const ::phaser::BufferOffset _phaser_metadata = "
              "::phaser::Message::MetadataFor<"
           << value.storage_type << ">(_phaser_runtime);\n";
        os << "    (*_phaser_pb)->Set<::phaser::BufferOffset>(_phaser_message, "
              "_phaser_metadata);\n";
        os << "    if (absl::Status _phaser_status = "
              "::phaser::internal::DeserializeFromROSAt<"
           << value.storage_type
           << ">(_phaser_runtime, _phaser_message, _phaser_buffer); "
              "!_phaser_status.ok()) return _phaser_status;\n";
        break;
      case Kind::kVector:
      case Kind::kMessageVector:
        os << "    if (_phaser_buffer.Remaining() < sizeof(uint32_t)) {\n";
        os << "      return _phaser_buffer.TruncatedError(sizeof(uint32_t));\n";
        os << "    }\n";
        os << "    const uint32_t _phaser_count = "
              "_phaser_buffer.ReadUnchecked<uint32_t>();\n";
        if (value.kind == Kind::kMessageVector) {
          os << "    if (absl::Status _phaser_status = "
                "::phaser::internal::DeserializeMessageVectorFromROSAt<"
             << value.storage_type << ">(_phaser_runtime, " << location
             << ", _phaser_count, _phaser_buffer); !_phaser_status.ok()) "
                "return _phaser_status;\n";
          break;
        }
        os << "    if (_phaser_count > _phaser_buffer.Remaining() / sizeof("
           << value.ros_type << ")) {\n";
        os << "      return absl::InvalidArgumentError("
              "\"ROS sequence length exceeds remaining input\");\n";
        os << "    }\n";
        os << "    " << value.storage_type
           << "* _phaser_data = ::phaser::internal::ResizeVectorAt<"
           << value.storage_type << ">(_phaser_runtime, " << location
           << ", _phaser_count);\n";
        os << "    if (_phaser_count != 0 && _phaser_data == nullptr) {\n";
        os << "      return ::phaser::internal::AllocationFailedError();\n";
        os << "    }\n";
        if (value.ros_type == value.storage_type) {
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.ReadArray<"
             << value.storage_type << ">(absl::Span<" << value.storage_type
             << ">(_phaser_data, _phaser_count)); !_phaser_status.ok()) "
                "return _phaser_status;\n";
        } else {
          os << "    const char* _phaser_input = "
                "_phaser_buffer.ReadRawUnchecked(size_t{_phaser_count} * "
                "sizeof("
             << value.ros_type << "));\n";
          os << "    for (uint32_t _phaser_index = 0; _phaser_index < "
                "_phaser_count; ++_phaser_index) {\n";
          os << "      const " << value.storage_type
             << " _phaser_value = static_cast<" << value.storage_type
             << ">(::phaser::ROSReader::Decode<" << value.ros_type
             << ">(_phaser_input + size_t{_phaser_index} * sizeof("
             << value.ros_type << ")));\n";
          os << "      memcpy(_phaser_data + _phaser_index, &_phaser_value, "
                "sizeof(_phaser_value));\n";
          os << "    }\n";
        }
        break;
    }
    os << "  }\n";
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void GeneratePhaserWriter(MessageCodegenHost& host, std::ostream& os) {
  if (!codegen::HasOnlyPlainScalars(host.message())) {
    return;
  }
  os << "  // Sets the fields of an element that MessageVectorField::Add(n, f) "
        "is\n"
        "  // adding, as their setters would.\n";
  os << "  class PhaserWriter {\n";
  os << "   public:\n";
  os << "    explicit PhaserWriter(char* _phaser_binary) : "
        "_phaser_binary_(_phaser_binary) {}\n";
  for (const auto& field : host.fields_in_order()) {
    std::string value = "_phaser_value";
    if (field->field->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
      value = "static_cast<std::underlying_type_t<" + field->c_type +
              ">>(_phaser_value)";
    }
    os << "    void set_" << field->field->name() << "(" << field->c_type
       << " _phaser_value) {\n";
    os << "      ::phaser::internal::StoreScalarAt(_phaser_binary_, "
       << host.FieldOffset(*field) << ", " << host.FieldId(*field) << ", "
       << value
       << ");\n";
    os << "    }\n";
  }
  os << "\n   private:\n";
  os << "    char* _phaser_binary_;\n";
  os << "  };\n";
}

void GenerateROSSerialization(MessageCodegenHost& host, std::ostream& os, bool decl) {
  const std::string name = host.MessageName(host.message());
  if (decl) {
    GenerateROSFixedSize(host, os);
    os << "  size_t ROSSerializedSize() const;\n";
    os << R"XXX(  absl::Status SerializeToROS(::phaser::ROSBuffer& _phaser_buffer) const {
    if constexpr (::phaser::kTrustSerializedSize) {
      if (absl::Status _phaser_status =
              _phaser_buffer.EnsureSpace(ROSSerializedSize());
          !_phaser_status.ok()) {
        return _phaser_status;
      }
    }
    return PhaserSerializeToROS(::phaser::ROSMessageWriter(_phaser_buffer));
  }
  // SerializeToROS() into a buffer that, with kTrustSerializedSize, already
  // has room for the message.
  absl::Status PhaserSerializeToROS(
      ::phaser::ROSMessageWriter _phaser_buffer) const;
)XXX";
    os << "  absl::Status DeserializeFromROS("
          "::phaser::ROSReader& _phaser_buffer);\n";
    os << "  // DeserializeFromROS for a message that is already clear.\n";
    os << "  absl::Status DeserializeFromROSUncleared("
          "::phaser::ROSReader& _phaser_buffer);\n";
    os << "  absl::Status ParseFromROS(absl::Span<const char> input);\n";
    os << R"XXX(  // With kTrustSerializedSize, '_phaser_size' must be at least
  // ROSSerializedSize().
  absl::Status SerializeToROSArray(
      void* _phaser_data, size_t _phaser_size) const {
    assert(!::phaser::kTrustSerializedSize ||
           _phaser_size >= ROSSerializedSize());
    ::phaser::ROSBuffer _phaser_buffer(_phaser_data, _phaser_size);
    return PhaserSerializeToROS(::phaser::ROSMessageWriter(_phaser_buffer));
  }
  absl::Status SerializeToROSString(std::string* _phaser_output) const {
    if (_phaser_output == nullptr) {
      return absl::InvalidArgumentError("ROS output string is null");
    }
    _phaser_output->resize(ROSSerializedSize());
    ::phaser::ROSBuffer _phaser_buffer(
        _phaser_output->data(), _phaser_output->size());
    absl::Status _phaser_status =
        PhaserSerializeToROS(::phaser::ROSMessageWriter(_phaser_buffer));
    if (!_phaser_status.ok()) {
      _phaser_output->clear();
    }
    return _phaser_status;
  }
)XXX";
    os << "  static absl::Status ProtobufToROS("
          "std::string_view protobuf, ::phaser::ROSBuffer& output);\n";
    os << "  static absl::Status ProtobufWireToROS("
          "std::string_view protobuf, ::phaser::ROSBuffer& output);\n";
    os << "  static absl::Status ROSReaderToProtobuf("
          "::phaser::ROSReader& ros, ::phaser::ProtoWriter& output);\n";
    os << "  static absl::Status ROSToProtobuf("
          "absl::Span<const char> ros, ::phaser::ProtoBuffer& output);\n";
    os << R"XXX(  static bool ROSToProtobufArray(
      absl::Span<const char> ros, void* output, size_t output_size) {
    ::phaser::ProtoBuffer buffer(static_cast<char*>(output), output_size);
    return ROSToProtobuf(ros, buffer).ok();
  }
)XXX";
    os << "  static absl::Status PhaserToROS("
          "absl::Span<const char> phaser, ::phaser::ROSBuffer& output);\n";

    os << "  static absl::Status ConvertToROS("
          "absl::Span<const char> input, ::phaser::ROSBuffer& output);\n\n";
    return;
  }

  os << "size_t " << name << "::ROSSerializedSize() const {\n";
  os << "  if constexpr (PhaserROSFixedSize() != ::phaser::kROSVariableSize) {\n"
        "    return PhaserROSFixedSize();\n"
        "  }\n";
  // ROS sizing and writing read cached ROS values through the field
  // accessors, so they do not need the payload to be synced first.
  if (host.IsRosTime(host.message()) || host.IsRosDuration(host.message())) {
    os << "  return 8;\n";
  } else {
    os << "  size_t _phaser_serialized_size = 0;\n";
    for (const auto& item : host.fields_in_order()) {
      if (item->IsUnion()) {
        auto union_info = std::static_pointer_cast<UnionInfo>(item);
        os << "  _phaser_serialized_size += 4;\n";
        os << "  switch (" << union_info->member_name
           << ".Discriminator()) {\n";
        for (size_t i = 0; i < union_info->members.size(); ++i) {
          const auto& field = union_info->members[i];
          os << "    case " << field->field->number() << ":\n";
          GenerateROSFieldSize(host, 
              os, field->field,
              ROSFieldValueExpression(host, field, union_info, static_cast<int>(i)),
              "      ");
          os << "      break;\n";
        }
        os << "    default:\n";
        os << "      break;\n";
        os << "  }\n";
        continue;
      }

      const auto* descriptor = item->field;
      if (!descriptor->is_repeated()) {
        GenerateROSFieldSize(host, os, descriptor, ROSFieldValueExpression(host, item),
                             "  ");
        continue;
      }

      const int fixed_extent = host.GetArraySize(descriptor);
      if (fixed_extent <= 0) {
        os << "  _phaser_serialized_size += 4;\n";
      }
      const std::string count = fixed_extent > 0
                                    ? std::to_string(fixed_extent)
                                    : item->member_name + ".size()";
      std::string bulk_type = codegen::ROSBulkPrimitiveType(descriptor);
      if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        bulk_type = host.EnumName(descriptor->enum_type());
      }
      if (!bulk_type.empty()) {
        os << "  _phaser_serialized_size += " << count << " * sizeof("
           << bulk_type << ");\n";
      } else {
        const bool direct = HasROSDirectVector(host, descriptor);
        if (direct) {
          os << "  if (::phaser::Message::runtime != nullptr && "
                "::phaser::Message::runtime->IsMutable()) {\n";
          os << "    _phaser_serialized_size += "
                "::phaser::internal::MessageVectorROSSize<"
             << host.MessageName(descriptor->message_type(), true)
             << ">(::phaser::Message::runtime, "
                "::phaser::Message::absolute_binary_offset + "
             << host.FieldOffset(*item) << ");\n";
          os << "  } else {\n";
        }
        std::string indent = "  ";
        const std::optional<std::string> element_type =
            ROSFixedSizeMessageType(host, descriptor);
        if (element_type.has_value()) {
          os << "  if constexpr (" << *element_type
             << "::PhaserROSFixedSize() != ::phaser::kROSVariableSize) {\n";
          os << "    _phaser_serialized_size += " << count << " * "
             << *element_type << "::PhaserROSFixedSize();\n";
          os << "  } else {\n";
          indent = "    ";
        }
        os << indent
           << "for (size_t _phaser_index = 0, _phaser_count = " << count
           << "; _phaser_index < _phaser_count; ++_phaser_index) {\n";
        GenerateROSFieldSize(host, os, descriptor,
                             item->member_name + ".Get(_phaser_index)",
                             indent + "  ");
        os << indent << "}\n";
        if (element_type.has_value()) {
          os << "  }\n";
        }
        if (direct) {
          os << "  }\n";
        }
      }
    }
    os << "  return _phaser_serialized_size;\n";
  }
  os << "}\n\n";

  os << "absl::Status " << name
     << "::PhaserSerializeToROS(::phaser::ROSMessageWriter _phaser_buffer) "
        "const {\n";
  if (host.IsRosTime(host.message())) {
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<uint32_t>(seconds())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<uint32_t>(nanos())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
  } else if (host.IsRosDuration(host.message())) {
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<int32_t>(seconds())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
    os << "  if (absl::Status _phaser_status = _phaser_buffer.Write("
          "static_cast<int32_t>(nanos())); !_phaser_status.ok()) "
          "return _phaser_status;\n";
  } else {
    for (const auto& item : host.fields_in_order()) {
      if (item->IsUnion()) {
        auto union_info = std::static_pointer_cast<UnionInfo>(item);
        os << "  if (absl::Status _phaser_status = "
              "_phaser_buffer.Write(static_cast<uint32_t>("
           << union_info->member_name
           << ".Discriminator())); !_phaser_status.ok()) "
              "return _phaser_status;\n";
        os << "  switch (" << union_info->member_name
           << ".Discriminator()) {\n";
        for (size_t i = 0; i < union_info->members.size(); ++i) {
          const auto& field = union_info->members[i];
          os << "    case " << field->field->number() << ":\n";
          GenerateROSFieldWrite(host, 
              os, field->field,
              ROSFieldValueExpression(host, field, union_info, static_cast<int>(i)),
              "      ");
          os << "      break;\n";
        }
        os << "    default:\n";
        os << "      break;\n";
        os << "  }\n";
        continue;
      }

      const auto* descriptor = item->field;
      if (!descriptor->is_repeated()) {
        GenerateROSFieldWrite(host, os, descriptor, ROSFieldValueExpression(host, item),
                              "  ");
        continue;
      }

      const int fixed_extent = host.GetArraySize(descriptor);
      if (fixed_extent <= 0) {
        os << "  if (absl::Status _phaser_status = "
              "_phaser_buffer.WriteSequenceLength("
           << item->member_name
           << ".size()); !_phaser_status.ok()) return _phaser_status;\n";
      }
      const std::string count = fixed_extent > 0
                                    ? std::to_string(fixed_extent)
                                    : item->member_name + ".size()";
      std::string bulk_type = codegen::ROSBulkPrimitiveType(descriptor);
      if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        bulk_type = host.EnumName(descriptor->enum_type());
      }
      if (!bulk_type.empty()) {
        if (fixed_extent > 0) {
          os << "  if (" << item->member_name << ".data() == nullptr) {\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.WriteZeros("
             << count << " * sizeof(" << bulk_type
             << ")); !_phaser_status.ok()) return _phaser_status;\n";
          os << "  } else {\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.WriteArray<"
             << bulk_type << ">(absl::Span<const " << bulk_type << ">("
             << item->member_name << ".data(), " << count
             << ")); !_phaser_status.ok()) return _phaser_status;\n";
          os << "  }\n";
        } else {
          os << "  if (absl::Status _phaser_status = "
                "_phaser_buffer.WriteArray<"
             << bulk_type << ">(absl::Span<const " << bulk_type << ">("
             << item->member_name << ".data(), " << count
             << ")); !_phaser_status.ok()) return _phaser_status;\n";
        }
      } else {
        const bool direct = HasROSDirectVector(host, descriptor);
        if (direct) {
          os << "  if (::phaser::Message::runtime != nullptr && "
                "::phaser::Message::runtime->IsMutable()) {\n";
          os << "    if (absl::Status _phaser_status = "
                "::phaser::internal::SerializeMessageVectorToROS<"
             << host.MessageName(descriptor->message_type(), true)
             << ">(::phaser::Message::runtime, "
                "::phaser::Message::absolute_binary_offset + "
             << host.FieldOffset(*item)
             << ", _phaser_buffer); !_phaser_status.ok()) "
                "return _phaser_status;\n";
          os << "  } else {\n";
        }
        os << "  for (size_t _phaser_index = 0, _phaser_count = " << count
           << "; _phaser_index < _phaser_count; ++_phaser_index) {\n";
        GenerateROSFieldWrite(host, 
            os, descriptor, item->member_name + ".Get(_phaser_index)", "    ");
        os << "  }\n";
        if (direct) {
          os << "  }\n";
        }
      }
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::DeserializeFromROS(::phaser::ROSReader& _phaser_buffer) {\n";
  os << "  Clear();\n";
  if (CanDeserializeROSDirectly(host)) {
    // Clearing leaves the fields' handles holding nothing, so the payload
    // can be written without them.
    os << "  if (IsMutable()) {\n";
    os << "    return PhaserDeserializeFromROSAt(::phaser::Message::runtime, "
          "::phaser::Message::absolute_binary_offset, _phaser_buffer);\n";
    os << "  }\n";
  }
  os << "  return DeserializeFromROSUncleared(_phaser_buffer);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::DeserializeFromROSUncleared(::phaser::ROSReader& _phaser_buffer) "
        "{\n";
  if (host.IsRosTime(host.message())) {
    os << "  absl::StatusOr<uint32_t> _phaser_sec = "
          "_phaser_buffer.Read<uint32_t>();\n";
    os << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
    os << "  absl::StatusOr<uint32_t> _phaser_nsec = "
          "_phaser_buffer.Read<uint32_t>();\n";
    os << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
    os << "  set_seconds(static_cast<int64_t>(*_phaser_sec));\n";
    os << "  set_nanos(static_cast<int32_t>(*_phaser_nsec));\n";
  } else if (host.IsRosDuration(host.message())) {
    os << "  absl::StatusOr<int32_t> _phaser_sec = "
          "_phaser_buffer.Read<int32_t>();\n";
    os << "  if (!_phaser_sec.ok()) return _phaser_sec.status();\n";
    os << "  absl::StatusOr<int32_t> _phaser_nsec = "
          "_phaser_buffer.Read<int32_t>();\n";
    os << "  if (!_phaser_nsec.ok()) return _phaser_nsec.status();\n";
    os << "  set_seconds(static_cast<int64_t>(*_phaser_sec));\n";
    os << "  set_nanos(*_phaser_nsec);\n";
  } else {
    for (const auto& item : host.fields_in_order()) {
      if (item->IsUnion()) {
        auto union_info = std::static_pointer_cast<UnionInfo>(item);
        os << "  {\n";
        os << "    absl::StatusOr<uint32_t> _phaser_discriminator = "
              "_phaser_buffer.Read<uint32_t>();\n";
        os << "    if (!_phaser_discriminator.ok()) return "
              "_phaser_discriminator.status();\n";
        os << "    switch (*_phaser_discriminator) {\n";
        os << "      case 0:\n";
        os << "        " << union_info->member_name << ".reset();\n";
        os << "        break;\n";
        for (size_t i = 0; i < union_info->members.size(); ++i) {
          const auto& field = union_info->members[i];
          os << "      case " << field->field->number() << ":\n";
          GenerateROSFieldRead(host, os, field->field, union_info->member_name,
                               "        ", false, "", static_cast<int>(i));
          os << "        break;\n";
        }
        os << "      default:\n";
        os << "        return absl::InvalidArgumentError("
              "\"Unknown ROS oneof discriminator\");\n";
        os << "    }\n";
        os << "  }\n";
        continue;
      }

      const auto* descriptor = item->field;
      if (!descriptor->is_repeated()) {
        GenerateROSFieldRead(host, os, descriptor, item->member_name, "  ");
        continue;
      }

      const int fixed_extent = host.GetArraySize(descriptor);
      std::string bulk_type = codegen::ROSBulkPrimitiveType(descriptor);
      if (descriptor->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        bulk_type = host.EnumName(descriptor->enum_type());
      }
      if (fixed_extent <= 0) {
        os << "  {\n";
        os << "    absl::StatusOr<uint32_t> _phaser_count = "
              "_phaser_buffer.ReadSequenceLength();\n";
        os << "    if (!_phaser_count.ok()) return _phaser_count.status();\n";
        if (!bulk_type.empty()) {
          os << "    if (*_phaser_count > _phaser_buffer.Remaining() / sizeof("
             << bulk_type << ")) {\n";
          os << "      return absl::InvalidArgumentError("
                "\"ROS sequence length exceeds remaining input\");\n";
          os << "    }\n";
          os << "    " << item->member_name << ".resize(*_phaser_count);\n";
          os << "    if (absl::Status _phaser_status = "
                "_phaser_buffer.ReadArray<"
             << bulk_type << ">(absl::Span<" << bulk_type << ">("
             << item->member_name
             << ".data(), static_cast<size_t>(*_phaser_count))); "
                "!_phaser_status.ok()) return _phaser_status;\n";
        } else {
          if (HasROSDirectVector(host, descriptor) &&
              !(host.IsRosFrontend() && host.IsRosIntrinsic(descriptor))) {
            os << "    if (absl::Status _phaser_status = "
                  "::phaser::internal::DeserializeMessageVectorFromROS<"
               << host.MessageName(descriptor->message_type(), true)
               << ">(::phaser::Message::runtime, " << item->member_name
               << ", *_phaser_count, _phaser_buffer); !_phaser_status.ok()) "
                  "return _phaser_status;\n";
            os << "  }\n";
            continue;
          }
          if (descriptor->type() ==
                  google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
              !host.IsRosIntrinsic(descriptor)) {
            // A hostile count can only reserve one element per four bytes
            // left in the input.  Smaller elements still fit by growing.
            os << "    " << item->member_name
               << ".reserve(std::min<size_t>(*_phaser_count, "
                  "_phaser_buffer.Remaining() / sizeof(uint32_t)));\n";
          }
          os << "    for (uint32_t _phaser_index = 0; "
                "_phaser_index < *_phaser_count; ++_phaser_index) {\n";
          GenerateROSFieldRead(host, os, descriptor, item->member_name, "      ",
                               true);
          os << "    }\n";
        }
        os << "  }\n";
      } else if (!bulk_type.empty()) {
        os << "  if (absl::Status _phaser_status = "
              "_phaser_buffer.ReadArray<"
           << bulk_type << ">(absl::Span<" << bulk_type << ">("
           << item->member_name << ".data(), " << fixed_extent
           << ")); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "  for (size_t _phaser_index = 0; _phaser_index < "
           << fixed_extent << "; ++_phaser_index) {\n";
        GenerateROSFieldRead(host, os, descriptor, item->member_name, "    ", false,
                             "_phaser_index");
        os << "  }\n";
      }
    }
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ParseFromROS(absl::Span<const char> input) {\n";
  os << "  ::phaser::ROSReader buffer(input);\n";
  os << "  if (absl::Status status = DeserializeFromROS(buffer); "
        "!status.ok()) return status;\n";
  os << "  if (!buffer.Eof()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Trailing bytes after ROS message\");\n";
  os << "  }\n";
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  GenerateDirectProtobufToROS(host, os);
  GenerateDirectROSToProtobuf(host, os);

  os << "absl::Status " << name
     << "::ProtobufToROS(std::string_view protobuf, "
        "::phaser::ROSBuffer& output) {\n";
  os << "  output.Clear();\n";
  os << "  absl::Status status = ProtobufWireToROS(protobuf, output);\n";
  os << "  if (!status.ok()) output.Clear();\n";
  os << "  return status;\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::PhaserToROS(absl::Span<const char> phaser, "
        "::phaser::ROSBuffer& output) {\n";
  os << "  if (phaser.empty()) {\n";
  os << "    return absl::InvalidArgumentError("
        "\"Native Phaser payload is empty\");\n";
  os << "  }\n";
  os << "  " << name
     << " message = CreateReadonly(phaser.data(), phaser.size());\n";
  os << "  return message.SerializeToROS(output);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ConvertToROS(absl::Span<const char> input, "
        "::phaser::ROSBuffer& output) {\n";
  os << "  switch (::phaser::InferMessageWireFormat(input)) {\n";
  os << "    case ::phaser::MessageWireFormat::kProtobuf:\n";
  os << "      return ProtobufToROS("
        "std::string_view(input.data(), input.size()), output);\n";
  os << "    case ::phaser::MessageWireFormat::kPhaser:\n";
  os << "      return PhaserToROS(input, output);\n";
  os << "    case ::phaser::MessageWireFormat::kUnknown:\n";
  os << "      return absl::InvalidArgumentError("
        "\"Input starts with a zero byte but is not a valid Phaser "
        "payload\");\n";
  os << "  }\n";
  os << "  return absl::InvalidArgumentError(\"Unknown input format\");\n";
  os << "}\n\n";
}

}  // namespace ros_wire_codegen
}  // namespace phaser
