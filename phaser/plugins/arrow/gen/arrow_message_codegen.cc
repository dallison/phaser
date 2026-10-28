// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/message_codegen_host.h"

#include "absl/strings/escaping.h"
#include "google/protobuf/descriptor.pb.h"

namespace phaser {
namespace arrow_codegen {

namespace {

const char* ArrowBool(bool value) { return value ? "true" : "false"; }

}  // namespace

std::string ArrowQuoted(MessageCodegenHost& host, std::string_view text) {
  return "\"" + absl::CEscape(text) + "\"";
}

bool ArrowFieldNullable(MessageCodegenHost& host,
    const google::protobuf::FieldDescriptor* field) {
  if (field->is_repeated() || field->is_map()) {
    return false;
  }
  if (host.message()->options().map_entry() && field->name() == "key") {
    return false;
  }
  return true;
}

const char* ArrowScalarAdd(MessageCodegenHost& host,
    const google::protobuf::FieldDescriptor* field) {
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return "AddInt32";
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      return "AddUInt32";
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      return "AddInt64";
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      return "AddUInt64";
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      return "AddFloat";
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return "AddDouble";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "AddBool";
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      return "AddString";
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "AddBinary";
    default:
      return nullptr;
  }
}

const char* ArrowScalarWrite(MessageCodegenHost& host,
    const google::protobuf::FieldDescriptor* field) {
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return "Int32";
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      return "UInt32";
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      return "Int64";
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      return "UInt64";
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      return "Float";
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return "Double";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "Bool";
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      return "String";
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "Binary";
    default:
      return nullptr;
  }
}

const char* ArrowScalarRead(MessageCodegenHost& host,
    const google::protobuf::FieldDescriptor* field) {
  return ArrowScalarWrite(host, field);
}

void GenerateArrowSchemaField(MessageCodegenHost& host,
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& name, bool nullable) {
  if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
      host.IsAny(field)) {
    os << "  builder.BeginStruct(" << name << ", " << ArrowBool(nullable)
       << ");\n";
    os << "  builder.AddString(\"type_url\", true);\n";
    os << "  builder.BeginUnion(\"value\", false);\n";
    os << "  for (const std::string& _phaser_type : types.types()) {\n";
    os << "    if (absl::Status _phaser_status = "
          "::phaser::arrow::CallArrowPlugin(\n";
    os << "            ::phaser::arrow::kArrowPluginAppendSchema, _phaser_type,\n";
    os << "            ::phaser::arrow::ArrowMessageArgs{\n";
    os << "                .schema_types = &types,\n";
    os << "                .schema_builder = &builder,\n";
    os << "                .field_name = _phaser_type,\n";
    os << "                .nullable = false});\n";
    os << "        !_phaser_status.ok()) return _phaser_status;\n";
    os << "  }\n";
    os << "  builder.EndUnion();\n";
    os << "  builder.EndStruct();\n";
    return;
  }
  if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    os << "  if (absl::Status _phaser_status = "
       << host.MessageName(field->message_type(), true)
       << "::AppendArrowSchema(builder, " << name << ", " << ArrowBool(nullable)
       << ", types); !_phaser_status.ok()) return _phaser_status;\n";
    return;
  }
  os << "  builder." << ArrowScalarAdd(host, field) << "(" << name << ", "
     << ArrowBool(nullable) << ");\n";
}

void GenerateArrowWriteField(MessageCodegenHost& host, std::ostream& os,
                                               const FieldInfo& field,
                                               int index) {
  if (field.IsUnion()) {
    const auto& union_field = static_cast<const UnionInfo&>(field);
    os << "  {\n";
    os << "    ::phaser::arrow::FieldWriter _phaser_oneof = writer.Child("
       << index << ");\n";
    os << "    switch (" << union_field.member_name << ".Discriminator()) {\n";
    os << "    case 0:\n";
    for (size_t i = 0; i < union_field.members.size(); ++i) {
      const auto* arm = union_field.members[i]->field;
      const bool message =
          arm->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
      os << "      if (absl::Status _phaser_status = _phaser_oneof.Child(" << i
         << ")." << (message ? "NullStruct" : "Null")
         << "(); !_phaser_status.ok()) return _phaser_status;\n";
    }
    os << "      break;\n";
    for (size_t i = 0; i < union_field.members.size(); ++i) {
      const auto* arm = union_field.members[i]->field;
      os << "    case " << arm->number() << ":\n";
      for (size_t j = 0; j < union_field.members.size(); ++j) {
        if (j == i) {
          continue;
        }
        const auto* other = union_field.members[j]->field;
        const bool message =
            other->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
        os << "      if (absl::Status _phaser_status = _phaser_oneof.Child("
           << j << ")." << (message ? "NullStruct" : "Null")
           << "(); !_phaser_status.ok()) return _phaser_status;\n";
      }
      os << "      {\n";
      os << "        ::phaser::arrow::FieldWriter _phaser_arm = "
            "_phaser_oneof.Child("
         << i << ");\n";
      if (arm->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        const std::string type = host.MessageName(arm->message_type(), true);
        if (host.IsAny(arm)) {
          os << "        if (absl::Status _phaser_status = "
                "::phaser::arrow::WriteAny("
             << union_field.member_name << ".GetReference<" << i << ", " << type
             << ">(), _phaser_arm, types); !_phaser_status.ok()) return "
                "_phaser_status;\n";
        } else {
          os << "        if (absl::Status _phaser_status = "
             << union_field.member_name << ".GetReference<" << i << ", " << type
             << ">().WriteArrow(_phaser_arm, types); !_phaser_status.ok()) "
                "return _phaser_status;\n";
        }
      } else if (arm->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        os << "        if (absl::Status _phaser_status = _phaser_arm."
           << ArrowScalarWrite(host, arm) << "(static_cast<int32_t>("
           << union_field.member_name << ".GetValue<" << i << ", "
           << host.EnumName(arm->enum_type())
           << ">())); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "        if (absl::Status _phaser_status = _phaser_arm."
           << ArrowScalarWrite(host, arm) << "(" << union_field.member_name
           << ".GetValue<" << i << ", " << host.FieldCType(arm)
           << ">()); !_phaser_status.ok()) return _phaser_status;\n";
      }
      os << "      }\n";
      os << "      break;\n";
    }
    os << "    default:\n";
    os << "      return absl::InvalidArgumentError("
       << ArrowQuoted(host, std::string(union_field.oneof->name()) +
                      " has an unknown discriminator")
       << ");\n";
    os << "    }\n";
    os << "    if (absl::Status _phaser_status = _phaser_oneof.FinishStruct(); "
          "!_phaser_status.ok()) return _phaser_status;\n";
    os << "  }\n";
    return;
  }

  const auto* desc = field.field;
  const std::string& member = field.member_name;
  os << "  {\n";
  os << "    ::phaser::arrow::FieldWriter _phaser_child = writer.Child("
     << index << ");\n";
  if (desc->is_map() || desc->is_repeated()) {
    const bool fixed = host.UsesArrayFacade(desc);
    const bool ros_time =
        host.IsRosFrontend() && host.IsRosIntrinsic(desc) && !host.IsRosHeader(desc->message_type());
    os << "    ::phaser::arrow::FieldWriter _phaser_values = "
          "_phaser_child.Values();\n";
    if (fixed || (host.IsRosFrontend() && host.IsRosIntrinsic(desc))) {
      os << "    for (size_t _phaser_i = 0; _phaser_i < " << member
         << ".size(); ++_phaser_i) {\n";
    } else {
      os << "    for (int _phaser_i = 0; _phaser_i < static_cast<int>("
         << member << ".size()); ++_phaser_i) {\n";
    }
    if (desc->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
        ros_time) {
      os << "      const auto _phaser_item = " << member << "[_phaser_i];\n";
      os << "      if (absl::Status _phaser_status = "
            "::phaser::arrow::WriteTimestamp(_phaser_values, "
            "static_cast<int64_t>(_phaser_item.sec), "
            "static_cast<int32_t>(_phaser_item.nsec)); !_phaser_status.ok()) "
            "return _phaser_status;\n";
    } else if (desc->type() ==
               google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      os << "      const auto _phaser_item = " << member << "[_phaser_i];\n";
      os << "      if (_phaser_item.runtime == nullptr) {\n";
      os << "        if (absl::Status _phaser_status = "
            "_phaser_values.NullStruct(); !_phaser_status.ok()) return "
            "_phaser_status;\n";
      os << "      } else if (absl::Status _phaser_status = ";
      if (host.IsAny(desc)) {
        os << "::phaser::arrow::WriteAny(_phaser_item, _phaser_values, types); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "_phaser_item.WriteArrow(_phaser_values, types); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      }
    } else if (desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
      os << "      if (absl::Status _phaser_status = _phaser_values."
         << ArrowScalarWrite(host, desc) << "(static_cast<int32_t>(" << member
         << "[_phaser_i])); !_phaser_status.ok()) return _phaser_status;\n";
    } else {
      os << "      if (absl::Status _phaser_status = _phaser_values."
         << ArrowScalarWrite(host, desc) << "(" << member
         << "[_phaser_i]); !_phaser_status.ok()) return _phaser_status;\n";
    }
    os << "    }\n";
    if (fixed) {
      os << "    if (absl::Status _phaser_status = _phaser_child.FinishFixed("
         << host.GetArraySize(desc) << "); !_phaser_status.ok()) return "
            "_phaser_status;\n";
    } else {
      os << "    if (absl::Status _phaser_status = "
            "_phaser_child.FinishList(static_cast<int64_t>("
         << member << ".size())); !_phaser_status.ok()) return "
            "_phaser_status;\n";
    }
    os << "  }\n";
    return;
  }

  if (host.IsRosFrontend() && host.IsRosIntrinsic(desc)) {
    os << "    if (!" << member << ".IsPresent()) {\n";
    os << "      if (absl::Status _phaser_status = _phaser_child.NullStruct(); "
          "!_phaser_status.ok()) return _phaser_status;\n";
    os << "    } else {\n";
    os << "      const auto _phaser_value = " << member << ".Get();\n";
    if (host.IsRosHeader(desc->message_type())) {
      os << "      if (absl::Status _phaser_status = "
            "::phaser::arrow::WriteHeader(_phaser_child, _phaser_value.seq, "
            "static_cast<int64_t>(_phaser_value.stamp.sec), "
            "static_cast<int32_t>(_phaser_value.stamp.nsec), "
            "_phaser_value.frame_id); !_phaser_status.ok()) return "
            "_phaser_status;\n";
    } else {
      os << "      if (absl::Status _phaser_status = "
            "::phaser::arrow::WriteTimestamp(_phaser_child, "
            "static_cast<int64_t>(_phaser_value.sec), "
            "static_cast<int32_t>(_phaser_value.nsec)); !_phaser_status.ok()) "
            "return _phaser_status;\n";
    }
    os << "    }\n";
    os << "  }\n";
    return;
  }

  if (desc->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    os << "    if (!" << member << ".IsPresent()) {\n";
    os << "      if (absl::Status _phaser_status = _phaser_child.NullStruct(); "
          "!_phaser_status.ok()) return _phaser_status;\n";
    os << "    } else if (absl::Status _phaser_status = ";
    if (host.IsAny(desc)) {
      os << "::phaser::arrow::WriteAny(" << member
         << ".Get(), _phaser_child, types); !_phaser_status.ok()) return "
            "_phaser_status;\n";
    } else {
      os << member << ".Get().WriteArrow(_phaser_child, types); "
                      "!_phaser_status.ok()) return _phaser_status;\n";
    }
    os << "  }\n";
    return;
  }

  os << "    if (" << member << ".IsPresent()) {\n";
  os << "      if (absl::Status _phaser_status = _phaser_child."
     << ArrowScalarWrite(host, desc) << "(";
  if (desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
    os << "static_cast<int32_t>(" << member << ".Get())";
  } else {
    os << member << ".Get()";
  }
  os << "); !_phaser_status.ok()) return _phaser_status;\n";
  os << "    } else if (absl::Status _phaser_status = _phaser_child.Null(); "
        "!_phaser_status.ok()) return _phaser_status;\n";
  os << "  }\n";
}

void GenerateArrowReadField(MessageCodegenHost& host, std::ostream& os,
                                              const FieldInfo& field,
                                              int index) {
  if (field.IsUnion()) {
    const auto& union_field = static_cast<const UnionInfo&>(field);
    os << "  {\n";
    os << "    const ::phaser::arrow::FieldReader _phaser_oneof = row.Child("
       << index << ");\n";
    os << "    if (absl::Status _phaser_status = _phaser_oneof.status(); "
          "!_phaser_status.ok()) return _phaser_status;\n";
    os << "    int _phaser_present = -1;\n";
    os << "    int _phaser_count = 0;\n";
    for (size_t i = 0; i < union_field.members.size(); ++i) {
      os << "    if (!_phaser_oneof.Child(" << i
         << ").IsNull()) { ++_phaser_count; _phaser_present = " << i << "; }\n";
    }
    os << "    if (_phaser_count > 1) {\n";
    os << "      return absl::InvalidArgumentError("
       << ArrowQuoted(host, std::string(union_field.oneof->name()) +
                      " has more than one Arrow value")
       << ");\n";
    os << "    }\n";
    os << "    if (_phaser_present < 0) {\n";
    os << "      " << union_field.member_name << ".reset();\n";
    os << "    } else {\n";
    os << "      switch (_phaser_present) {\n";
    for (size_t i = 0; i < union_field.members.size(); ++i) {
      const auto* arm = union_field.members[i]->field;
      os << "      case " << i << ": {\n";
      os << "        const ::phaser::arrow::FieldReader _phaser_arm = "
            "_phaser_oneof.Child("
         << i << ");\n";
      if (arm->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        const std::string type = host.MessageName(arm->message_type(), true);
        if (host.IsAny(arm)) {
          os << "        if (absl::Status _phaser_status = "
                "::phaser::arrow::ReadAny("
             << union_field.member_name << ".Mutable<" << i << ", " << type
             << ">(), _phaser_arm); !_phaser_status.ok()) return "
                "_phaser_status;\n";
        } else {
          os << "        if (absl::Status _phaser_status = "
             << union_field.member_name << ".Mutable<" << i << ", " << type
             << ">()->ReadArrow(_phaser_arm); !_phaser_status.ok()) return "
                "_phaser_status;\n";
        }
      } else if (arm->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
        os << "        absl::StatusOr<int32_t> _phaser_value = "
              "_phaser_arm.Int32();\n";
        os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
        os << "        " << union_field.member_name << ".Set<" << i
           << ">(static_cast<" << host.EnumName(arm->enum_type())
           << ">(*_phaser_value));\n";
      } else if (arm->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
                 arm->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
        const char* read =
            arm->type() == google::protobuf::FieldDescriptor::TYPE_BYTES
                ? "Binary"
                : "String";
        os << "        absl::StatusOr<std::string_view> _phaser_value = "
              "_phaser_arm."
           << read << "();\n";
        os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
        os << "        " << union_field.member_name << ".Set<" << i
           << ">(*_phaser_value);\n";
      } else {
        os << "        absl::StatusOr<" << host.FieldCType(arm)
           << "> _phaser_value = _phaser_arm." << ArrowScalarRead(host, arm)
           << "();\n";
        os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
        os << "        " << union_field.member_name << ".Set<" << i
           << ">(*_phaser_value);\n";
      }
      os << "        break;\n";
      os << "      }\n";
    }
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
    return;
  }

  const auto* desc = field.field;
  const std::string& member = field.member_name;
  os << "  {\n";
  os << "    const ::phaser::arrow::FieldReader _phaser_child = row.Child("
     << index << ");\n";
  os << "    if (absl::Status _phaser_status = _phaser_child.status(); "
        "!_phaser_status.ok()) return _phaser_status;\n";

  if (desc->is_map() || desc->is_repeated()) {
    const bool fixed = host.UsesArrayFacade(desc);
    if (!fixed) {
      os << "    " << member << ".Clear();\n";
    }
    os << "    if (_phaser_child.IsNull()) {\n";
    if (fixed) {
      os << "      return absl::InvalidArgumentError(\"Arrow fixed-size list "
            "is null\");\n";
    }
    os << "    } else {\n";
    if (fixed) {
      os << "      if (_phaser_child.length() != " << host.GetArraySize(desc)
         << ") {\n";
      os << "        return absl::InvalidArgumentError(\"Arrow fixed-size list "
            "has the wrong length\");\n";
      os << "      }\n";
      os << "      for (size_t _phaser_i = 0; _phaser_i < " << member
         << ".size(); ++_phaser_i) {\n";
    } else if (host.IsRosFrontend() && host.IsRosIntrinsic(desc)) {
      os << "      for (size_t _phaser_i = 0; _phaser_i < "
            "static_cast<size_t>(_phaser_child.length()); ++_phaser_i) {\n";
    } else {
      os << "      for (int _phaser_i = 0; _phaser_i < "
            "static_cast<int>(_phaser_child.length()); ++_phaser_i) {\n";
    }
    os << "        const ::phaser::arrow::FieldReader _phaser_element = "
          "_phaser_child.Element(static_cast<int64_t>(_phaser_i));\n";
    os << "        if (absl::Status _phaser_status = _phaser_element.status(); "
          "!_phaser_status.ok()) return _phaser_status;\n";
    if (desc->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
        host.IsRosFrontend() && host.IsRosIntrinsic(desc) &&
        !host.IsRosHeader(desc->message_type())) {
      os << "        int64_t _phaser_sec = 0;\n";
      os << "        int32_t _phaser_nsec = 0;\n";
      os << "        if (absl::Status _phaser_status = "
            "::phaser::arrow::ReadTimestamp(_phaser_element, &_phaser_sec, "
            "&_phaser_nsec); !_phaser_status.ok()) return _phaser_status;\n";
      if (fixed) {
        os << "        " << member << ".Set(_phaser_i, ";
      } else {
        os << "        " << member << ".push_back(";
      }
      if (host.IsRosDuration(desc->message_type())) {
        os << "::ros::Duration(static_cast<int32_t>(_phaser_sec), "
              "_phaser_nsec)";
      } else {
        os << "::ros::Time(static_cast<uint32_t>(_phaser_sec), "
              "static_cast<uint32_t>(_phaser_nsec))";
      }
      os << ");\n";
    } else if (desc->type() ==
                   google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
               host.IsAny(desc)) {
      if (fixed) {
        os << "        auto _phaser_slot = " << member << "[_phaser_i];\n";
      } else {
        os << "        auto _phaser_slot = " << member << ".Add();\n";
      }
      os << "        if (!_phaser_element.IsNull()) {\n";
      os << "          if (absl::Status _phaser_status = "
            "::phaser::arrow::ReadAny(&_phaser_slot, _phaser_element); "
            "!_phaser_status.ok()) return _phaser_status;\n";
      os << "        }\n";
    } else if (desc->type() ==
               google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      if (fixed) {
        os << "        auto _phaser_slot = " << member << "[_phaser_i];\n";
      } else {
        os << "        auto _phaser_slot = " << member << ".Add();\n";
      }
      os << "        if (!_phaser_element.IsNull()) {\n";
      os << "          if (absl::Status _phaser_status = "
            "_phaser_slot.ReadArrow(_phaser_element); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "        }\n";
    } else if (desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
      os << "        absl::StatusOr<int32_t> _phaser_value = "
            "_phaser_element.Int32();\n";
      os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
      if (fixed) {
        os << "        " << member << ".Set(_phaser_i, static_cast<"
           << host.EnumName(desc->enum_type()) << ">(*_phaser_value));\n";
      } else {
        os << "        " << member << ".push_back(static_cast<"
           << host.EnumName(desc->enum_type()) << ">(*_phaser_value));\n";
      }
    } else if (desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
               desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
      const char* read =
          desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES
              ? "Binary"
              : "String";
      os << "        absl::StatusOr<std::string_view> _phaser_value = "
            "_phaser_element."
         << read << "();\n";
      os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
      if (fixed) {
        os << "        " << member << ".Set(_phaser_i, *_phaser_value);\n";
      } else {
        os << "        " << member << ".push_back(*_phaser_value);\n";
      }
    } else {
      os << "        absl::StatusOr<" << host.FieldCType(desc)
         << "> _phaser_value = _phaser_element." << ArrowScalarRead(host, desc)
         << "();\n";
      os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
      if (fixed) {
        os << "        " << member << ".Set(_phaser_i, *_phaser_value);\n";
      } else {
        os << "        " << member << ".push_back(*_phaser_value);\n";
      }
    }
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
    return;
  }

  if (host.IsRosFrontend() && host.IsRosIntrinsic(desc)) {
    os << "    if (_phaser_child.IsNull()) {\n";
    os << "      " << member << ".Clear();\n";
    os << "    } else {\n";
    if (host.IsRosHeader(desc->message_type())) {
      os << "      uint32_t _phaser_seq = 0;\n";
      os << "      if (!_phaser_child.Child(0).IsNull()) {\n";
      os << "        absl::StatusOr<uint32_t> _phaser_value = "
            "_phaser_child.Child(0).UInt32();\n";
      os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
      os << "        _phaser_seq = *_phaser_value;\n";
      os << "      }\n";
      os << "      int64_t _phaser_sec = 0;\n";
      os << "      int32_t _phaser_nsec = 0;\n";
      os << "      if (absl::Status _phaser_status = "
            "::phaser::arrow::ReadTimestamp(_phaser_child.Child(1), "
            "&_phaser_sec, &_phaser_nsec); !_phaser_status.ok()) return "
            "_phaser_status;\n";
      os << "      std::string _phaser_frame;\n";
      os << "      if (!_phaser_child.Child(2).IsNull()) {\n";
      os << "        absl::StatusOr<std::string_view> _phaser_value = "
            "_phaser_child.Child(2).String();\n";
      os << "        if (!_phaser_value.ok()) return _phaser_value.status();\n";
      os << "        _phaser_frame.assign(_phaser_value->data(), "
            "_phaser_value->size());\n";
      os << "      }\n";
      os << "      ::std_msgs::Header _phaser_header;\n";
      os << "      _phaser_header.seq = _phaser_seq;\n";
      os << "      _phaser_header.stamp = ::ros::Time("
            "static_cast<uint32_t>(_phaser_sec), "
            "static_cast<uint32_t>(_phaser_nsec));\n";
      os << "      _phaser_header.frame_id = std::move(_phaser_frame);\n";
      os << "      " << member << ".Set(_phaser_header);\n";
    } else {
      os << "      int64_t _phaser_sec = 0;\n";
      os << "      int32_t _phaser_nsec = 0;\n";
      os << "      if (absl::Status _phaser_status = "
            "::phaser::arrow::ReadTimestamp(_phaser_child, &_phaser_sec, "
            "&_phaser_nsec); !_phaser_status.ok()) return _phaser_status;\n";
      if (host.IsRosDuration(desc->message_type())) {
        os << "      " << member
           << ".Set(::ros::Duration(static_cast<int32_t>(_phaser_sec), "
              "_phaser_nsec));\n";
      } else {
        os << "      " << member
           << ".Set(::ros::Time(static_cast<uint32_t>(_phaser_sec), "
              "static_cast<uint32_t>(_phaser_nsec)));\n";
      }
    }
    os << "    }\n";
    os << "  }\n";
    return;
  }

  if (desc->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    os << "    if (_phaser_child.IsNull()) {\n";
    os << "      " << member << ".Clear();\n";
    os << "    } else if (absl::Status _phaser_status = ";
    if (host.IsAny(desc)) {
      os << "::phaser::arrow::ReadAny(" << member
         << ".Mutable(), _phaser_child); !_phaser_status.ok()) return "
            "_phaser_status;\n";
    } else {
      os << member << ".Mutable()->ReadArrow(_phaser_child); "
                      "!_phaser_status.ok()) return _phaser_status;\n";
    }
    os << "  }\n";
    return;
  }

  os << "    if (_phaser_child.IsNull()) {\n";
  os << "      " << member << ".Clear();\n";
  os << "    } else {\n";
  if (desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
    os << "      absl::StatusOr<int32_t> _phaser_value = _phaser_child.Int32();\n";
    os << "      if (!_phaser_value.ok()) return _phaser_value.status();\n";
    os << "      " << member << ".Set(static_cast<" << host.EnumName(desc->enum_type())
       << ">(*_phaser_value));\n";
  } else if (desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING ||
             desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
    const char* read =
        desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES ? "Binary"
                                                                      : "String";
    os << "      absl::StatusOr<std::string_view> _phaser_value = "
          "_phaser_child."
       << read << "();\n";
    os << "      if (!_phaser_value.ok()) return _phaser_value.status();\n";
    os << "      " << member << ".Set(*_phaser_value);\n";
  } else {
    os << "      absl::StatusOr<" << host.FieldCType(desc)
       << "> _phaser_value = _phaser_child." << ArrowScalarRead(host, desc)
       << "();\n";
    os << "      if (!_phaser_value.ok()) return _phaser_value.status();\n";
    os << "      " << member << ".Set(*_phaser_value);\n";
  }
  os << "    }\n";
  os << "  }\n";
}

void GenerateArrowCollectField(MessageCodegenHost& host, std::ostream& os,
                                                 const FieldInfo& field) {
  if (field.IsUnion()) {
    const auto& union_field = static_cast<const UnionInfo&>(field);
    bool any_message = false;
    for (const auto& arm : union_field.members) {
      if (arm->field->type() ==
          google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        any_message = true;
        break;
      }
    }
    if (!any_message) {
      return;
    }
    os << "  switch (" << union_field.member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < union_field.members.size(); ++i) {
      const auto* arm = union_field.members[i]->field;
      if (arm->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        continue;
      }
      const std::string type = host.MessageName(arm->message_type(), true);
      os << "  case " << arm->number() << ":\n";
      if (host.IsAny(arm)) {
        os << "    if (absl::Status _phaser_status = ::phaser::arrow::CollectAny("
           << union_field.member_name << ".GetReference<" << i << ", " << type
           << ">(), types); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "    if (absl::Status _phaser_status = " << union_field.member_name
           << ".GetReference<" << i << ", " << type
           << ">().CollectArrowTypes(types); !_phaser_status.ok()) return "
              "_phaser_status;\n";
      }
      os << "    break;\n";
    }
    os << "  }\n";
    return;
  }

  const auto* desc = field.field;
  if (desc->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    return;
  }
  if (host.IsRosFrontend() && host.IsRosIntrinsic(desc)) {
    return;
  }
  const std::string& member = field.member_name;
  if (desc->is_map() || desc->is_repeated()) {
    if (host.UsesArrayFacade(desc)) {
      os << "  for (size_t _phaser_i = 0; _phaser_i < " << host.GetArraySize(desc)
         << "; ++_phaser_i) {\n";
      os << "    const auto _phaser_element = " << member << "[_phaser_i];\n";
      os << "    if (_phaser_element.runtime == nullptr) continue;\n";
      if (host.IsAny(desc)) {
        os << "    if (absl::Status _phaser_status = "
              "::phaser::arrow::CollectAny(_phaser_element, types); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "    if (absl::Status _phaser_status = "
              "_phaser_element.CollectArrowTypes(types); !_phaser_status.ok()) "
              "return _phaser_status;\n";
      }
      os << "  }\n";
      return;
    }
    os << "  for (int _phaser_i = 0; _phaser_i < static_cast<int>(" << member
       << ".size()); ++_phaser_i) {\n";
    if (host.IsAny(desc)) {
      os << "    if (absl::Status _phaser_status = ::phaser::arrow::CollectAny("
         << member
         << "[_phaser_i], types); !_phaser_status.ok()) return _phaser_status;\n";
    } else {
      os << "    if (absl::Status _phaser_status = " << member
         << "[_phaser_i].CollectArrowTypes(types); !_phaser_status.ok()) "
            "return _phaser_status;\n";
    }
    os << "  }\n";
    return;
  }
  os << "  if (" << member << ".IsPresent()) {\n";
  if (host.IsAny(desc)) {
    os << "    if (absl::Status _phaser_status = ::phaser::arrow::CollectAny("
       << member << ".Get(), types); !_phaser_status.ok()) return "
          "_phaser_status;\n";
  } else {
    os << "    if (absl::Status _phaser_status = " << member
       << ".Get().CollectArrowTypes(types); !_phaser_status.ok()) return "
          "_phaser_status;\n";
  }
  os << "  }\n";
}

}  // namespace arrow_codegen
}  // namespace phaser
