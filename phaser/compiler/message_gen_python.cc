// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/gen.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "absl/strings/str_split.h"

namespace phaser {

std::string PythonRegisterSymbol(const std::string& proto_filename) {
  std::string stem = proto_filename;
  constexpr std::string_view kSuffix = ".proto";
  if (stem.size() >= kSuffix.size() &&
      stem.compare(stem.size() - kSuffix.size(), kSuffix.size(), kSuffix) ==
          0) {
    stem.resize(stem.size() - kSuffix.size());
  }
  for (char& c : stem) {
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                       (c >= 'a' && c <= 'z');
    if (!alnum) {
      c = '_';
    }
  }
  return "phaser_python_register_" + stem;
}

void WriteEmptyPythonRegistration(std::ostream& os, const std::string& symbol) {
  os << "#include <pybind11/pybind11.h>\n";
  os << "namespace py = pybind11;\n\n";
  os << "namespace phaser::python {\n";
  os << "void " << symbol << "(py::module_&);\n";
  os << "}  // namespace phaser::python\n\n";
  os << "void phaser::python::" << symbol << "(py::module_&) {}\n";
}

std::string Generator::CppNamespace() const {
  std::string ns;
  const std::vector<std::string> parts =
      absl::StrSplit(file_->package(), '.');
  for (const std::string& part : parts) {
    if (part.empty()) {
      continue;
    }
    if (!ns.empty()) {
      ns += "::";
    }
    ns += part;
  }
  if (!added_namespace_.empty()) {
    if (!ns.empty()) {
      ns += "::";
    }
    ns += added_namespace_;
  }
  return ns;
}

namespace {

void GeneratePythonAnyMessage(std::ostream& os) {
  os << "  if (!py::hasattr(m, \"AnyMessage\")) {\n";
  os << "    py::class_<::phaser::AnyMessage>(m, \"AnyMessage\")\n";
  os << "        .def(\"type_url\", [](const ::phaser::AnyMessage& self) {\n";
  os << "          return std::string(self.type_url());\n";
  os << "        })\n";
  os << "        .def(\"set_type_url\",\n";
  os << "             [](::phaser::AnyMessage& self, std::string value) {\n";
  os << "               self.set_type_url(value);\n";
  os << "             })\n";
  os << "        .def(\"has_type_url\", &::phaser::AnyMessage::has_type_url)\n";
  os << "        .def(\"clear_type_url\", "
        "&::phaser::AnyMessage::clear_type_url)\n";
  os << "        .def(\"value\", [](const ::phaser::AnyMessage& self) {\n";
  os << "          std::string_view value = self.value();\n";
  os << "          const char* data = value.data() == nullptr ? \"\" : "
        "value.data();\n";
  os << "          return py::bytes(data, value.size());\n";
  os << "        })\n";
  os << "        .def(\"set_value\",\n";
  os << "             [](::phaser::AnyMessage& self, std::string value) {\n";
  os << "               self.set_value(value);\n";
  os << "             })\n";
  os << "        .def(\"has_value\", &::phaser::AnyMessage::has_value)\n";
  os << "        .def(\"clear_value\", &::phaser::AnyMessage::clear_value)\n";
  os << "        .def(\"Clear\", &::phaser::AnyMessage::Clear)\n";
  os << "        .def(\"SerializeAsString\",\n";
  os << "             [](const ::phaser::AnyMessage& self) {\n";
  os << "               std::string bytes = self.SerializeAsString();\n";
  os << "               return py::bytes(bytes.data(), bytes.size());\n";
  os << "             })\n";
  os << "        .def(\"ParseFromString\",\n";
  os << "             [](::phaser::AnyMessage& self, std::string bytes) {\n";
  os << "               return self.ParseFromString(bytes);\n";
  os << "             });\n";
  os << "  }\n";
}

}  // namespace

void Generator::GeneratePython(std::ostream& os) {
  const std::string symbol = PythonRegisterSymbol(std::string(file_->name()));
  if (frontend_style_ != FrontendStyle::kProtobuf) {
    // The ROS frontend is a struct with public fields, not getters and
    // setters. Python bindings follow the protobuf accessor API.
    WriteEmptyPythonRegistration(os, symbol);
    return;
  }

  std::filesystem::path header(GeneratedIncludeFilename(
      package_name_, target_name_, std::string(file_->name())));
  header.replace_extension(".phaser.h");

  os << "#include <pybind11/pybind11.h>\n\n";
  os << "#include <string>\n\n";
  os << "#include \"" << header.string() << "\"\n\n";
  os << "namespace py = pybind11;\n\n";
  os << "namespace phaser::python {\n";
  os << "void " << symbol << "(py::module_& m);\n";
  os << "}  // namespace phaser::python\n\n";
  os << "void phaser::python::" << symbol << "(py::module_& m) {\n";
  const std::string ns = CppNamespace();
  if (!ns.empty()) {
    os << "  using namespace " << ns << ";\n";
  }
  for (const auto& enum_gen : enum_gens_) {
    enum_gen->GeneratePython(os);
  }
  bool uses_any = false;
  for (const auto& message : message_gens_) {
    message->GeneratePythonEnums(os);
    if (message->PythonUsesAny()) {
      uses_any = true;
    }
  }
  if (uses_any) {
    GeneratePythonAnyMessage(os);
  }
  for (const auto& message : message_gens_) {
    message->GeneratePythonClasses(os);
  }
  for (const auto& message : message_gens_) {
    message->GeneratePythonMethods(os);
  }
  os << "}\n";
}

std::string MessageGenerator::PythonValueType(
    const google::protobuf::FieldDescriptor* field) {
  std::string type = FieldCType(field);
  if (type.find("::") != std::string::npos && type.compare(0, 2, "::") != 0) {
    return "::" + type;
  }
  return type;
}

void MessageGenerator::GeneratePythonEnums(std::ostream& os) {
  for (const auto& nested : nested_message_gens_) {
    nested->GeneratePythonEnums(os);
  }
  for (const auto& enum_gen : enum_gens_) {
    enum_gen->GeneratePython(os);
  }
}

bool MessageGenerator::PythonUsesAny() {
  for (int i = 0; i < message_->field_count(); ++i) {
    if (IsAny(message_->field(i))) {
      return true;
    }
  }
  for (const auto& nested : nested_message_gens_) {
    if (nested->PythonUsesAny()) {
      return true;
    }
  }
  return false;
}

void MessageGenerator::GeneratePythonClasses(std::ostream& os) {
  for (const auto& nested : nested_message_gens_) {
    nested->GeneratePythonClasses(os);
  }
  const std::string name = MessageName(message_);
  os << "  py::class_<" << name << "> cls_" << GeneratedClassName() << "(m, \""
     << GeneratedClassName() << "\");\n";
}

void MessageGenerator::GeneratePythonField(
    std::ostream& os, const std::shared_ptr<FieldInfo>& field) {
  const google::protobuf::FieldDescriptor* desc = field->field;
  if (desc->type() == google::protobuf::FieldDescriptor::TYPE_GROUP) {
    std::cerr << "Groups are not supported\n";
    exit(1);
  }
  const std::string proto(desc->name());
  const std::string accessor = FieldAccessorName(proto);
  const std::string cls = MessageName(message_);
  const bool bytes =
      desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES;
  const bool text =
      bytes || desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING;
  const bool message =
      desc->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;

  if (desc->is_repeated()) {
    if (message) {
      os << "      .def(\"" << accessor << "\", [](const " << cls
         << "& self, size_t index) {\n";
      os << "        return self." << accessor << "(index);\n";
      os << "      }, py::keep_alive<0, 1>())\n";
      os << "      .def(\"mutable_" << proto << "\", [](" << cls
         << "& self, size_t index) {\n";
      os << "        return self.mutable_" << proto << "(index);\n";
      os << "      }, py::keep_alive<0, 1>())\n";
      os << "      .def(\"add_" << proto << "\", [](" << cls << "& self) {\n";
      os << "        return self.add_" << proto << "();\n";
      os << "      }, py::keep_alive<0, 1>())\n";
    } else if (bytes) {
      os << "      .def(\"" << accessor << "\", [](const " << cls
         << "& self, size_t index) {\n";
      os << "        std::string_view value = self." << accessor
         << "(index);\n";
      os << "        const char* data = value.data() == nullptr ? \"\" : "
            "value.data();\n";
      os << "        return py::bytes(data, value.size());\n";
      os << "      })\n";
    } else if (text) {
      os << "      .def(\"" << accessor << "\", [](const " << cls
         << "& self, size_t index) {\n";
      os << "        return std::string(self." << accessor << "(index));\n";
      os << "      })\n";
    } else {
      os << "      .def(\"" << accessor << "\", [](const " << cls
         << "& self, size_t index) {\n";
      os << "        return self." << accessor << "(index);\n";
      os << "      })\n";
    }
    os << "      .def(\"" << proto << "_size\", &" << cls << "::" << proto
       << "_size)\n";
    os << "      .def(\"clear_" << proto << "\", &" << cls << "::clear_"
       << proto << ")\n";
    os << "      .def(\"reserve_" << proto << "\", &" << cls << "::reserve_"
       << proto << ")\n";
    os << "      .def(\"resize_" << proto << "\", &" << cls << "::resize_"
       << proto << ")\n";
    if (message) {
      return;
    }
    if (text) {
      os << "      .def(\"add_" << proto << "\", [](" << cls
         << "& self, std::string value) {\n";
      os << "        self.add_" << proto << "(value);\n";
      os << "      })\n";
      os << "      .def(\"set_" << proto << "\", [](" << cls
         << "& self, size_t index, std::string value) {\n";
      os << "        self.set_" << proto << "(index, value);\n";
      os << "      })\n";
    } else {
      os << "      .def(\"add_" << proto << "\", &" << cls << "::add_" << proto
         << ")\n";
      os << "      .def(\"set_" << proto << "\", &" << cls << "::set_" << proto
         << ")\n";
    }
    return;
  }

  if (message) {
    const std::string type = PythonValueType(desc);
    os << "      .def(\"" << accessor << "\", [](const " << cls
       << "& self) -> const " << type << "& {\n";
    os << "        return self." << accessor << "();\n";
    os << "      }, py::return_value_policy::reference_internal)\n";
    os << "      .def(\"mutable_" << proto << "\", [](" << cls
       << "& self) {\n";
    os << "        return self.mutable_" << proto << "();\n";
    os << "      }, py::return_value_policy::reference_internal)\n";
  } else if (bytes) {
    os << "      .def(\"" << accessor << "\", [](const " << cls
       << "& self) {\n";
    os << "        std::string_view value = self." << accessor << "();\n";
    os << "        const char* data = value.data() == nullptr ? \"\" : "
          "value.data();\n";
    os << "        return py::bytes(data, value.size());\n";
    os << "      })\n";
    os << "      .def(\"set_" << proto << "\", [](" << cls
       << "& self, std::string value) {\n";
    os << "        self.set_" << proto << "(value);\n";
    os << "      })\n";
  } else if (text) {
    os << "      .def(\"" << accessor << "\", [](const " << cls
       << "& self) {\n";
    os << "        return std::string(self." << accessor << "());\n";
    os << "      })\n";
    os << "      .def(\"set_" << proto << "\", [](" << cls
       << "& self, std::string value) {\n";
    os << "        self.set_" << proto << "(value);\n";
    os << "      })\n";
  } else {
    os << "      .def(\"" << accessor << "\", &" << cls << "::" << accessor
       << ")\n";
    os << "      .def(\"set_" << proto << "\", &" << cls << "::set_" << proto
       << ")\n";
  }
  const std::string presence = PresenceAccessorName(proto);
  os << "      .def(\"" << presence << "\", &" << cls << "::" << presence
     << ")\n";
  os << "      .def(\"clear_" << proto << "\", &" << cls << "::clear_" << proto
     << ")\n";
}

void MessageGenerator::GeneratePythonMethods(std::ostream& os) {
  for (const auto& nested : nested_message_gens_) {
    nested->GeneratePythonMethods(os);
  }
  const std::string cls = MessageName(message_);
  os << "  cls_" << GeneratedClassName() << "\n";
  os << "      .def(py::init<size_t>(), py::arg(\"initial_size\") = "
        "static_cast<size_t>(8192))\n";
  for (const auto& field : fields_) {
    GeneratePythonField(os, field);
  }
  for (const auto& [oneof, info] : unions_) {
    os << "      .def(\"" << oneof->name() << "_case\", &" << cls
       << "::" << oneof->name() << "_case)\n";
    for (const auto& member : info->members) {
      GeneratePythonField(os, member);
    }
  }
  os << "      .def(\"Clear\", &" << cls << "::Clear)\n";
  os << "      .def(\"DebugString\", &" << cls << "::DebugString)\n";
  os << "      .def(\"ByteSizeLong\", &" << cls << "::ByteSizeLong)\n";
  os << "      .def(\"SerializeAsString\", [](const " << cls << "& self) {\n";
  os << "        std::string bytes = self.SerializeAsString();\n";
  os << "        return py::bytes(bytes.data(), bytes.size());\n";
  os << "      })\n";
  os << "      .def(\"ParseFromString\", [](" << cls
     << "& self, std::string bytes) {\n";
  os << "        return self.ParseFromString(bytes);\n";
  os << "      });\n";
}

}  // namespace phaser
