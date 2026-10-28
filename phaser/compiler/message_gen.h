// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"
#include "phaser/compiler/enum_gen.h"

namespace phaser {

class MessageGenerator;
class MessageCodegenHost;

enum class FrontendStyle { kProtobuf, kRos, kC };

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
  // A singular message stored inside this message.  Its offset is a C++
  // constant, since it follows the other fields and the sizes of the
  // messages before it are only known to the C++ compiler.
  bool inline_message = false;
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
  // C frontend declarations and definitions. `rust` receives the safe Rust
  // wrapper when non-null. `declared_types` records opaque struct names
  // already emitted into the C header.
  void GenerateBindings(std::ostream& decl, std::ostream& def,
                        std::ostream* rust,
                        std::vector<std::string>* declared_types);
  // Python bindings are emitted in three passes so a method can return a type
  // declared later in the file: enums, then class objects, then methods.
  void GeneratePythonEnums(std::ostream& os);
  void GeneratePythonClasses(std::ostream& os);
  void GeneratePythonMethods(std::ostream& os);
  bool PythonUsesAny();

  void GenerateFieldDeclarations(std::ostream& os);

  void GenerateEnums(std::ostream& os);

 private:
  friend class MessageCodegenHost;
  void CompileFields();
  void CompileUnions();
  void FinalizeOffsetsAndSizes();
  // Whether the singular message 'field' is stored inside this message
  // rather than through an offset.
  bool StoresMessageInline(const google::protobuf::FieldDescriptor* field);
  // Whether a message of 'type' can own no more than 'blocks' blocks outside
  // its body, which it takes from 'blocks'.
  bool OwnsAtMostBlocks(const google::protobuf::Descriptor* type, int& blocks);
  // C++ constant expressions for the field's offset in this message and its
  // id in the field metadata.
  std::string FieldOffset(const FieldInfo& field) const;
  std::string FieldId(const FieldInfo& field) const;
  // Whether the field's offset, id and number are in its C++ type rather
  // than in the field.
  bool FieldLocationInType(const FieldInfo& field) const;
  // The C++ type of the field's member, in namespace ::phaser.
  std::string FieldMemberType(const FieldInfo& field) const;
  // Declares the offsets of the inline messages and returns a C++ constant
  // expression for where the message's binary ends.
  std::string GenerateInlineMessageOffsets(std::ostream& os);

  void GenerateDefaultConstructor(std::ostream& os, bool decl);
  void GenerateInternalDefaultConstructor(std::ostream& os, bool decl);
  void GenerateMainConstructor(std::ostream& os, bool decl);
  void GenerateConstructors(std::ostream& os, bool decl);
  void GenerateFieldInitializers(std::ostream& os, const char* sep = ": ");
  void GenerateSizeFunctions(std::ostream& os);
  // The message types this message can reach, starting with itself.
  std::vector<const google::protobuf::Descriptor*> ReachableMessageTypes()
      const;
  // ReachableMessageTypes() as template arguments.
  std::string MetadataTypeList();
  // The InitializeMetadata call that sets up the new buffer at 'pb', a
  // PayloadBuffer** expression, for this message.
  std::string InitializeMetadataCall(const std::string& pb);
  void GenerateFieldMetadata(std::ostream& os);
  void GenerateCreators(std::ostream& os, bool decl);
  void GenerateClear(std::ostream& os, bool decl);
  // The fast path of Clear() for a main message: resetting its buffer.
  void GenerateClearByReset(std::ostream& os);
  void GenerateReleaseAt(std::ostream& os, bool decl);

  void GeneratePublicFieldDeclarations(std::ostream& os);
  void GenerateRosOneofTypes(std::ostream& os);
  void GenerateRosOwnerCopyMove(std::ostream& os, bool decl);
  void GenerateRosMetadata(std::ostream& os);
  void GenerateProtobufCopyMove(std::ostream& os, bool decl);
  void GenerateRosSyncToPayload(std::ostream& os);
  void GenerateProtobufAccessors(std::ostream& os);
  void GenerateFieldProtobufAccessors(std::ostream& os);
  void GenerateFieldProtobufAccessors(std::shared_ptr<FieldInfo> field,
                                      std::shared_ptr<UnionInfo> union_field,
                                      int union_index, std::ostream& os);
  void GeneratePythonField(std::ostream& os,
                           const std::shared_ptr<FieldInfo>& field);
  // FieldCType with a leading :: when the type is already qualified, so it
  // still resolves inside `using namespace` of this message's package.
  std::string PythonValueType(
      const google::protobuf::FieldDescriptor* field);
  void GenerateUnionProtobufAccessors(std::ostream& os);
  void GenerateNestedTypes(std::ostream& os);
  void GenerateFieldNumbers(std::ostream& os);
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
      const google::protobuf::FieldDescriptor* field, int fixed_array_size);
  std::string FieldUnionCType(const google::protobuf::FieldDescriptor* field);
  uint32_t FieldBinarySize(const google::protobuf::FieldDescriptor* field);
  std::string FieldInfoType(const google::protobuf::FieldDescriptor* field);
  std::string SanitizedIdentifier(const std::string& name) const;
  std::string GeneratedClassName() const;
  std::string FieldAccessorName(const std::string& name) const;
  std::string PresenceAccessorName(const std::string& name) const;
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
      const google::protobuf::FieldDescriptor* field, bool stored_inline);
  std::string RosIntrinsicVectorFieldType(
      const google::protobuf::FieldDescriptor* field);
  std::string RosIntrinsicArrayFieldType(
      const google::protobuf::FieldDescriptor* field,
      const std::string& extent);
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
  bool IsCFrontend() const { return frontend_style_ == FrontendStyle::kC; }
  void EmitCField(const google::protobuf::FieldDescriptor* field,
                  std::ostream& decl, std::ostream& def,
                  std::ostream* rust_extern, std::ostream* rust_body,
                  std::vector<std::string>* declared_types,
                  const std::string& self_prefix, const std::string& self_cxx,
                  const std::string& self_raw);
  std::string CppMessageType(const google::protobuf::Descriptor* desc) const;
  std::string CppEnumType(const google::protobuf::EnumDescriptor* desc) const;
  std::string RustMessageType(const google::protobuf::Descriptor* desc) const;

  const google::protobuf::Descriptor* message_;
  std::vector<std::unique_ptr<MessageGenerator>> nested_message_gens_;
  std::vector<std::unique_ptr<EnumGenerator>> enum_gens_;
  std::vector<std::shared_ptr<FieldInfo>> fields_;
  std::map<const google::protobuf::OneofDescriptor*, std::shared_ptr<UnionInfo>>
      unions_;
  std::vector<std::shared_ptr<FieldInfo>> fields_in_order_;
  uint32_t binary_size_ = 4;
  // Where the binary ends, including any inline messages.
  std::string binary_end_;
  uint32_t presence_mask_size_ = 0;
  std::string added_namespace_;
  std::string package_name_;
  bool generate_active_message_ = false;
  FrontendStyle frontend_style_ = FrontendStyle::kProtobuf;
  bool generate_ros_metadata_ = false;
};

}  // namespace phaser
