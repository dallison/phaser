#pragma once
#include <iostream>

#include "phaser/runtime/message.h"

#include "phaser/runtime/any.h"
#include "phaser/runtime/fields.h"
#include "phaser/runtime/iterators.h"
#include "toolbelt/payload_buffer.h"
#include "phaser/runtime/phaser_bank.h"
#include "phaser/runtime/union.h"
#include "phaser/runtime/vectors.h"
#include "toolbelt/hexdump.h"

namespace phaser {
#define DEFINE_PRIMITIVE_FIELD_STREAMER(cname)                                 \
  template <bool FixedSize, bool Signed>                                       \
  inline std::ostream &operator<<(                                             \
      std::ostream &os, const cname##Field<FixedSize, Signed> &field) {        \
    os << field.GetForPrinting();                                              \
    return os;                                                                 \
  }

DEFINE_PRIMITIVE_FIELD_STREAMER(Int32)
DEFINE_PRIMITIVE_FIELD_STREAMER(Uint32)
DEFINE_PRIMITIVE_FIELD_STREAMER(Int64)
DEFINE_PRIMITIVE_FIELD_STREAMER(Uint64)
DEFINE_PRIMITIVE_FIELD_STREAMER(Double)
DEFINE_PRIMITIVE_FIELD_STREAMER(Float)
DEFINE_PRIMITIVE_FIELD_STREAMER(Bool)

#undef DEFINE_PRIMITIVE_FIELD_STREAMER

inline std::ostream &operator<<(std::ostream &os, const StringField &field) {
  os << "\"" << field.Get() << "\"";
  return os;
}

inline std::ostream &operator<<(std::ostream &os,
                                const NonEmbeddedStringField &field) {
  os << "\"" << field.Get() << "\"";
  return os;
}

template <typename Enum, typename Stringizer, typename Parser>
inline std::ostream &
operator<<(std::ostream &os, const EnumField<Enum, Stringizer, Parser> &field) {
  os << field.GetForPrinting();
  return os;
}

template <typename T>
inline std::ostream &operator<<(std::ostream &os,
                                const IndirectMessageField<T> &field) {
  os << "{\n";
  field.Indent(2);
  os << field.Get();
  field.Indent(-2);
  field.PrintIndent(os);
  os << "}";
  return os;
}

template <typename T>
inline std::ostream &operator<<(std::ostream &os,
                                const MessageObject<T> &field) {
  os << "{\n";
  field.Indent(2);
  os << field.Get();
  field.Indent(-2);
  field.PrintIndent(os);
  os << "}";
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const AnyField &field) {
  // If the any field hasn't been set, don't print anything.
  if (!field.has_type_url() || !field.has_value()) {
    return os;
  }
  if (field.type_url().empty()) {
    return os;
  }
  os << field.Msg();
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const AnyMessage &msg) {
  if (msg.value_.IsPresent()) {
    // The value contains a message.  We use the phaser bank to stream
    // actual message contents.
    const char *addr = msg.value().data();
    os << "{\n";
    msg.value_.Indent(2);
    msg.value_.PrintIndent(os);
    std::string type = msg.MessageTypeName();
    os << "[" << msg.type_url() << "] {\n";
    msg.value_.Indent(2);
    absl::StatusOr<const Message *> s =
        PhaserBankMakeExisting(type, msg.runtime, addr);
    if (!s.ok()) {
      os << "Error: " << s.status() << std::endl;
      msg.value_.Indent(-2);
      return os;
    }
    std::unique_ptr<const Message> src(*s);
    if (absl::Status st =
            PhaserStreamTo(type, *src, os, msg.value_.GetIndent());
        !st.ok()) {
      os << "Error: " << st << std::endl;
      msg.value_.Indent(-2);
      return os;
    }
    msg.value_.Indent(-2);
    msg.value_.PrintIndent(os);
    os << "}\n";
    msg.value_.Indent(-2);
    msg.value_.PrintIndent(os);
    os << "}";
  }
  return os;
}
}
