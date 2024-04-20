#include "phaser/compiler/gen.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include <filesystem>
#include <fstream>

namespace phaser {

static void WriteToZeroCopyStream(const std::string& data, google::protobuf::io::ZeroCopyOutputStream* stream) {
  // Write to the stream that protobuf wants
  void* data_buffer;
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

bool CodeGenerator::Generate(
    const google::protobuf::FileDescriptor *file, const std::string &parameter,
    google::protobuf::compiler::GeneratorContext *generator_context,
    std::string *error) const {
  Generator gen(file, added_namespace_);

  std::filesystem::path p(file->name());
  std::ofstream header(p.replace_extension(".phaser.h"));

  // There appears to be no way to get anything other than a ZeorCopyOutputStream
  // from the GeneratorContext.  We want to use std::ofstream to write the file,
  // so we'll write to a stringstream and then copy the data to the file.
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> header_output(
      generator_context->Open(p.filename()));

  std::stringstream header_stream;
  gen.GenerateHeaders(header_stream);
  gen.GenerateSources(std::cerr);

  // Write to the stream that protobuf wants
  WriteToZeroCopyStream(header_stream.str(), header_output.get());
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

Generator::Generator(const google::protobuf::FileDescriptor *file, const std::string& ns)
    : file_(file), added_namespace_(ns) {
  for (int i = 0; i < file->message_type_count(); i++) {
    message_gens_.push_back(
        std::make_unique<MessageGenerator>(file->message_type(i)));
  }
  // Enums
  for (int i = 0; i < file->enum_type_count(); i++) {
    enum_gens_.push_back(
        std::make_unique<EnumGenerator>(file->enum_type(i)));
  }
}

void Generator::GenerateHeaders(std::ostream &os) {
  os << "#pragma once\n";
  os << "#include \"phaser/runtime/runtime.h\"\n";
  for (int i = 0; i < file_->dependency_count(); i++) {
    std::filesystem::path p(file_->dependency(i)->name());
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
  OpenNamespace(os);

  for (auto &msg_gen : message_gens_) {
    msg_gen->GenerateSource(os);
  }

  CloseNamespace(os);
}
} // namespace phaser
