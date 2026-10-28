// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/generator_plugin.h"
#include "phaser/compiler/message_codegen_host.h"
#include "arrow_message_codegen.h"

namespace phaser {
namespace {

void EmitArrowMessageHeader(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
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

void EmitArrowMessageSource(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());

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
  for (const auto& item : host.fields_in_order()) {
    if (item->IsUnion()) {
      const auto& union_field = static_cast<const UnionInfo&>(*item);
      os << "  builder.BeginStruct("
         << arrow_codegen::ArrowQuoted(host, std::string(union_field.oneof->name())) << ", false);\n";
      for (const auto& arm : union_field.members) {
        arrow_codegen::GenerateArrowSchemaField(host, os, arm->field,
                                   arrow_codegen::ArrowQuoted(host, std::string(arm->field->name())),
                                   true);
      }
      os << "  builder.EndStruct();\n";
      continue;
    }
    const auto* desc = item->field;
    if (desc->is_map()) {
      os << "  builder.BeginMap(" << arrow_codegen::ArrowQuoted(host, std::string(desc->name()))
         << ", false);\n";
      os << "  if (absl::Status _phaser_status = "
         << host.MessageName(desc->message_type(), true)
         << "::AppendArrowSchema(builder, \"entries\", false, types); "
            "!_phaser_status.ok()) return _phaser_status;\n";
      os << "  builder.EndMap();\n";
      continue;
    }
    if (desc->is_repeated()) {
      if (host.UsesArrayFacade(desc)) {
        os << "  builder.BeginFixedList("
           << arrow_codegen::ArrowQuoted(host, std::string(desc->name())) << ", "
           << host.GetArraySize(desc) << ");\n";
        arrow_codegen::GenerateArrowSchemaField(host, os, desc, "\"item\"", false);
        os << "  builder.EndFixedList();\n";
      } else {
        os << "  builder.BeginList(" << arrow_codegen::ArrowQuoted(host, std::string(desc->name()))
           << ", false);\n";
        arrow_codegen::GenerateArrowSchemaField(host, os, desc, "\"item\"", false);
        os << "  builder.EndList();\n";
      }
      continue;
    }
    arrow_codegen::GenerateArrowSchemaField(host, os, desc, arrow_codegen::ArrowQuoted(host, std::string(desc->name())),
                               arrow_codegen::ArrowFieldNullable(host, desc));
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
  for (const auto& item : host.fields_in_order()) {
    arrow_codegen::GenerateArrowCollectField(host, os, *item);
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::WriteArrow(::phaser::arrow::FieldWriter writer, const "
        "::phaser::arrow::TypeSet& types) const {\n";
  os << "  (void)types;\n";
  int index = 0;
  for (const auto& item : host.fields_in_order()) {
    arrow_codegen::GenerateArrowWriteField(host, os, *item, index);
    ++index;
  }
  os << "  return writer.FinishStruct();\n";
  os << "}\n\n";

  os << "absl::Status " << name
     << "::ReadArrow(::phaser::arrow::FieldReader row) {\n";
  os << "  if (absl::Status _phaser_status = row.status(); !_phaser_status.ok()) "
        "return _phaser_status;\n";
  int read_index = 0;
  for (const auto& item : host.fields_in_order()) {
    arrow_codegen::GenerateArrowReadField(host, os, *item, read_index);
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

void EmitArrowBankInfoFields(MessageCodegenHost& host, std::ostream& os) {
  const std::string name = host.MessageName(host.message());
  os << "  .append_arrow_schema = " << name << "AppendArrowSchema,\n";
  os << "  .collect_arrow_types_at_offset = " << name
     << "CollectArrowTypesAtOffset,\n";
  os << "  .write_arrow_at_offset = " << name << "WriteArrowAtOffset,\n";
  os << "  .read_arrow_at_offset = " << name << "ReadArrowAtOffset,\n";
}

void EmitArrowFileHeaderIncludes(std::ostream& os) {
  os << "#include \"phaser/runtime/columnar_forward.h\"\n";
}

void EmitArrowFileSourceIncludes(std::ostream& os) {
  os << "#include \"phaser/plugins/arrow/arrow.h\"\n";
}

void EmitArrowMessageHeaderFromGenerator(MessageGenerator& generator,
                                         std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitArrowMessageHeader(host, os);
}

void EmitArrowMessageSourceFromGenerator(MessageGenerator& generator,
                                         std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitArrowMessageSource(host, os);
}

void EmitArrowBankInfoFieldsFromGenerator(MessageGenerator& generator,
                                          std::ostream& os) {
  MessageCodegenHost host(generator);
  EmitArrowBankInfoFields(host, os);
}

const GeneratorPlugin kArrowGeneratorPlugin = {
    "arrow",
    20,
    EmitArrowFileHeaderIncludes,
    EmitArrowFileSourceIncludes,
    EmitArrowMessageHeaderFromGenerator,
    EmitArrowMessageSourceFromGenerator,
    EmitArrowBankInfoFieldsFromGenerator,
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
