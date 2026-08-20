// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/gen.h"

#include <filesystem>
#include <fstream>

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace phaser {

static FrontendStyle EffectiveFrontendStyle(
    const google::protobuf::FileDescriptor* file, FrontendStyle requested) {
  if (requested == FrontendStyle::kRos &&
      file->package().rfind("google.protobuf", 0) == 0) {
    // Well-known protobuf imports keep the protobuf-style layout so dependency
    // graphs (notably descriptor.proto) remain compilable in ROS targets.
    return FrontendStyle::kProtobuf;
  }
  return requested;
}

static bool UsesRos1Intrinsic(const google::protobuf::Descriptor* message) {
  for (int i = 0; i < message->field_count(); ++i) {
    const auto* field = message->field(i);
    if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      continue;
    }
    const auto name = field->message_type()->full_name();
    if (name == "google.protobuf.Timestamp" ||
        name == "google.protobuf.Duration" || name == "std_msgs.Header") {
      return true;
    }
  }
  for (int i = 0; i < message->nested_type_count(); ++i) {
    if (UsesRos1Intrinsic(message->nested_type(i))) {
      return true;
    }
  }
  return false;
}

static bool UsesRos1Intrinsic(const google::protobuf::FileDescriptor* file) {
  for (int i = 0; i < file->message_type_count(); ++i) {
    if (UsesRos1Intrinsic(file->message_type(i))) {
      return true;
    }
  }
  return false;
}

static void WriteToZeroCopyStream(
    const std::string& data,
    google::protobuf::io::ZeroCopyOutputStream* stream) {
  // Write to the stream that protobuf wants
  void* data_buffer;
  int size;
  size_t offset = 0;
  while (offset < data.size()) {
    if (!stream->Next(&data_buffer, &size)) {
      break;
    }
    int to_copy = std::min(size, static_cast<int>(data.size() - offset));
    std::memcpy(data_buffer, data.data() + offset,
                static_cast<size_t>(to_copy));
    offset += static_cast<size_t>(to_copy);
    stream->BackUp(size - to_copy);
  }
}

static std::string GeneratedFilename(const std::filesystem::path& package_name,
                                     const std::filesystem::path& target_name,
                                     std::string filename) {
  size_t virtual_imports = filename.find("_virtual_imports/");
  if (virtual_imports != std::string::npos) {
    // This is something like:
    // bazel-out/darwin_arm64-dbg/bin/external/com_google_protobuf/_virtual_imports/any_proto/google/protobuf/any.proto
    filename = filename.substr(virtual_imports + sizeof("_virtual_imports/"));
    // Remove the first directory.
    filename = filename.substr(filename.find('/') + 1);
  }
  return package_name / target_name / filename;
}

bool CodeGenerator::Generate(
    const google::protobuf::FileDescriptor* file, const std::string& parameter,
    google::protobuf::compiler::GeneratorContext* generator_context,
    std::string* error) const {
  // The options for the compiler are passed in the --phaser_out parameter
  // as a comma separated list of key=value pairs, followed by a colon
  // and then the output directory.
  std::vector<std::pair<std::string, std::string>> options;
  google::protobuf::compiler::ParseGeneratorParameter(parameter, &options);

  for (auto option : options) {
    if (option.first == "add_namespace") {
      added_namespace_ = option.second;
    } else if (option.first == "package_name") {
      package_name_ = option.second;
    } else if (option.first == "target_name") {
      target_name_ = option.second;
    } else if (option.first == "active_message") {
      // Bare flag or explicit truthy value enables the field.
      generate_active_message_ = option.second.empty() ||
                                 option.second == "true" ||
                                 option.second == "1";
    } else if (option.first == "frontend") {
      if (option.second == "protobuf" || option.second.empty()) {
        frontend_style_ = FrontendStyle::kProtobuf;
      } else if (option.second == "ros") {
        frontend_style_ = FrontendStyle::kRos;
      } else {
        *error = absl::StrFormat(
            "Unknown frontend value: %s (expected protobuf or ros)",
            option.second);
        return false;
      }
    }
  }

  const FrontendStyle effective_frontend =
      EffectiveFrontendStyle(file, frontend_style_);

  // Custom option schemas and other message-free protos need no C++ output.
  // descriptor.proto is imported for extensions but must not be emitted as a
  // Phaser message graph (it is huge and not a runtime payload type here).
  if (file->message_type_count() == 0 && file->enum_type_count() == 0) {
    return true;
  }
  if (file->name() == std::string("google/protobuf/descriptor.proto")) {
    return true;
  }

  Generator gen(file, added_namespace_, package_name_, target_name_,
                generate_active_message_, effective_frontend);

  std::string filename =
      GeneratedFilename(package_name_, target_name_, std::string(file->name()));

  std::filesystem::path hp(filename);
  hp.replace_extension(".phaser.h");
  std::cerr << "Generating " << hp << "\n";

  // There appears to be no way to get anything other than a
  // ZeorCopyOutputStream from the GeneratorContext.  We want to use
  // std::ofstream to write the file, so we'll write to a stringstream and then
  // copy the data to the file.
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> header_output(
      generator_context->Open(hp.string()));

  std::filesystem::create_directories(hp.parent_path());

  if (header_output == nullptr) {
    std::cerr << "Failed to open " << hp << " for writing\n";
    *error = absl::StrFormat("Failed to open %s for writing", hp.string());
    return false;
  }
  std::stringstream header_stream;
  std::string validation_error;
  gen.GenerateHeaders(header_stream, &validation_error);
  if (!validation_error.empty()) {
    *error = validation_error;
    return false;
  }

  std::filesystem::path cp(filename);
  cp.replace_extension(".phaser.cc");

  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> source_output(
      generator_context->Open(cp.string()));
  if (source_output == nullptr) {
    *error = absl::StrFormat("Failed to open %s for writing", cp.string());
    return false;
  }
  std::stringstream source_stream;
  gen.GenerateSources(source_stream);

  // Write to the streams that protobuf wants
  WriteToZeroCopyStream(header_stream.str(), header_output.get());
  WriteToZeroCopyStream(source_stream.str(), source_output.get());
  return true;
}

void Generator::OpenNamespace(std::ostream& os) {
  std::vector<std::string> parts = absl::StrSplit(file_->package(), '.');
  for (const auto& part : parts) {
    os << "namespace " << part << " {\n";
  }
  if (!added_namespace_.empty()) {
    os << "namespace " << added_namespace_ << " {\n";
  }
}

void Generator::CloseNamespace(std::ostream& os) {
  if (!added_namespace_.empty()) {
    os << "} // namespace " << added_namespace_ << "\n";
  }
  std::vector<std::string> parts = absl::StrSplit(file_->package(), '.');
  for (const auto& part : parts) {
    os << "} // namespace " << part << "\n";
  }
}

Generator::Generator(const google::protobuf::FileDescriptor* file,
                     const std::string& ns, const std::string& pn,
                     const std::string& tn, bool generate_active_message,
                     FrontendStyle frontend_style)
    : file_(file),
      added_namespace_(ns),
      package_name_(pn),
      target_name_(tn),
      generate_active_message_(generate_active_message),
      frontend_style_(frontend_style) {
  for (int i = 0; i < file->message_type_count(); i++) {
    message_gens_.push_back(std::make_unique<MessageGenerator>(
        file->message_type(i), added_namespace_, std::string(file->package()),
        generate_active_message_, frontend_style_));
  }
  // Enums
  for (int i = 0; i < file->enum_type_count(); i++) {
    enum_gens_.push_back(std::make_unique<EnumGenerator>(file->enum_type(i)));
  }
}

void Generator::GenerateHeaders(std::ostream& os, std::string* error) {
  os << "#pragma once\n";
  os << "#include \"phaser/runtime/runtime.h\"\n";
  os << "#include <new>\n";
  if (frontend_style_ == FrontendStyle::kRos && UsesRos1Intrinsic(file_)) {
    os << "#include \"phaser/runtime/ros.h\"\n";
  }
  if (generate_active_message_) {
    os << "#include <any>\n";
  }
  for (int i = 0; i < file_->dependency_count(); i++) {
    const google::protobuf::FileDescriptor* dep = file_->dependency(i);
    if (dep->message_type_count() == 0 && dep->enum_type_count() == 0) {
      continue;
    }
    if (dep->name() == std::string("google/protobuf/descriptor.proto")) {
      continue;
    }
    std::string base = GeneratedFilename(
        package_name_, target_name_, std::string(dep->name()));
    std::filesystem::path p(base);
    p.replace_extension(".phaser.h");
    os << "#include \"" << p.string() << "\"\n";
  }

  OpenNamespace(os);

  // Enums
  for (auto& enum_gen : enum_gens_) {
    enum_gen->GenerateHeader(os);
  }

  for (auto& msg_gen : message_gens_) {
    msg_gen->GenerateEnums(os);
  }

  for (auto& msg_gen : message_gens_) {
    if (absl::Status status = msg_gen->GenerateHeader(os); !status.ok()) {
      if (error != nullptr) {
        *error = std::string(status.message());
      }
      return;
    }
  }

  CloseNamespace(os);
}

void Generator::GenerateSources(std::ostream& os) {
  std::filesystem::path p(GeneratedFilename(package_name_, target_name_,
                                            std::string(file_->name())));
  p.replace_extension(".phaser.h");
  os << "#include \"" << p.string() << "\"\n";

  OpenNamespace(os);

  for (auto& msg_gen : message_gens_) {
    msg_gen->GenerateSource(os);
  }

  CloseNamespace(os);
}
}  // namespace phaser
