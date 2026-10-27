// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/protobuf/gen/protobuf_wire_codegen.h"

#include "phaser/compiler/message_codegen_host.h"

namespace phaser {
namespace protobuf_wire_codegen {

void GenerateSerializedSize(MessageCodegenHost& host, std::ostream& os, bool decl) {
  if (decl) {
    os << "  size_t SerializedSize() const;\n";
    return;
  }
  os << "size_t " << host.MessageName(host.message()) << "::SerializedSize() const {\n";
  os << "  size_t _phaser_serialized_size = 0;\n";
  for (auto& field : host.fields()) {
    if (field->field->is_repeated()) {
      os << "  _phaser_serialized_size += " << field->member_name
         << ".SerializedSize();\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    _phaser_serialized_size += " << field->member_name
         << ".SerializedSize();\n";
      os << "  }\n";
    }
  }
  for (auto& [oneof, u] : host.unions()) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      os << "    _phaser_serialized_size += " << u->member_name
         << ".SerializedSize<" << i << ">(" << field->field->number() << ");\n";
      os << "    break;\n";
    }
    os << "  }\n";
  }
  os << "  return _phaser_serialized_size;\n";
  os << "}\n\n";
}

void GenerateSerializer(MessageCodegenHost& host, std::ostream& os, bool decl) {
  if (decl) {
    os << R"XXX(  absl::Status Serialize(::phaser::ProtoBuffer &_phaser_buffer) const {
    if constexpr (::phaser::kTrustSerializedSize) {
      if (absl::Status _phaser_status =
              _phaser_buffer.EnsureSpace(SerializedSize());
          !_phaser_status.ok()) {
        return _phaser_status;
      }
    }
    return PhaserSerialize(::phaser::ProtoMessageWriter(_phaser_buffer));
  }
  // Serialize() into a buffer that, with kTrustSerializedSize, already has
  // room for the message.
  absl::Status PhaserSerialize(
      ::phaser::ProtoMessageWriter _phaser_buffer) const;
)XXX";
    return;
  }
  os << "absl::Status " << host.MessageName(host.message())
     << "::PhaserSerialize(::phaser::ProtoMessageWriter _phaser_buffer) "
        "const {\n";
  os << "  if (AllocationFailed()) return "
        "::phaser::internal::AllocationFailedError();\n";
  for (auto& field : host.fields()) {
    if (field->field->is_repeated()) {
      os << "  if (absl::Status _phaser_status = " << field->member_name
         << ".Serialize(_phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
    } else {
      os << "  if (" << field->member_name << ".IsPresent()) {\n";
      os << "    if (absl::Status _phaser_status = " << field->member_name
         << ".Serialize(_phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "  }\n";
    }
  }
  for (auto& [oneof, u] : host.unions()) {
    os << "  switch (" << u->member_name << ".Discriminator()) {\n";
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "  case " << field->field->number() << ":\n";
      os << "    if (absl::Status _phaser_status = " << u->member_name
         << ".Serialize<" << i << ">(" << field->field->number()
         << ", _phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "    break;\n";
    }
    os << "  }\n";
  }
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void GenerateDeserializer(MessageCodegenHost& host, std::ostream& os, bool decl) {
  if (decl) {
    os << "  absl::Status Deserialize("
          "::phaser::ProtoBuffer &_phaser_buffer);\n";
    return;
  }
  os << "absl::Status " << host.MessageName(host.message())
     << "::Deserialize(::phaser::ProtoBuffer &_phaser_buffer) {";
  bool has_array_fields = false;
  for (auto& field : host.fields()) {
    if (host.UsesArrayFacade(field->field)) {
      has_array_fields = true;
      break;
    }
  }
  if (has_array_fields) {
    os << "\n";
    for (auto& field : host.fields()) {
      if (host.UsesArrayFacade(field->field)) {
        os << "  " << field->member_name << ".BeginDeserialize();\n";
      }
    }
  }
  os << R"XXX(
  while (!_phaser_buffer.Eof()) {
    absl::StatusOr<uint32_t> _phaser_tag =
        _phaser_buffer.DeserializeVarint<uint32_t, false>();
    if (!_phaser_tag.ok()) {
      return _phaser_tag.status();
    }
    uint32_t _phaser_field_number =
        *_phaser_tag >> ::phaser::ProtoBuffer::kFieldIdShift;
    switch (_phaser_field_number) {
)XXX";
  for (auto& field : host.fields()) {
    os << "    case " << field->field->number() << ":\n";
    os << "      if (absl::Status _phaser_status = " << field->member_name
       << ".Deserialize(_phaser_buffer); !_phaser_status.ok()) "
          "return _phaser_status;\n";
    os << "      break;\n";
  }
  for (auto& [oneof, u] : host.unions()) {
    for (size_t i = 0; i < u->members.size(); i++) {
      auto& field = u->members[i];
      os << "    case " << field->field->number() << ":\n";
      os << "      if (absl::Status _phaser_status = " << u->member_name
         << ".Deserialize<" << i << ">(" << field->field->number()
         << ", _phaser_buffer); !_phaser_status.ok()) "
            "return _phaser_status;\n";
      os << "      break;\n";
    }
  }
  os << R"XXX(
    default:
      if (absl::Status _phaser_status =
              _phaser_buffer.SkipTag(*_phaser_tag);
          !_phaser_status.ok()) {
        return _phaser_status;
      }
    }
  }
)XXX";
  if (has_array_fields) {
    os << "\n";
    for (auto& field : host.fields()) {
      if (host.UsesArrayFacade(field->field)) {
        os << "  if (absl::Status _phaser_status = " << field->member_name
           << ".FinalizeDeserialize(); !_phaser_status.ok()) "
              "return _phaser_status;\n";
      }
    }
  }
  os << "  if (AllocationFailed()) return "
        "::phaser::internal::AllocationFailedError();\n";
  os << "  return absl::OkStatus();\n";
  os << "}\n\n";
}

void GenerateProtobufSerialization(MessageCodegenHost& host, std::ostream& os) {
  os << R"XXX(
  // This is the size of the message on the wire.  It is not the serialized protobuf size.
  size_t ByteSizeLong() const {
    return ZeroCopySize();
  }

  int ByteSize() const {
    return static_cast<int>(ByteSizeLong());
  }

  // With kTrustSerializedSize, '_phaser_size' must be at least
  // SerializedSize().
  bool SerializeToArray(char* _phaser_array, size_t _phaser_size) const {
    assert(!::phaser::kTrustSerializedSize ||
           _phaser_size >= SerializedSize());
    ::phaser::ProtoBuffer _phaser_buffer(_phaser_array, _phaser_size);
    if (absl::Status _phaser_status =
            PhaserSerialize(::phaser::ProtoMessageWriter(_phaser_buffer));
        !_phaser_status.ok()) return false;
    return true;
  }

  bool ParseFromArray(const char* _phaser_array, size_t _phaser_size) {
    ::phaser::ProtoBuffer _phaser_buffer(_phaser_array, _phaser_size);
    if (absl::Status _phaser_status = Deserialize(_phaser_buffer);
        !_phaser_status.ok()) return false;
    return true;
  }

  // String serialization.
  bool SerializeToString(std::string* _phaser_string) const {
    size_t _phaser_size = SerializedSize();
    _phaser_string->resize(_phaser_size);
    return SerializeToArray(&(*_phaser_string)[0], _phaser_size);
  }

  std::string SerializeAsString() const {
    std::string _phaser_string;
    SerializeToString(&_phaser_string);
    return _phaser_string;
  }

  bool ParseFromString(const std::string& _phaser_string) {
    return ParseFromArray(_phaser_string.data(), _phaser_string.size());
  }
)XXX";
}


}  // namespace protobuf_wire_codegen
}  // namespace phaser
