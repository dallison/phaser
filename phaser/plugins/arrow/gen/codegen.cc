// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_gen.h"

#include "absl/strings/escaping.h"
#include "google/protobuf/descriptor.pb.h"

namespace phaser {
namespace {

const char* ArrowBool(bool value) { return value ? "true" : "false"; }

}  // namespace

std::string MessageGenerator::ArrowQuoted(std::string_view text) const {
  return "\"" + absl::CEscape(text) + "\"";
}

bool MessageGenerator::ArrowFieldNullable(
    const google::protobuf::FieldDescriptor* field) const {
  if (field->is_repeated() || field->is_map()) {
    return false;
  }
  if (message_->options().map_entry() && field->name() == "key") {
    return false;
  }
  return true;
}

const char* MessageGenerator::ArrowScalarAdd(
    const google::protobuf::FieldDescriptor* field) const {
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

const char* MessageGenerator::ArrowScalarWrite(
    const google::protobuf::FieldDescriptor* field) const {
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

const char* MessageGenerator::ArrowScalarRead(
    const google::protobuf::FieldDescriptor* field) const {
  return ArrowScalarWrite(field);
}

void MessageGenerator::GenerateArrowSchemaField(
    std::ostream& os, const google::protobuf::FieldDescriptor* field,
    const std::string& name, bool nullable) {
  if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
      IsAny(field)) {
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
       << MessageName(field->message_type(), true)
       << "::AppendArrowSchema(builder, " << name << ", " << ArrowBool(nullable)
       << ", types); !_phaser_status.ok()) return _phaser_status;\n";
    return;
  }
  os << "  builder." << ArrowScalarAdd(field) << "(" << name << ", "
     << ArrowBool(nullable) << ");\n";
}

void MessageGenerator::GenerateArrowWriteField(std::ostream& os,
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
        const std::string type = MessageName(arm->message_type(), true);
        if (IsAny(arm)) {
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
           << ArrowScalarWrite(arm) << "(static_cast<int32_t>("
           << union_field.member_name << ".GetValue<" << i << ", "
           << EnumName(arm->enum_type())
           << ">())); !_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "        if (absl::Status _phaser_status = _phaser_arm."
           << ArrowScalarWrite(arm) << "(" << union_field.member_name
           << ".GetValue<" << i << ", " << FieldCType(arm)
           << ">()); !_phaser_status.ok()) return _phaser_status;\n";
      }
      os << "      }\n";
      os << "      break;\n";
    }
    os << "    default:\n";
    os << "      return absl::InvalidArgumentError("
       << ArrowQuoted(std::string(union_field.oneof->name()) +
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
    const bool fixed = UsesArrayFacade(desc);
    const bool ros_time =
        IsRosFrontend() && IsRosIntrinsic(desc) && !IsRosHeader(desc->message_type());
    os << "    ::phaser::arrow::FieldWriter _phaser_values = "
          "_phaser_child.Values();\n";
    if (fixed || (IsRosFrontend() && IsRosIntrinsic(desc))) {
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
      if (IsAny(desc)) {
        os << "::phaser::arrow::WriteAny(_phaser_item, _phaser_values, types); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      } else {
        os << "_phaser_item.WriteArrow(_phaser_values, types); "
              "!_phaser_status.ok()) return _phaser_status;\n";
      }
    } else if (desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
      os << "      if (absl::Status _phaser_status = _phaser_values."
         << ArrowScalarWrite(desc) << "(static_cast<int32_t>(" << member
         << "[_phaser_i])); !_phaser_status.ok()) return _phaser_status;\n";
    } else {
      os << "      if (absl::Status _phaser_status = _phaser_values."
         << ArrowScalarWrite(desc) << "(" << member
         << "[_phaser_i]); !_phaser_status.ok()) return _phaser_status;\n";
    }
    os << "    }\n";
    if (fixed) {
      os << "    if (absl::Status _phaser_status = _phaser_child.FinishFixed("
         << GetArraySize(desc) << "); !_phaser_status.ok()) return "
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

  if (IsRosFrontend() && IsRosIntrinsic(desc)) {
    os << "    if (!" << member << ".IsPresent()) {\n";
    os << "      if (absl::Status _phaser_status = _phaser_child.NullStruct(); "
          "!_phaser_status.ok()) return _phaser_status;\n";
    os << "    } else {\n";
    os << "      const auto _phaser_value = " << member << ".Get();\n";
    if (IsRosHeader(desc->message_type())) {
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
    if (IsAny(desc)) {
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
     << ArrowScalarWrite(desc) << "(";
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

void MessageGenerator::GenerateArrowReadField(std::ostream& os,
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
       << ArrowQuoted(std::string(union_field.oneof->name()) +
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
        const std::string type = MessageName(arm->message_type(), true);
        if (IsAny(arm)) {
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
           << ">(static_cast<" << EnumName(arm->enum_type())
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
        os << "        absl::StatusOr<" << FieldCType(arm)
           << "> _phaser_value = _phaser_arm." << ArrowScalarRead(arm)
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
    const bool fixed = UsesArrayFacade(desc);
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
      os << "      if (_phaser_child.length() != " << GetArraySize(desc)
         << ") {\n";
      os << "        return absl::InvalidArgumentError(\"Arrow fixed-size list "
            "has the wrong length\");\n";
      os << "      }\n";
      os << "      for (size_t _phaser_i = 0; _phaser_i < " << member
         << ".size(); ++_phaser_i) {\n";
    } else if (IsRosFrontend() && IsRosIntrinsic(desc)) {
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
        IsRosFrontend() && IsRosIntrinsic(desc) &&
        !IsRosHeader(desc->message_type())) {
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
      if (IsRosDuration(desc->message_type())) {
        os << "::ros::Duration(static_cast<int32_t>(_phaser_sec), "
              "_phaser_nsec)";
      } else {
        os << "::ros::Time(static_cast<uint32_t>(_phaser_sec), "
              "static_cast<uint32_t>(_phaser_nsec))";
      }
      os << ");\n";
    } else if (desc->type() ==
                   google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
               IsAny(desc)) {
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
           << EnumName(desc->enum_type()) << ">(*_phaser_value));\n";
      } else {
        os << "        " << member << ".push_back(static_cast<"
           << EnumName(desc->enum_type()) << ">(*_phaser_value));\n";
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
      os << "        absl::StatusOr<" << FieldCType(desc)
         << "> _phaser_value = _phaser_element." << ArrowScalarRead(desc)
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

  if (IsRosFrontend() && IsRosIntrinsic(desc)) {
    os << "    if (_phaser_child.IsNull()) {\n";
    os << "      " << member << ".Clear();\n";
    os << "    } else {\n";
    if (IsRosHeader(desc->message_type())) {
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
      if (IsRosDuration(desc->message_type())) {
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
    if (IsAny(desc)) {
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
    os << "      " << member << ".Set(static_cast<" << EnumName(desc->enum_type())
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
    os << "      absl::StatusOr<" << FieldCType(desc)
       << "> _phaser_value = _phaser_child." << ArrowScalarRead(desc)
       << "();\n";
    os << "      if (!_phaser_value.ok()) return _phaser_value.status();\n";
    os << "      " << member << ".Set(*_phaser_value);\n";
  }
  os << "    }\n";
  os << "  }\n";
}

void MessageGenerator::GenerateArrowCollectField(std::ostream& os,
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
      const std::string type = MessageName(arm->message_type(), true);
      os << "  case " << arm->number() << ":\n";
      if (IsAny(arm)) {
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
  if (IsRosFrontend() && IsRosIntrinsic(desc)) {
    return;
  }
  const std::string& member = field.member_name;
  if (desc->is_map() || desc->is_repeated()) {
    if (UsesArrayFacade(desc)) {
      os << "  for (size_t _phaser_i = 0; _phaser_i < " << GetArraySize(desc)
         << "; ++_phaser_i) {\n";
      os << "    const auto _phaser_element = " << member << "[_phaser_i];\n";
      os << "    if (_phaser_element.runtime == nullptr) continue;\n";
      if (IsAny(desc)) {
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
    if (IsAny(desc)) {
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
  if (IsAny(desc)) {
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

namespace codegen {

void ArrowMessageCodegen::EmitMessageHeader(MessageGenerator& generator,
                                            std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  static absl::Status AppendArrowSchema("
        "::phaser::arrow::SchemaBuilder& builder, std::string_view name, "
        "bool nullable, const ::phaser::arrow::TypeSet& types);\n";
  os << "  absl::Status CollectArrowTypes(::phaser::arrow::TypeSet* types) "
        "const;\n";
  os << "  absl::Status WriteArrow(::phaser::arrow::FieldWriter writer, "
        "const ::phaser::arrow::TypeSet& types) const;\n";
  os << "  absl::Status ReadArrow(::phaser::arrow::FieldReader row);\n";
  os << "  absl::Status ToArrow(struct ArrowSchema* schema, "
        "struct ArrowArray* array) const;\n";
  os << "  static absl::Status ToArrow(absl::Span<const " << name
     << "> messages, struct ArrowSchema* schema, struct ArrowArray* array);\n";
  os << "  static absl::Status FromArrow(const struct ArrowSchema* schema, "
        "const struct ArrowArray* array, std::vector<"
     << name << ">* out);\n";
}

void ArrowMessageCodegen::EmitMessageSource(MessageGenerator& generator,
                                            std::ostream& os) {
  MessageGenerator& g = generator;
  const std::string name = g.MessageName(g.message_);

  os << "absl::Status " << name
     << "::ToArrow(struct ArrowSchema* schema, struct ArrowArray* array) "
        "const {\n";
  os << "  const " << name << "* _phaser_self = this;\n";
  os << "  return ::phaser::arrow::MessagesToArrow<" << name
     << ">(absl::MakeConstSpan(_phaser_self, 1), schema, array);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ToArrow(absl::Span<const " << name
     << "> messages, struct ArrowSchema* schema, struct ArrowArray* array) {\n";
  os << "  return ::phaser::arrow::MessagesToArrow(messages, schema, array);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::FromArrow(const struct ArrowSchema* schema, const struct ArrowArray* "
        "array, std::vector<"
     << name << ">* out) {\n";
  os << "  return ::phaser::arrow::MessagesFromArrow(schema, array, out);\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::AppendArrowSchema(::phaser::arrow::SchemaBuilder& builder, "
        "std::string_view name, bool nullable, const ::phaser::arrow::TypeSet& "
        "types) {\n";
  os << "  (void)types;\n";
  os << "  ::phaser::arrow::SchemaBuilder::TypeGuard _phaser_guard(builder, "
        "FullName());\n";
  os << "  if (_phaser_guard.cycle()) {\n";
  os << "    return absl::UnimplementedError(std::string(\"Arrow schema cannot "
        "represent recursive message \") + std::string(FullName()));\n";
  os << "  }\n";
  os << "  builder.BeginStruct(name, nullable);\n";
  for (const auto& item : g.fields_in_order_) {
    if (item->IsUnion()) {
      const auto& union_field = static_cast<const UnionInfo&>(*item);
      os << "  builder.BeginStruct("
         << g.ArrowQuoted(std::string(union_field.oneof->name())) << ", false);\n";
      for (const auto& arm : union_field.members) {
        g.GenerateArrowSchemaField(os, arm->field,
                                   g.ArrowQuoted(std::string(arm->field->name())),
                                   true);
      }
      os << "  builder.EndStruct();\n";
      continue;
    }
    const auto* desc = item->field;
    if (desc->is_map()) {
      os << "  builder.BeginMap(" << g.ArrowQuoted(std::string(desc->name()))
         << ", false);\n";
      os << "  if (absl::Status _phaser_status = "
         << g.MessageName(desc->message_type(), true)
         << "::AppendArrowSchema(builder, \"entries\", false, types); "
            "!_phaser_status.ok()) return _phaser_status;\n";
      os << "  builder.EndMap();\n";
      continue;
    }
    if (desc->is_repeated()) {
      if (g.UsesArrayFacade(desc)) {
        os << "  builder.BeginFixedList("
           << g.ArrowQuoted(std::string(desc->name())) << ", "
           << g.GetArraySize(desc) << ");\n";
        g.GenerateArrowSchemaField(os, desc, "\"item\"", false);
        os << "  builder.EndFixedList();\n";
      } else {
        os << "  builder.BeginList(" << g.ArrowQuoted(std::string(desc->name()))
           << ", false);\n";
        g.GenerateArrowSchemaField(os, desc, "\"item\"", false);
        os << "  builder.EndList();\n";
      }
      continue;
    }
    g.GenerateArrowSchemaField(os, desc, g.ArrowQuoted(std::string(desc->name())),
                               g.ArrowFieldNullable(desc));
  }
  os << "  builder.EndStruct();\n";
  os << "  return builder.status();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::CollectArrowTypes(::phaser::arrow::TypeSet* types) const {\n";
  os << "  if (runtime != nullptr && absolute_binary_offset != 0 &&\n";
  os << "      !types->Visit(runtime->pb, absolute_binary_offset)) {\n";
  os << "    return absl::OkStatus();\n";
  os << "  }\n";
  for (const auto& item : g.fields_in_order_) {
    g.GenerateArrowCollectField(os, *item);
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::WriteArrow(::phaser::arrow::FieldWriter writer, const "
        "::phaser::arrow::TypeSet& types) const {\n";
  os << "  (void)types;\n";
  int index = 0;
  for (const auto& item : g.fields_in_order_) {
    g.GenerateArrowWriteField(os, *item, index);
    ++index;
  }
  os << "  return writer.FinishStruct();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ReadArrow(::phaser::arrow::FieldReader row) {\n";
  os << "  if (absl::Status _phaser_status = row.status(); !_phaser_status.ok()) "
        "return _phaser_status;\n";
  int read_index = 0;
  for (const auto& item : g.fields_in_order_) {
    g.GenerateArrowReadField(os, *item, read_index);
    ++read_index;
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "static absl::Status " << name
     << "AppendArrowSchema(::phaser::arrow::SchemaBuilder* builder, "
        "std::string_view field_name, bool nullable, const "
        "::phaser::arrow::TypeSet* types) {\n";
  os << "  return " << name
     << "::AppendArrowSchema(*builder, field_name, nullable, *types);\n";
  os << "}\n\n";
  os << "static absl::Status " << name
     << "CollectArrowTypesAtOffset(::phaser::RuntimeHandle runtime, "
        "::phaser::BufferOffset offset, ::phaser::arrow::TypeSet* types) {\n";
  os << "  const " << name << " message(runtime, offset);\n";
  os << "  return message.CollectArrowTypes(types);\n";
  os << "}\n\n";
  os << "static absl::Status " << name
     << "WriteArrowAtOffset(::phaser::RuntimeHandle runtime, "
        "::phaser::BufferOffset offset, ::phaser::arrow::FieldWriter* writer, "
        "const ::phaser::arrow::TypeSet* types) {\n";
  os << "  const " << name << " message(runtime, offset);\n";
  os << "  return message.WriteArrow(*writer, *types);\n";
  os << "}\n\n";
  os << "static absl::Status " << name
     << "ReadArrowAtOffset(::phaser::RuntimeHandle runtime, "
        "::phaser::BufferOffset offset, const ::phaser::arrow::FieldReader* "
        "reader) {\n";
  os << "  " << name << " message(runtime, offset);\n";
  os << "  if (!message.InstallMetadata<" << name
     << ">()) return ::phaser::internal::AllocationFailedError();\n";
  os << "  return message.ReadArrow(*reader);\n";
  os << "}\n\n";
}

void ArrowMessageCodegen::EmitBankInfoFields(MessageGenerator& generator,
                                             std::ostream& os) {
  const std::string name = generator.MessageName(generator.message_);
  os << "  .append_arrow_schema = " << name << "AppendArrowSchema,\n";
  os << "  .collect_arrow_types_at_offset = " << name
     << "CollectArrowTypesAtOffset,\n";
  os << "  .write_arrow_at_offset = " << name << "WriteArrowAtOffset,\n";
  os << "  .read_arrow_at_offset = " << name << "ReadArrowAtOffset,\n";
}

}  // namespace codegen

namespace {

void EmitArrowFileHeaderIncludes(std::ostream& os) {
  os << "#include \"phaser/runtime/columnar_forward.h\"\n";
}

void EmitArrowFileSourceIncludes(std::ostream& os) {
  os << "#include \"phaser/plugins/arrow/arrow.h\"\n";
}

void EmitArrowMessageHeader(MessageGenerator& generator, std::ostream& os) {
  codegen::ArrowMessageCodegen::EmitMessageHeader(generator, os);
}

void EmitArrowMessageSource(MessageGenerator& generator, std::ostream& os) {
  codegen::ArrowMessageCodegen::EmitMessageSource(generator, os);
}

void EmitArrowBankInfoFields(MessageGenerator& generator, std::ostream& os) {
  codegen::ArrowMessageCodegen::EmitBankInfoFields(generator, os);
}

const GeneratorPlugin kArrowGeneratorPlugin = {
    "arrow",
    20,
    EmitArrowFileHeaderIncludes,
    EmitArrowFileSourceIncludes,
    EmitArrowMessageHeader,
    EmitArrowMessageSource,
    EmitArrowBankInfoFields,
    nullptr,
    nullptr,
};

struct ArrowGeneratorPluginRegistrar {
  ArrowGeneratorPluginRegistrar() {
    RegisterGeneratorPlugin(kArrowGeneratorPlugin);
  }
};

ArrowGeneratorPluginRegistrar g_arrow_generator_plugin_registrar;

}  // namespace

}  // namespace phaser
