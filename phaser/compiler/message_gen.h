// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"
#include "phaser/compiler/enum_gen.h"

namespace phaser {

enum class FrontendStyle { kProtobuf, kRos };

struct FieldInfo {
  // Constructor.
  FieldInfo(const google::protobuf::FieldDescriptor* f, uint32_t o, uint32_t i,
            const std::string& name, const std::string& mtype,
            const std::string& ctype, uint32_t size)
      : field(f),
        offset(o),
        id(i),
        member_name(name),
        member_type(mtype),
        c_type(ctype),
        binary_size(size) {}
  virtual ~FieldInfo() = default;
  virtual bool IsUnion() const { return false; }
  const google::protobuf::FieldDescriptor* field;
  uint32_t offset;
  uint32_t id;
  std::string member_name;
  std::string member_type;
  std::string c_type;
  uint32_t binary_size;
};

struct UnionInfo : public FieldInfo {
  // Constructor
  UnionInfo(const google::protobuf::OneofDescriptor* o, uint32_t size,
            const std::string& name, const std::string& type)
      : FieldInfo(nullptr, 0, 0, name, type, "", size), oneof(o) {}
  bool IsUnion() const override { return true; }
  const google::protobuf::OneofDescriptor* oneof;
  std::vector<std::shared_ptr<FieldInfo>> members;
};

class MessageGenerator {
 public:
  MessageGenerator(const google::protobuf::Descriptor* message,
                   const std::string& added_namespace,
                   const std::string& package_name,
                   bool generate_active_message = false,
                   FrontendStyle frontend_style = FrontendStyle::kProtobuf,
                   bool generate_ros_metadata = false)
      : message_(message),
        added_namespace_(added_namespace),
        package_name_(package_name),
        generate_active_message_(generate_active_message),
        frontend_style_(frontend_style),
        generate_ros_metadata_(generate_ros_metadata) {
    for (int i = 0; i < message_->nested_type_count(); i++) {
      nested_message_gens_.push_back(std::make_unique<MessageGenerator>(
          message_->nested_type(i), added_namespace, package_name,
          generate_active_message, frontend_style, generate_ros_metadata));
    }
    // Enums
    for (int i = 0; i < message_->enum_type_count(); i++) {
      enum_gens_.push_back(
          std::make_unique<EnumGenerator>(message_->enum_type(i)));
    }
  }

  absl::Status GenerateHeader(std::ostream& os);
  void GenerateSource(std::ostream& os);

  void GenerateFieldDeclarations(std::ostream& os);

  void GenerateEnums(std::ostream& os);

 private:
  void CompileFields();
  void CompileUnions();
  void FinalizeOffsetsAndSizes();

  void GenerateDefaultConstructor(std::ostream& os, bool decl);
  void GenerateInternalDefaultConstructor(std::ostream& os, bool decl);
  void GenerateMainConstructor(std::ostream& os, bool decl);
  void GenerateConstructors(std::ostream& os, bool decl);
  void GenerateFieldInitializers(std::ostream& os, const char* sep = ": ");
  void GenerateSizeFunctions(std::ostream& os);
  size_t ReachableMessageTypeCount() const;
  void GenerateFieldMetadata(std::ostream& os);
  void GenerateCreators(std::ostream& os, bool decl);
  void GenerateClear(std::ostream& os, bool decl);

  void GeneratePublicFieldDeclarations(std::ostream& os);
  void GenerateRosOneofTypes(std::ostream& os);
  void GenerateRosOwnerCopyMove(std::ostream& os, bool decl);
  void GenerateRosMetadata(std::ostream& os);
  void GenerateRosSyncToPayload(std::ostream& os);
  void GenerateProtobufAccessors(std::ostream& os);
  void GenerateFieldProtobufAccessors(std::ostream& os);
  void GenerateFieldProtobufAccessors(std::shared_ptr<FieldInfo> field,
                                      std::shared_ptr<UnionInfo> union_field,
                                      int union_index, std::ostream& os);
  void GenerateUnionProtobufAccessors(std::ostream& os);
  void GenerateNestedTypes(std::ostream& os);
  void GenerateFieldNumbers(std::ostream& os);
  void GenerateSerializedSize(std::ostream& os, bool decl);
  void GenerateSerializer(std::ostream& os, bool decl);
  void GenerateDeserializer(std::ostream& os, bool decl);
  void GenerateROSSerialization(std::ostream& os, bool decl);
  void GenerateDirectProtobufToROS(std::ostream& os);
  void GenerateDirectProtobufField(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& indent);
  void GenerateDirectProtobufSingularField(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& indent);
  void GenerateDirectProtobufReadValue(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& buffer, const std::string& value,
      const std::string& indent);
  void GenerateDirectROSWriteValue(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& value, const std::string& indent);
  void GenerateDirectROSToProtobuf(std::ostream& os);
  void GenerateDirectROSFieldToProtobuf(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& indent);
  void GenerateDirectROSReadValue(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& reader, const std::string& value,
      const std::string& indent);
  void GenerateDirectProtoWriteValue(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& value, const std::string& field_number,
      const std::string& indent, bool raw = false);
  std::string DirectProtobufValueType(
      const google::protobuf::FieldDescriptor* field) const;
  void GenerateROSFieldSize(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& value_expression, const std::string& indent);
  void GenerateROSFieldWrite(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& value_expression, const std::string& indent);
  void GenerateROSFieldRead(
      std::ostream& os, const google::protobuf::FieldDescriptor* field,
      const std::string& target_expression, const std::string& indent,
      bool append = false, const std::string& index_expression = "",
      int union_index = -1);
  std::string ROSFieldValueExpression(
      const std::shared_ptr<FieldInfo>& field,
      const std::shared_ptr<UnionInfo>& union_field = nullptr,
      int union_index = -1) const;

  void GenerateProtobufSerialization(std::ostream& os);
  void GenerateIndent(std::ostream& os);
  void GenerateStreamer(std::ostream& os);
  bool IsAny(const google::protobuf::Descriptor* desc);
  bool IsAny(const google::protobuf::FieldDescriptor* field);
  void GenerateCopy(std::ostream& os, bool decl);
  void GenerateDebugString(std::ostream& os);
  void GeneratePhaserBank(std::ostream& os);
  void GenerateMessageInfo(std::ostream& os, bool decl);
  void GenerateFieldInfo(int index, std::shared_ptr<FieldInfo> field,
                         std::shared_ptr<UnionInfo> union_field,
                         int union_index, std::ostream& os);

  std::string EnumName(const google::protobuf::EnumDescriptor* desc);
  // If is_ref is true, it changes how the generator treats google.protobuf.Any.
  // For a reference to a google.protobuf.Any, we use an internal
  // ::phaser::AnyMessage type.
  std::string MessageName(const google::protobuf::Descriptor* desc,
                          bool is_ref = false);
  std::string FieldCFieldType(const google::protobuf::FieldDescriptor* field);
  std::string FieldCType(const google::protobuf::FieldDescriptor* field);
  std::string FieldRepeatedCType(
      const google::protobuf::FieldDescriptor* field);
  std::string FieldRepeatedVectorCType(
      const google::protobuf::FieldDescriptor* field);
  std::string FieldRepeatedArrayCType(
      const google::protobuf::FieldDescriptor* field, int array_size);
  std::string FieldUnionCType(const google::protobuf::FieldDescriptor* field);
  uint32_t FieldBinarySize(const google::protobuf::FieldDescriptor* field);
  std::string FieldInfoType(const google::protobuf::FieldDescriptor* field);
  std::string SanitizedIdentifier(const std::string& name) const;
  std::string MemberVariableName(const std::string& proto_name) const;
  std::string OneofVariantTypeName(
      const google::protobuf::OneofDescriptor* oneof) const;
  std::string OneofAlternativeTypeName(
      const google::protobuf::FieldDescriptor* field) const;
  int GetArraySize(const google::protobuf::FieldDescriptor* field) const;
  bool UsesArrayFacade(const google::protobuf::FieldDescriptor* field) const;
  bool IsRosTime(const google::protobuf::Descriptor* desc) const;
  bool IsRosDuration(const google::protobuf::Descriptor* desc) const;
  bool IsRosHeader(const google::protobuf::Descriptor* desc) const;
  bool IsRosIntrinsic(const google::protobuf::FieldDescriptor* field) const;
  std::string RosIntrinsicFieldType(
      const google::protobuf::FieldDescriptor* field);
  std::string RosIntrinsicCType(
      const google::protobuf::FieldDescriptor* field);
  absl::Status ValidateFieldOptions() const;
  absl::Status ValidateRosMetadataOptions() const;
  absl::Status ValidateArraySizeOption(
      const google::protobuf::FieldDescriptor* field) const;
  absl::Status ValidateRosHeaderDescriptor() const;
  bool IsRosFrontend() const {
    return frontend_style_ == FrontendStyle::kRos;
  }

  const google::protobuf::Descriptor* message_;
  std::vector<std::unique_ptr<MessageGenerator>> nested_message_gens_;
  std::vector<std::unique_ptr<EnumGenerator>> enum_gens_;
  std::vector<std::shared_ptr<FieldInfo>> fields_;
  std::map<const google::protobuf::OneofDescriptor*, std::shared_ptr<UnionInfo>>
      unions_;
  std::vector<std::shared_ptr<FieldInfo>> fields_in_order_;
  uint32_t binary_size_ = 4;
  uint32_t presence_mask_size_ = 0;
  std::string added_namespace_;
  std::string package_name_;
  bool generate_active_message_ = false;
  FrontendStyle frontend_style_ = FrontendStyle::kProtobuf;
  bool generate_ros_metadata_ = false;
};

}  // namespace phaser
