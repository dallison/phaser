#pragma once

// Fields that handle google.protobuf.Any messages.
// These are mesasges that contain two fields:
// 1. A string called 'type_url' that specifies the type of the message
// 2. A bytes field called 'value' that contains a message.
//
// In regular protobuf, the 'value' field is a serialized message, but
// in phaser, the value is the binary of the message.
//
#include "phaser/runtime/fields.h"

#include <stddef.h>

namespace phaser {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

// Hand-coded message class that represents a google.protobuf.Any message.
class AnyMessage : public Message {
  public:
  AnyMessage(phaser::InternalDefault d)
      : type_url_(offsetof(AnyMessage, type_url_), HeaderSize() + 0, 0, 1),
        value_(offsetof(AnyMessage, value_), HeaderSize() + 4, 1, 2) {}

  AnyMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
             toolbelt::BufferOffset offset)
      : Message(runtime, offset),
        type_url_(offsetof(AnyMessage, type_url_), HeaderSize() + 0, 0, 1),
        value_(offsetof(AnyMessage, value_), HeaderSize() + 4, 1, 2) {}

  static constexpr size_t BinarySize() { return HeaderSize() + 8; }
  static constexpr size_t PresenceMaskSize() { return 0; }
  static constexpr uint32_t HeaderSize() { return 4 + PresenceMaskSize(); }
  struct FieldData {
    uint32_t num;
    struct Field {
      uint32_t number;
      uint32_t offset : 24;
      uint32_t id : 8;
    } fields[2];
  };
  static constexpr FieldData field_data = {
      .num = 6,
      .fields = {
          {.number = 1, .offset = 4, .id = 0},
          {.number = 2, .offset = 8, .id = 0},
      }};
  static std::string GetName() { return "Any"; }
  static std::string GetFullName() { return "google.protobuf.Any"; }

  friend std::ostream &operator<<(std::ostream &os, const AnyMessage &msg);

  void Indent(int indent) {
    type_url_.Indent(indent);
    value_.Indent(indent);
  }

  // Protobuf accessors.
  std::string_view type_url() const { return type_url_.Get(); }
  template <typename Str>
  void set_type_url(Str str) { type_url_.Set(str); }
  void clear_type_url() { type_url_.Clear(); }
  bool has_type_url() const { return type_url_.IsPresent(); }

  std::string_view value() const { return value_.Get(); }
  template <typename Str>
  void set_value(Str str) { value_.Set(str); }
  void clear_value() { value_.Clear(); }
  bool has_value() const { return value_.IsPresent(); }

  void Clear() {
    type_url_.Clear();
    value_.Clear();
  }

  bool operator==(const AnyMessage &other) const {
    return type_url_ == other.type_url_ && value_ == other.value_;
  }
  bool operator!=(const AnyMessage &other) const {
    return type_url_ != other.type_url_ || value_ != other.value_;
  }
  size_t SerializedSize() const {
    size_t size = 0;
    if (type_url_.IsPresent()) {
      size += type_url_.SerializedSize();
    }
    if (value_.IsPresent()) {
      size += value_.SerializedSize();
    }
    return size;
  }

  absl::Status Serialize(phaser::ProtoBuffer &buffer) const {
    if (type_url_.IsPresent()) {
      if (absl::Status status = type_url_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (value_.IsPresent()) {
      if (absl::Status status = value_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Deserialize(phaser::ProtoBuffer &buffer) {
    while (!buffer.Eof()) {
      absl::StatusOr<uint32_t> tag =
          buffer.DeserializeVarint<uint32_t, false>();
      if (!tag.ok()) {
        return tag.status();
      }
      uint32_t field_number = *tag >> phaser::ProtoBuffer::kFieldIdShift;
      switch (field_number) {
      case 1:
        if (absl::Status status = type_url_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 2:
        if (absl::Status status = value_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      default:
        if (absl::Status status = buffer.SkipTag(*tag); !status.ok()) {
          return status;
        }
      }
    }
    return absl::OkStatus();
  }

  phaser::StringField type_url_;
  phaser::StringField value_;
};

#pragma clang diagnostic pop


class AnyField : public IndirectMessageField<AnyMessage> {
public:
  AnyField(uint32_t boff, uint32_t offset, int id, int number)
      : IndirectMessageField(boff, offset, id, number){}

  bool operator==(const AnyField &other) const { return IndirectMessageField<AnyMessage>::operator==(other); }
  bool operator!=(const AnyField &other) const { return IndirectMessageField<AnyMessage>::operator!=(other); }

  bool PackFrom(const Message &msg) { return true; }

  template <typename T> bool PackFrom(const T &msg) { return true; }

  bool UnpackTo(Message *msg) const { return true; }

  template <typename T> bool UnpackTo(T *msg) const { return true; }

  template <typename T> bool Is() const { return true; }

};

} // namespace phaser
