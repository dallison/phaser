#pragma once
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"
#include "phaser/compiler/enum_gen.h"
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace phaser {

struct FieldInfo {
  // Constructor.
  FieldInfo(const google::protobuf::FieldDescriptor *f, uint32_t o, uint32_t i,
            const std::string &name, const std::string &mtype,
            const std::string &ctype, uint32_t size)
      : field(f), offset(o), id(i), member_name(name), member_type(mtype),
        c_type(ctype), binary_size(size) {}
  const google::protobuf::FieldDescriptor *field;
  uint32_t offset;
  uint32_t id;
  std::string member_name;
  std::string member_type;
  std::string c_type;
  uint32_t binary_size;
};

struct UnionInfo : public FieldInfo {
  // Constructor
  UnionInfo(const google::protobuf::OneofDescriptor *o, uint32_t size,
            const std::string &name, const std::string &type)
      : FieldInfo(nullptr, 0, 0, name, type, "", 4), oneof(o),
        binary_size(size) {}
  const google::protobuf::OneofDescriptor *oneof;
  std::vector<std::shared_ptr<FieldInfo>> members;
  uint32_t binary_size;
};

class MessageGenerator {
public:
  MessageGenerator(const google::protobuf::Descriptor *message)
      : message_(message) {
    for (int i = 0; i < message_->nested_type_count(); i++) {
      nested_message_gens_.push_back(
          std::make_unique<MessageGenerator>(message_->nested_type(i)));
    }
    // Enums
    for (int i = 0; i < message_->enum_type_count(); i++) {
      enum_gens_.push_back(
          std::make_unique<EnumGenerator>(message_->enum_type(i)));
    }
  }

  void GenerateHeader(std::ostream &os);
  void GenerateSource(std::ostream &os);

  void GenerateFieldDeclarations(std::ostream &os);

  void GenerateEnums(std::ostream &os);
  
private:
  void CompileFields();
  void CompileUnions();
  void FinalizeOffsetsAndSizes();

  void GenerateDefaultConstructor(std::ostream &os, bool decl);
  void GenerateInternalDefaultConstructor(std::ostream &os, bool decl);
  void GenerateMainConstructor(std::ostream &os, bool decl);
  void GenerateConstructors(std::ostream &os, bool decl);
  void GenerateFieldInitializers(std::ostream &os, const char *sep = ": ");
  void GenerateSizeFunctions(std::ostream &os);
  void GenerateFieldMetadata(std::ostream &os);
  void GenerateCreators(std::ostream &os, bool decl);
  void GenerateClear(std::ostream &os, bool decl);

  void GenerateProtobufAccessors(std::ostream &os);
  void GenerateFieldProtobufAccessors(std::ostream &os);
  void GenerateFieldProtobufAccessors(std::shared_ptr<FieldInfo> field, std::shared_ptr<UnionInfo> union_field, int union_index, std::ostream &os);
  void GenerateUnionProtobufAccessors(std::ostream &os);
  void GenerateNestedTypes(std::ostream &os);
  void GenerateFieldNumbers(std::ostream &os);
  void GenerateSerializedSize(std::ostream &os, bool decl);
  void GenerateSerializer(std::ostream &os, bool decl);
  void GenerateDeserializer(std::ostream &os, bool decl);

  void GenerateProtobufSerialization(std::ostream &os);

  const google::protobuf::Descriptor *message_;
  std::vector<std::unique_ptr<MessageGenerator>> nested_message_gens_;
  std::vector<std::unique_ptr<EnumGenerator>> enum_gens_;
  std::vector<std::shared_ptr<FieldInfo>> fields_;
  std::map<const google::protobuf::OneofDescriptor *,
           std::shared_ptr<UnionInfo>>
      unions_;
  uint32_t binary_size_ = 4;
  uint32_t presence_mask_size_ = 0;
};

} // namespace phaser
