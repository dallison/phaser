#pragma once
#include <iostream>

#include "phaser/runtime/message.h"

#include "phaser/runtime/fields.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/union.h"
#include "phaser/runtime/vectors.h"
#include "toolbelt/payload_buffer.h"

namespace phaser {
#define DEFINE_PRIMITIVE_FIELD_STREAMER(cname)                                 \
  template <bool FixedSize, bool Signed>                                       \
  inline std::ostream &operator<<(                                             \
      std::ostream &os, const cname##Field<FixedSize, Signed> &field) {        \
    os << field.GetForPrinting();                                            \
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
inline std::ostream &operator<<(std::ostream &os,
                                const EnumField<Enum, Stringizer, Parser> &field) {
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

}

