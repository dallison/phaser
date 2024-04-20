#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "google/protobuf/compiler/code_generator.h"
#include "google/protobuf/compiler/plugin.h"
#include "phaser/compiler/gen.h"

ABSL_FLAG(std::string, add_namespace, "",
          "Add a namespace to the message classes");

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);

  phaser::CodeGenerator generator(absl::GetFlag(FLAGS_add_namespace));
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}