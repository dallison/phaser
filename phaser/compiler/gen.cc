// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/gen.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "google/protobuf/descriptor.pb.h"

namespace phaser {

// Generated messages hold the messages they use by value, so each top-level
// message must be defined after the ones its fields (and its nested messages'
// fields) use.  Otherwise the file order is kept.  Messages that use each other
// cannot both be held by value; in such a cycle the message reached first is
// defined last.
static std::vector<const google::protobuf::Descriptor*>
MessagesInDependencyOrder(const google::protobuf::FileDescriptor* file) {
  std::unordered_set<const google::protobuf::Descriptor*> visited;
  std::vector<const google::protobuf::Descriptor*> order;

  std::function<void(const google::protobuf::Descriptor*)> visit =
      [&](const google::protobuf::Descriptor* message) {
        if (!visited.insert(message).second) {
          return;
        }
        std::vector<const google::protobuf::Descriptor*> pending = {message};
        while (!pending.empty()) {
          const google::protobuf::Descriptor* d = pending.back();
          pending.pop_back();
          for (int i = 0; i < d->field_count(); i++) {
            const google::protobuf::Descriptor* used =
                d->field(i)->message_type();
            if (used == nullptr || used->file() != file) {
              continue;
            }
            while (used->containing_type() != nullptr) {
              used = used->containing_type();
            }
            if (used != message) {
              visit(used);
            }
          }
          for (int i = 0; i < d->nested_type_count(); i++) {
            pending.push_back(d->nested_type(i));
          }
        }
        order.push_back(message);
      };

  for (int i = 0; i < file->message_type_count(); i++) {
    visit(file->message_type(i));
  }
  return order;
}

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

static bool WriteOutput(
    google::protobuf::compiler::GeneratorContext* generator_context,
    const std::filesystem::path& path, const std::string& contents,
    std::string* error) {
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> output(
      generator_context->Open(path.string()));
  if (output == nullptr) {
    *error = absl::StrFormat("Failed to open %s for writing", path.string());
    return false;
  }
  WriteToZeroCopyStream(contents, output.get());
  return true;
}

static std::string GeneratedFilename(const std::filesystem::path& package_name,
                                     const std::filesystem::path& target_name,
                                     std::string filename) {
  if (filename.rfind("../", 0) == 0) {
    // Bazel represents external sources as ../<repo>/<path>. Keep only the
    // repository-relative path so it cannot escape target_name.
    const size_t repository_end = filename.find('/', 3);
    filename = filename.substr(repository_end + 1);
  } else if (filename.rfind("external/", 0) == 0) {
    // protoc can instead preserve the execroot-relative external/<repo>/ form.
    const size_t repository_end = filename.find('/', sizeof("external/") - 1);
    filename = filename.substr(repository_end + 1);
  }
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

std::string GeneratedIncludeFilename(
    const std::filesystem::path& package_name,
    const std::filesystem::path& target_name, const std::string& filename) {
  std::string result = GeneratedFilename(package_name, target_name, filename);
  if (result.rfind("external/", 0) == 0) {
    const size_t repository_end = result.find('/', sizeof("external/") - 1);
    result = result.substr(repository_end + 1);
  }
  return result;
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
    } else if (option.first == "ros_metadata") {
      generate_ros_metadata_ = option.second.empty() ||
                               option.second == "true" || option.second == "1";
    } else if (option.first == "rust") {
      generate_rust_ = option.second.empty() || option.second == "true" ||
                       option.second == "1";
    } else if (option.first == "frontend") {
      if (option.second == "protobuf" || option.second.empty()) {
        frontend_style_ = FrontendStyle::kProtobuf;
      } else if (option.second == "ros") {
        frontend_style_ = FrontendStyle::kRos;
      } else if (option.second == "c") {
        frontend_style_ = FrontendStyle::kC;
      } else {
        *error = absl::StrFormat(
            "Unknown frontend value: %s (expected protobuf, ros, or c)",
            option.second);
        return false;
      }
    }
  }
  if (generate_rust_ && frontend_style_ != FrontendStyle::kC) {
    *error = "rust=true requires frontend=c";
    return false;
  }

  const FrontendStyle effective_frontend =
      EffectiveFrontendStyle(file, frontend_style_);
  const bool effective_ros_metadata =
      generate_ros_metadata_ &&
      file->package().rfind("google.protobuf", 0) != 0;

  // Custom option schemas, service-only files, and descriptor.proto need no
  // generated declarations. Still create their declared outputs so build rules
  // can safely include them in larger transitive proto graphs.
  if ((file->message_type_count() == 0 && file->enum_type_count() == 0) ||
      file->name() == std::string("google/protobuf/descriptor.proto") ||
      file->name() == std::string("phaser/options.proto")) {
    std::string filename = GeneratedFilename(package_name_, target_name_,
                                             std::string(file->name()));
    std::filesystem::path hp(filename);
    hp.replace_extension(".phaser.h");
    std::filesystem::path cp(filename);
    cp.replace_extension(".phaser.cc");
    auto header_output =
        std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream>(
            generator_context->Open(hp.string()));
    auto source_output =
        std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream>(
            generator_context->Open(cp.string()));
    if (header_output == nullptr || source_output == nullptr) {
      *error = absl::StrFormat("Failed to create empty outputs for %s",
                               file->name());
      return false;
    }
    WriteToZeroCopyStream("// No Phaser declarations in this schema.\n",
                          header_output.get());
    WriteToZeroCopyStream("// No Phaser definitions in this schema.\n",
                          source_output.get());
    // descriptor.proto and options.proto are not declared as language outputs.
    // Every other schema, including ones with no messages, is.
    const bool skip_language_bindings =
        file->name() == std::string("google/protobuf/descriptor.proto") ||
        file->name() == std::string("phaser/options.proto");
    if (!skip_language_bindings && frontend_style_ == FrontendStyle::kC) {
      std::filesystem::path c_header(filename);
      c_header.replace_extension(".phaser.c.h");
      if (!WriteOutput(generator_context, c_header,
                       "// No Phaser C API in this schema.\n", error)) {
        return false;
      }
    }
    if (!skip_language_bindings && generate_rust_) {
      std::filesystem::path rust_path(filename);
      rust_path.replace_extension(".rs");
      if (!WriteOutput(generator_context, rust_path,
                       "// No Phaser Rust API in this schema.\n", error)) {
        return false;
      }
    }
    if (!skip_language_bindings) {
      std::filesystem::path pyp(filename);
      pyp.replace_extension(".pybind.cc");
      auto python_output =
          std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream>(
              generator_context->Open(pyp.string()));
      if (python_output == nullptr) {
        *error = absl::StrFormat("Failed to create empty Python output for %s",
                                 file->name());
        return false;
      }
      std::stringstream python_stream;
      WriteEmptyPythonRegistration(
          python_stream, PythonRegisterSymbol(std::string(file->name())));
      WriteToZeroCopyStream(python_stream.str(), python_output.get());
    }
    return true;
  }

  Generator gen(file, added_namespace_, package_name_, target_name_,
                generate_active_message_, effective_frontend,
                effective_ros_metadata);

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

  if (effective_frontend == FrontendStyle::kC) {
    std::stringstream c_header;
    std::stringstream rust_stream;
    gen.GenerateBindings(c_header, source_stream,
                         generate_rust_ ? &rust_stream : nullptr);
    std::filesystem::path c_path(filename);
    c_path.replace_extension(".phaser.c.h");
    if (!WriteOutput(generator_context, c_path, c_header.str(), error)) {
      return false;
    }
    if (generate_rust_) {
      std::filesystem::path rust_path(filename);
      rust_path.replace_extension(".rs");
      if (!WriteOutput(generator_context, rust_path, rust_stream.str(),
                       error)) {
        return false;
      }
    }
  }

  // Write to the streams that protobuf wants
  std::filesystem::path pyp(filename);
  pyp.replace_extension(".pybind.cc");
  std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> python_output(
      generator_context->Open(pyp.string()));
  if (python_output == nullptr) {
    *error = absl::StrFormat("Failed to open %s for writing", pyp.string());
    return false;
  }
  std::stringstream python_stream;
  gen.GeneratePython(python_stream);

  WriteToZeroCopyStream(header_stream.str(), header_output.get());
  WriteToZeroCopyStream(source_stream.str(), source_output.get());
  WriteToZeroCopyStream(python_stream.str(), python_output.get());
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
                     FrontendStyle frontend_style, bool generate_ros_metadata)
    : file_(file),
      added_namespace_(ns),
      package_name_(pn),
      target_name_(tn),
      generate_active_message_(generate_active_message),
      generate_ros_metadata_(generate_ros_metadata),
      frontend_style_(frontend_style) {
  for (const google::protobuf::Descriptor* message :
       MessagesInDependencyOrder(file)) {
    message_gens_.push_back(std::make_unique<MessageGenerator>(
        message, added_namespace_, std::string(file->package()),
        generate_active_message_, frontend_style_, generate_ros_metadata_));
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
    os << "#include \"phaser/runtime/active_message.h\"\n";
  }
  for (int i = 0; i < file_->dependency_count(); i++) {
    const google::protobuf::FileDescriptor* dep = file_->dependency(i);
    if (dep->message_type_count() == 0 && dep->enum_type_count() == 0) {
      continue;
    }
    if (dep->name() == std::string("google/protobuf/descriptor.proto") ||
        dep->name() == std::string("phaser/options.proto")) {
      continue;
    }
    std::string base = GeneratedIncludeFilename(package_name_, target_name_,
                                                std::string(dep->name()));
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

namespace {

bool PhaserSkipsDescriptorRegistration(
    const google::protobuf::FileDescriptor* file) {
  const std::string name(file->name());
  // These files have no generated Phaser source. descriptor.proto is already
  // in libprotobuf. options.proto is embedded by each file that imports it.
  return name == "google/protobuf/descriptor.proto" ||
         name == "phaser/options.proto";
}

void AppendSerializedFileDescriptor(
    std::ostream& os, const google::protobuf::FileDescriptor* file, int index) {
  google::protobuf::FileDescriptorProto proto;
  file->CopyTo(&proto);
  proto.clear_source_code_info();
  std::string bytes;
  proto.SerializeToString(&bytes);

  os << "static constexpr unsigned char kPhaserProtobufFileDescriptor" << index
     << "[] = {";
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i % 12 == 0) {
      os << "\n    ";
    }
    os << absl::StrFormat("0x%02x", static_cast<unsigned char>(bytes[i]));
    if (i + 1 != bytes.size()) {
      os << ",";
      if (i % 12 != 11) {
        os << " ";
      }
    }
  }
  os << "\n};\n\n";
}

void CollectOptionsDescriptors(
    const google::protobuf::FileDescriptor* file, std::set<std::string>* seen,
    std::vector<const google::protobuf::FileDescriptor*>* files) {
  if (file == nullptr || !seen->insert(std::string(file->name())).second) {
    return;
  }
  if (std::string(file->name()) == "phaser/options.proto") {
    files->push_back(file);
  }
  for (int i = 0; i < file->dependency_count(); ++i) {
    CollectOptionsDescriptors(file->dependency(i), seen, files);
  }
}

}  // namespace

void Generator::GenerateProtobufDescriptorRegistrations(std::ostream& os) {
  os << "#include \"phaser/runtime/phaser_bank_protobuf.h\"\n\n";
  std::vector<const google::protobuf::FileDescriptor*> files;
  if (!PhaserSkipsDescriptorRegistration(file_)) {
    files.push_back(file_);
  }
  std::set<std::string> seen;
  seen.insert(std::string(file_->name()));
  std::vector<const google::protobuf::FileDescriptor*> options;
  for (int i = 0; i < file_->dependency_count(); ++i) {
    CollectOptionsDescriptors(file_->dependency(i), &seen, &options);
  }
  files.insert(files.end(), options.begin(), options.end());
  int index = 0;
  for (const auto* file : files) {
    AppendSerializedFileDescriptor(os, file, index++);
  }
  if (index == 0) {
    return;
  }
  os << "static void RegisterEmbeddedProtobufDescriptors() {\n";
  for (int i = 0; i < index; ++i) {
    os << "  ::phaser::RegisterProtobufFileDescriptor(std::string_view(\n";
    os << "      reinterpret_cast<const char*>(kPhaserProtobufFileDescriptor"
       << i << "),\n";
    os << "      sizeof(kPhaserProtobufFileDescriptor" << i << ")));\n";
  }
  os << "}\n";
  // Clang drops an unreferenced static object. Messages call this from their
  // bank initializer; files with no messages use the anchor below.
  os << "__attribute__((used)) static const int "
        "kRegisterEmbeddedProtobufDescriptors =\n";
  os << "    (RegisterEmbeddedProtobufDescriptors(), 0);\n\n";
}

void Generator::GenerateSources(std::ostream& os) {
  std::filesystem::path p(GeneratedIncludeFilename(package_name_, target_name_,
                                                   std::string(file_->name())));
  p.replace_extension(".phaser.h");
  os << "#include \"" << p.string() << "\"\n";
  GenerateProtobufDescriptorRegistrations(os);

  OpenNamespace(os);

  for (auto& msg_gen : message_gens_) {
    msg_gen->GenerateSource(os);
  }

  CloseNamespace(os);
}
}  // namespace phaser
