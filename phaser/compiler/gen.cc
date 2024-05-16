// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/gen.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include <filesystem>
#include <fstream>

namespace phaser {

static void
WriteToZeroCopyStream(const std::string &data,
                      google::protobuf::io::ZeroCopyOutputStream *stream) {
  // Write to the stream that protobuf wants
  void *data_buffer;
  int size;
  size_t offset = 0;
  while (offset < data.size()) {
    stream->Next(&data_buffer, &size);
    int to_copy = std::min(size, static_cast<int>(data.size() - offset));
    std::memcpy(data_buffer, data.data() + offset, to_copy);
    offset += to_copy;
    stream->BackUp(size - to_copy);
  }
}

static std::string GeneratedFilename(const std::filesystem::path& package_name, const std::filesystem::path& target_name, std::string filename) {
  size_t virtual_imports = filename.find("_virtual_imports/");
  if (virtual_imports != std::string::npos) {
    // This is something like:
    // bazel-out/darwin_arm64-dbg/bin/external/com_google_protobuf/_virtual_imports/any_proto/google/protobuf/any.proto
    filename = filename.substr(virtual_imports + sizeof("_virtual_imports/"));
    // Remove the first directory.
    filename = filename.substr(filename.find('/') + 1);
  }
  return  package_name / target_name / filename;
}

bool CodeGenerator::Generate(
    const google::protobuf::FileDescriptor *file, const std::string &parameter,
    google::protobuf::compiler::GeneratorContext *generator_context,
    std::string *error) const {

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
   }
  }

  Generator gen(file, added_namespace_, package_name_, target_name_);

  std::string filename = GeneratedFilename(package_name_, target_name_, file->name());

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
  gen.GenerateHeaders(header_stream);

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


void Generator::OpenNamespace(std::ostream &os) {
  std::vector<std::string> parts = absl::StrSplit(file_->package(), '.');
  for (const auto &part : parts) {
    os << "namespace " << part << " {\n";
  }
  if (!added_namespace_.empty()) {
    os << "namespace " << added_namespace_ << " {\n";
  }
}

void Generator::CloseNamespace(std::ostream &os) {
  if (!added_namespace_.empty()) {
    os << "} // namespace " << added_namespace_ << "\n";
  }
  std::vector<std::string> parts = absl::StrSplit(file_->package(), '.');
  for (const auto &part : parts) {
    os << "} // namespace " << part << "\n";
  }
}

Generator::Generator(const google::protobuf::FileDescriptor *file,
                     const std::string &ns, const std::string& pn,
                     const std::string& tn)
    : file_(file), added_namespace_(ns), package_name_(pn), target_name_(tn) {
  for (int i = 0; i < file->message_type_count(); i++) {
    message_gens_.push_back(
        std::make_unique<MessageGenerator>(file->message_type(i), added_namespace_, file->package()));
  }
  // Enums
  for (int i = 0; i < file->enum_type_count(); i++) {
    enum_gens_.push_back(std::make_unique<EnumGenerator>(file->enum_type(i)));
  }
}

void Generator::GenerateHeaders(std::ostream &os) {
  os << "#pragma once\n";
  os << "#include \"phaser/runtime/runtime.h\"\n";
  for (int i = 0; i < file_->dependency_count(); i++) {
    std::string base = GeneratedFilename(package_name_, target_name_, file_->dependency(i)->name());
    std::filesystem::path p(base);
    p.replace_extension(".phaser.h");
    os << "#include \"" << p.string() << "\"\n";
  }

  OpenNamespace(os);

  // Enums
  for (auto &enum_gen : enum_gens_) {
    enum_gen->GenerateHeader(os);
  }

  for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateEnums(os);
  }

  for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateHeader(os);
  }

  CloseNamespace(os);
}

void Generator::GenerateSources(std::ostream &os) {
  std::filesystem::path p(GeneratedFilename(package_name_, target_name_, file_->name()));
  p.replace_extension(".phaser.h");
  os << "#include \"" << p.string() << "\"\n";

  OpenNamespace(os);

  for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateSource(os);
  }

  CloseNamespace(os);
}
} // namespace phaser
