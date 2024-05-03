#include "absl/strings/str_format.h"
#include "phaser/runtime/runtime.h"
#include "phaser/testdata/TestMessage.pb.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"
#include <gtest/gtest.h>
#include <sstream>

using PayloadBuffer = toolbelt::PayloadBuffer;
using BufferOffset = toolbelt::BufferOffset;
using Message = phaser::Message;
using VectorHeader = toolbelt::VectorHeader;
using StringHeader = toolbelt::StringHeader;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

enum class EnumTest : uint16_t { UNSET = 0, FOO = 0xda, BAR = 0xad };

struct EnumTestStringizer {
  std::string operator()(EnumTest e) const {
    switch (e) {
    case EnumTest::FOO:
      return "FOO";
    case EnumTest::BAR:
      return "BAR";
    case EnumTest::UNSET:
      return "UNSET";
    }
  }
};

struct EnumTestParser {
  EnumTest operator()(std::string_view s) const {
    if (s == "FOO") {
      return EnumTest::FOO;
    } else if (s == "BAR") {
      return EnumTest::BAR;
    }
    return EnumTest::UNSET;
  }
};

struct InnerMessage : public Message {
  InnerMessage(phaser::InternalDefault d)
      : str_(offsetof(InnerMessage, str_), HeaderSize() + 0, 0, 10),
        f_(offsetof(InnerMessage, f_), HeaderSize() + 8, 1, 20),
        e_(offsetof(InnerMessage, e_), HeaderSize() + 16, 2, 30),
        ev_(offsetof(InnerMessage, ev_), HeaderSize() + 20, 0, 40),
        uv_(offsetof(InnerMessage, uv_), HeaderSize() + 28, 0, 0, {50, 60}) {}

  InnerMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
               toolbelt::BufferOffset offset)
      : Message(runtime, offset),
        str_(offsetof(InnerMessage, str_), HeaderSize() + 0, 0, 10),
        f_(offsetof(InnerMessage, f_), HeaderSize() + 8, 1, 20),
        e_(offsetof(InnerMessage, e_), HeaderSize() + 16, 2, 30),
        ev_(offsetof(InnerMessage, ev_), HeaderSize() + 20, 0, 40),
        uv_(offsetof(InnerMessage, uv_), HeaderSize() + 28, 0, 0, {50, 60}) {}

  static constexpr size_t BinarySize() { return HeaderSize() + 36; }
  static constexpr size_t PresenceMaskSize() { return 4; }
  static constexpr uint32_t HeaderSize() { return 4 + PresenceMaskSize(); }
  struct FieldData {
    uint32_t num;
    struct Field {
      uint32_t number;
      uint32_t offset : 24;
      uint32_t id : 8;
    } fields[6];
  };
  static constexpr FieldData field_data = {
      .num = 6,
      .fields = {
          {.number = 10, .offset = 8, .id = 0},
          {.number = 20, .offset = 16, .id = 1},
          {.number = 30, .offset = 24, .id = 2},
          {.number = 40, .offset = 28, .id = 0},
          {.number = 50, .offset = 36, .id = 0},
          {.number = 60, .offset = 36, .id = 0},
      }};
  static std::string GetName() { return "InnerMessage"; }

  friend std::ostream &operator<<(std::ostream &os, const InnerMessage &msg);

  void Indent(int indent) {
    str_.Indent(indent);
    f_.Indent(indent);
    e_.Indent(indent);
    ev_.Indent(indent);
    uv_.Indent(indent);
  }

  // Protobuf accessors.
  std::string_view str() const { return str_.Get(); }
  void set_str(const std::string &str) { str_.Set(str); }
  void clear_str() { str_.Clear(); }
  bool has_str() const { return str_.IsPresent(); }

  uint64_t f() const { return f_.Get(); }
  void set_f(uint64_t f) { f_.Set(f); }
  void clear_f() { f_.Clear(); }
  bool has_f() const { return f_.IsPresent(); }

  EnumTest e() const { return e_.Get(); }
  void set_e(EnumTest e) { e_.Set(e); }
  void clear_e() { e_.Clear(); }
  bool has_e() const { return e_.IsPresent(); }

  void Clear() {
    str_.Clear();
    f_.Clear();
    e_.Clear();
    ev_.Clear();
    uv_.Clear<0>();
    uv_.Clear<1>();
  }

  size_t SerializedSize() const {
    size_t size = 0;
    if (str_.IsPresent()) {
      size += str_.SerializedSize();
    }
    if (f_.IsPresent()) {
      size += f_.SerializedSize();
    }
    if (e_.IsPresent()) {
      size += e_.SerializedSize();
    }
    size += ev_.SerializedSize();
    switch (uv_.Discriminator()) {
    case 50:
      size += uv_.SerializedSize<0>(50);
      break;
    case 60:
      size += uv_.SerializedSize<1>(60);
      break;
    }
    return size;
  }

  absl::Status Serialize(phaser::ProtoBuffer &buffer) const {
    if (str_.IsPresent()) {
      if (absl::Status status = str_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (f_.IsPresent()) {
      if (absl::Status status = f_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (e_.IsPresent()) {
      if (absl::Status status = e_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (absl::Status status = ev_.Serialize(buffer); !status.ok()) {
      return status;
    }

    switch (uv_.Discriminator()) {
    case 50:
      if (absl::Status status = uv_.Serialize<0>(50, buffer); !status.ok()) {
        return status;
      }
      break;
    case 60:
      if (absl::Status status = uv_.Serialize<1>(60, buffer); !status.ok()) {
        return status;
      }
      break;
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
      case 10:
        if (absl::Status status = str_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 20:
        if (absl::Status status = f_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 30:
        if (absl::Status status = e_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 40:
        if (absl::Status status = ev_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 50:
        if (absl::Status status = uv_.Deserialize<0>(50, buffer);
            !status.ok()) {
          return status;
        }
        break;
      case 60:
        if (absl::Status status = uv_.Deserialize<1>(60, buffer);
            !status.ok()) {
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

  phaser::StringField str_;
  phaser::Uint64Field<true, false> f_; // Fixed size.
  phaser::EnumField<EnumTest, EnumTestStringizer, EnumTestParser> e_;
  phaser::EnumVectorField<EnumTest, EnumTestStringizer, EnumTestParser, false>
      ev_;
  phaser::UnionField<
      phaser::UnionUint32Field<false, false>,
      phaser::UnionEnumField<EnumTest, EnumTestStringizer, EnumTestParser>>
      uv_;
};

inline std::ostream &operator<<(std::ostream &os, const InnerMessage &msg) {
  if (msg.str_.IsPresent()) {
    msg.str_.PrintIndent(os);
    os << "str: " << msg.str_ << std::endl;
  }
  if (msg.f_.IsPresent()) {
    msg.f_.PrintIndent(os);
    os << "f: " << msg.f_ << std::endl;
  }
  if (msg.e_.IsPresent()) {
    msg.e_.PrintIndent(os);
    os << "e: " << msg.e_ << std::endl;
  }
  for (auto &v : msg.ev_) {
    msg.ev_.PrintIndent(os);
    os << "ev: " << EnumTestStringizer()(v) << std::endl;
  }
  switch (msg.uv_.Discriminator()) {
  case 50:
    msg.uv_.PrintIndent(os);
    os << "uva: ";
    msg.uv_.Print<0>(os);
    os << std::endl;
    break;
  case 60:
    msg.uv_.PrintIndent(os);
    os << "uvb: ";
    msg.uv_.Print<1>(os);
    os << std::endl;
    break;
  }
  return os;
}

struct TestMessage : public Message {
  // Default constructor makes a dynamic payload buffer.
  TestMessage(size_t initial_size = 1024)
      : x_(offsetof(TestMessage, x_), HeaderSize() + 0, 0, 100),
        y_(offsetof(TestMessage, y_), HeaderSize() + 8, 1, 101),
        s_(offsetof(TestMessage, s_), HeaderSize() + 16, 0, 102),
        m_(offsetof(TestMessage, m_), HeaderSize() + 20, 0, 103),
        vi32_(offsetof(TestMessage, vi32_), HeaderSize() + 24, 0, 104),
        vstr_(offsetof(TestMessage, vstr_), HeaderSize() + 32, 0, 105),
        vm_(offsetof(TestMessage, vm_), HeaderSize() + 40, 0, 106),
        u1_(offsetof(TestMessage, u1_), HeaderSize() + 48, 0, 0, {107, 108}),
        u2_(offsetof(TestMessage, u2_), HeaderSize() + 56, 0, 0, {109, 110}),
        u3_(offsetof(TestMessage, u3_), HeaderSize() + 64, 0, 0, {111, 112}) {
    InitDynamicMutable(initial_size);
  }

  TestMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
              toolbelt::BufferOffset offset)
      : Message(runtime, offset),
        x_(offsetof(TestMessage, x_), HeaderSize() + 0, 0, 100),
        y_(offsetof(TestMessage, y_), HeaderSize() + 8, 1, 101),
        s_(offsetof(TestMessage, s_), HeaderSize() + 16, 0, 102),
        m_(offsetof(TestMessage, m_), HeaderSize() + 20, 0, 103),
        vi32_(offsetof(TestMessage, vi32_), HeaderSize() + 24, 0, 104),
        vstr_(offsetof(TestMessage, vstr_), HeaderSize() + 32, 0, 105),
        vm_(offsetof(TestMessage, vm_), HeaderSize() + 40, 0, 106),
        u1_(offsetof(TestMessage, u1_), HeaderSize() + 48, 0, 0, {107, 108}),
        u2_(offsetof(TestMessage, u2_), HeaderSize() + 56, 0, 0, {109, 110}),
        u3_(offsetof(TestMessage, u3_), HeaderSize() + 64, 0, 0, {111, 112}) {}

  static TestMessage CreateMutable(void *addr, size_t size) {
    toolbelt::PayloadBuffer *pb = new (addr) toolbelt::PayloadBuffer(size);
    toolbelt::PayloadBuffer::AllocateMainMessage(&pb,
                                                 TestMessage::BinarySize());
    auto runtime = std::make_shared<phaser::MutableMessageRuntime>(pb);
    auto msg = TestMessage(runtime, pb->message);
    msg.InstallMetadata<TestMessage>();
    return msg;
  }

  static TestMessage CreateReadonly(void *addr) {
    toolbelt::PayloadBuffer *pb =
        reinterpret_cast<toolbelt::PayloadBuffer *>(addr);
    auto runtime = std::make_shared<phaser::MessageRuntime>(pb);
    return TestMessage(runtime, pb->message);
  }

  static TestMessage CreateDynamicMutable(size_t initial_size = 1024) {
    toolbelt::PayloadBuffer *pb = phaser::NewDynamicBuffer(initial_size);
    toolbelt::PayloadBuffer::AllocateMainMessage(&pb,
                                                 TestMessage::BinarySize());
    auto runtime = std::make_shared<phaser::DynamicMutableMessageRuntime>(pb);
    auto msg = TestMessage(runtime, pb->message);
    msg.InstallMetadata<TestMessage>();
    return msg;
  }

  void InitDynamicMutable(size_t initial_size = 1024) {
    toolbelt::PayloadBuffer *pb = phaser::NewDynamicBuffer(initial_size);
    toolbelt::PayloadBuffer::AllocateMainMessage(&pb,
                                                 TestMessage::BinarySize());
    auto runtime = std::make_shared<phaser::DynamicMutableMessageRuntime>(pb);
    this->runtime = runtime;
    this->absolute_binary_offset = pb->message;
    this->InstallMetadata<TestMessage>();
  }

  static constexpr size_t BinarySize() { return HeaderSize() + 72; }
  static constexpr size_t PresenceMaskSize() { return 4; }
  static constexpr uint32_t HeaderSize() { return 4 + PresenceMaskSize(); }
  struct FieldData {
    uint32_t num;
    struct Field {
      uint32_t number;
      uint32_t offset : 24;
      uint32_t id : 8;
    } fields[13];
  };
  static constexpr FieldData field_data = {
      .num = 13,
      .fields = {
          {.number = 100, .offset = 8 + 0, .id = 0},
          {.number = 101, .offset = 8 + 8, .id = 1},
          {.number = 102, .offset = 8 + 16, .id = 0},
          {.number = 103, .offset = 8 + 20, .id = 0},
          {.number = 104, .offset = 8 + 24, .id = 0},
          {.number = 105, .offset = 8 + 32, .id = 0},
          {.number = 106, .offset = 8 + 40, .id = 0},
          {.number = 107, .offset = 8 + 48, .id = 0},
          {.number = 108, .offset = 8 + 48, .id = 0},
          {.number = 109, .offset = 8 + 56, .id = 0},
          {.number = 110, .offset = 8 + 56, .id = 0},
          {.number = 111, .offset = 8 + 64, .id = 0},
          {.number = 112, .offset = 8 + 64, .id = 0},
      }};
  static std::string GetName() { return "TestMessage"; }

  void Clear() {
    x_.Clear();
    y_.Clear();
    s_.Clear();
    m_.Clear();
    vi32_.Clear();
    vstr_.Clear();
    vm_.Clear();
    u1_.Clear<0>();
    u1_.Clear<1>();
    u2_.Clear<0>();
    u2_.Clear<1>();
    u3_.Clear<0>();
    u3_.Clear<1>();
  }

  size_t SerializedSize() const {
    size_t size = 0;
    if (x_.IsPresent()) {
      size += x_.SerializedSize();
    }
    if (y_.IsPresent()) {
      size += y_.SerializedSize();
    }
    if (s_.IsPresent()) {
      size += s_.SerializedSize();
    }
    if (m_.IsPresent()) {
      size += m_.SerializedSize();
    }
    size += vi32_.SerializedSize();
    size += vstr_.SerializedSize();
    size += vm_.SerializedSize();
    switch (u1_.Discriminator()) {
    case 107:
      size += u1_.SerializedSize<0>(107);
      break;
    case 108:
      size += u1_.SerializedSize<1>(108);
      break;
    }

    switch (u2_.Discriminator()) {
    case 109:
      size += u2_.SerializedSize<0>(109);
      break;
    case 110:
      size += u2_.SerializedSize<1>(110);
      break;
    }

    switch (u3_.Discriminator()) {
    case 111:
      size += u3_.SerializedSize<0>(111);
      break;
    case 112:
      size += u3_.SerializedSize<1>(112);
      break;
    }
    return size;
  }

  absl::Status Serialize(phaser::ProtoBuffer &buffer) const {
    if (x_.IsPresent()) {
      if (absl::Status status = x_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (y_.IsPresent()) {
      if (absl::Status status = y_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (s_.IsPresent()) {
      if (absl::Status status = s_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    if (m_.IsPresent()) {
      if (absl::Status status = m_.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    // Repeated fields.
    if (absl::Status status = vi32_.Serialize(buffer); !status.ok()) {
      return status;
    }
    if (absl::Status status = vstr_.Serialize(buffer); !status.ok()) {
      return status;
    }
    if (absl::Status status = vm_.Serialize(buffer); !status.ok()) {
      return status;
    }
    // Union fields.
    switch (u1_.Discriminator()) {
    case 107:
      if (absl::Status status = u1_.Serialize<0>(107, buffer); !status.ok()) {
        return status;
      }
      break;
    case 108:
      if (absl::Status status = u1_.Serialize<1>(108, buffer); !status.ok()) {
        return status;
      }
      break;
    }

    // u2.
    switch (u2_.Discriminator()) {
    case 109:
      if (absl::Status status = u2_.Serialize<0>(109, buffer); !status.ok()) {
        return status;
      }
      break;
    case 110:
      if (absl::Status status = u2_.Serialize<1>(110, buffer); !status.ok()) {
        return status;
      }
      break;
    }

    // u3.
    switch (u3_.Discriminator()) {
    case 111:
      if (absl::Status status = u3_.Serialize<0>(111, buffer); !status.ok()) {
        return status;
      }
      break;
    case 112:
      if (absl::Status status = u3_.Serialize<1>(112, buffer); !status.ok()) {
        return status;
      }
      break;
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
      case 100:
        if (absl::Status status = x_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 101:
        if (absl::Status status = y_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 102:
        if (absl::Status status = s_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 103:
        if (absl::Status status = m_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 104:
        if (absl::Status status = vi32_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 105:
        if (absl::Status status = vstr_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 106:
        if (absl::Status status = vm_.Deserialize(buffer); !status.ok()) {
          return status;
        }
        break;
      case 107:
        if (absl::Status status = u1_.Deserialize<0>(107, buffer);
            !status.ok()) {
          return status;
        }
        break;
      case 108:
        if (absl::Status status = u1_.Deserialize<1>(108, buffer);
            !status.ok()) {
          return status;
        }
        break;
      case 109:
        if (absl::Status status = u2_.Deserialize<0>(109, buffer);
            !status.ok()) {
          return status;
        }
        break;
      case 110:
        if (absl::Status status = u2_.Deserialize<1>(110, buffer);
            !status.ok()) {
          return status;
        }
        break;
      case 111:
        if (absl::Status status = u3_.Deserialize<0>(111, buffer);
            !status.ok()) {
          return status;
        }
        break;
      case 112:
        if (absl::Status status = u3_.Deserialize<1>(112, buffer);
            !status.ok()) {
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

  friend std::ostream &operator<<(std::ostream &os, const TestMessage &msg);

  void Indent(int indent) {
    x_.Indent(indent);
    y_.Indent(indent);
    s_.Indent(indent);
    m_.Indent(indent);
    vi32_.Indent(indent);
    vstr_.Indent(indent);
    vm_.Indent(indent);
    u1_.Indent(indent);
    u2_.Indent(indent);
    u3_.Indent(indent);
  }

  // Protobuf access functions.
  uint32_t x() const { return x_.Get(); }
  void set_x(uint32_t x) { x_.Set(x); }
  void clear_x() { x_.Clear(); }
  bool has_x() const { return x_.IsPresent(); }

  uint64_t y() const { return y_.Get(); }
  void set_y(uint64_t y) { y_.Set(y); }
  void clear_y() { y_.Clear(); }
  bool has_y() const { return y_.IsPresent(); }

  std::string_view s() const { return s_.Get(); }
  void set_s(const std::string &s) { s_.Set(s); }
  void clear_s() { s_.Clear(); }
  bool has_s() const { return s_.IsPresent(); }
  absl::Span<char> allocate_s(size_t len) { return s_.Allocate(len); }

  const InnerMessage &m() const { return m_.Get(); }
  InnerMessage *mutable_m() { return m_.Mutable(); }
  void clear_m() { m_.Clear(); }
  bool has_m() const { return m_.IsPresent(); }

  // Repeated accessors.
  size_t vi32_size() const { return vi32_.size(); }
  int32_t vi32(size_t i) const { return vi32_.Get(i); }
  void add_vi32(int32_t v) { vi32_.Add(v); }
  void set_vi32(size_t i, int32_t v) { vi32_.Set(i, v); }
  void clear_vi32() { vi32_.Clear(); }
  phaser::PrimitiveVectorField<int32_t, false, false, true> &vi32() {
    return vi32_;
  }

  size_t vstr_size() const { return vstr_.size(); }
  std::string_view vstr(size_t i) const { return vstr_.Get(i); }
  void add_vstr(const std::string &v) { vstr_.Add(v); }
  void set_vstr(size_t i, const std::string &v) { vstr_.Set(i, v); }
  void clear_vstr() { vstr_.Clear(); }
  phaser::StringVectorField &vstr() {
    vstr_.Populate();
    return vstr_;
  }

  size_t vm_size() const { return vm_.size(); }
  const InnerMessage &vm(size_t i) const { return vm_.Get(i); }
  InnerMessage *mutable_vm(size_t i) { return vm_.Mutable(i); }
  void add_vm() { vm_.Add(); }
  void clear_vm() { vm_.Clear(); }
  phaser::MessageVectorField<InnerMessage> &vm() {
    vm_.Populate();
    return vm_;
  }

  // Union fields.
  int u1_case() const { return u1_.Discriminator(); }

  uint32_t u1a() const { return u1_.GetValue<0, uint32_t>(); }
  void set_u1a(uint32_t v) {
    u1_.Clear<1>();
    u1_.Set<0>(v);
  }
  void clear_u1a() { u1_.Clear<0>(); }

  uint64_t u1b() const { return u1_.GetValue<1, uint64_t>(); }
  void set_u1b(uint64_t v) {
    u1_.Clear<0>();
    u1_.Set<1>(v);
  }
  void clear_u1b() { u1_.Clear<1>(); }

  int u2_case() const { return u2_.Discriminator(); }

  int64_t u2a() const { return u2_.GetValue<0, int64_t>(); }
  void set_u2a(int64_t v) {
    u2_.Clear<1>();
    u2_.Set<0>(v);
  }
  void clear_u2a() { u2_.Clear<0>(); }

  std::string_view u2b() const { return u2_.GetValue<1, std::string_view>(); }
  void set_u2b(const std::string &v) {
    u2_.Clear<0>();
    u2_.Set<1>(v);
  }
  void clear_u2b() { u2_.Clear<1>(); }

  int u3_case() const { return u3_.Discriminator(); }

  int64_t u3a() const { return u3_.GetValue<0, int64_t>(); }
  void set_u3a(int64_t v) {
    u3_.Clear<1>();
    u3_.Set<0>(v);
  }
  void clear_u3a() { u3_.Clear<0>(); }

  const InnerMessage &u3b() const {
    return u3_.GetReference<1, InnerMessage>();
  }
  InnerMessage *mutable_u3b() {
    u3_.Clear<0>();
    return u3_.Mutable<1, InnerMessage>();
  }
  void clear_u3b() { u3_.Clear<1>(); }

  phaser::Uint32Field<false, false> x_;
  phaser::Uint64Field<false, false> y_;
  phaser::StringField s_;
  phaser::IndirectMessageField<InnerMessage> m_;

  // Repeated fields.
  phaser::PrimitiveVectorField<int32_t, false, false, true> vi32_;
  phaser::StringVectorField vstr_;
  phaser::MessageVectorField<InnerMessage> vm_;

  // Union fields.
  phaser::UnionField<phaser::UnionUint32Field<false, false>,
                     phaser::UnionUint64Field<true, false>>
      u1_;
  phaser::UnionField<phaser::UnionInt64Field<false, false>,
                     phaser::UnionStringField>
      u2_;
  phaser::UnionField<phaser::UnionInt64Field<false, false>,
                     phaser::UnionMessageField<InnerMessage>>
      u3_;

  void DebugDump() {
    runtime->pb->Dump(std::cout);
    toolbelt::Hexdump(runtime->pb, runtime->pb->hwm);
  }
};

#pragma clang diagnostic pop

inline std::ostream &operator<<(std::ostream &os, const TestMessage &msg) {
  if (msg.x_.IsPresent()) {
    msg.x_.PrintIndent(os);
    os << "x: " << msg.x_ << std::endl;
  }

  if (msg.y_.IsPresent()) {
    msg.y_.PrintIndent(os);
    os << "y: " << msg.y_ << std::endl;
  }

  if (msg.s_.IsPresent()) {
    msg.s_.PrintIndent(os);
    os << "s: " << msg.s_ << std::endl;
  }

  if (msg.m_.IsPresent()) {
    msg.m_.PrintIndent(os);
    os << "m: " << msg.m_ << std::endl;
  }

  // Repeated Fields.
  for (auto &v : msg.vi32_) {
    msg.vi32_.PrintIndent(os);
    os << "vi32: " << v << std::endl;
  }

  for (auto &v : msg.vstr_) {
    msg.vstr_.PrintIndent(os);
    os << "vstr: " << v << std::endl;
  }

  for (auto &v : msg.vm_) {
    msg.vm_.PrintIndent(os);
    os << "vm: " << v << std::endl;
  }

  // Unions
  switch (msg.u1_.Discriminator()) {
  case 107:
    msg.u1_.PrintIndent(os);
    os << "u1a: ";
    msg.u1_.Print<0>(os);
    os << std::endl;
    break;
  case 108:
    msg.u1_.PrintIndent(os);
    os << "u1b: ";
    msg.u1_.Print<1>(os);
    os << std::endl;
    break;
  }

  switch (msg.u2_.Discriminator()) {
  case 109:
    msg.u2_.PrintIndent(os);
    os << "u2a: ";
    msg.u2_.Print<0>(os);
    os << std::endl;
    break;
  case 110:
    msg.u2_.PrintIndent(os);
    os << "u2b: ";
    msg.u2_.Print<1>(os);
    os << std::endl;
    break;
  }

  switch (msg.u3_.Discriminator()) {
  case 111:
    msg.u3_.PrintIndent(os);
    os << "u3a: ";
    msg.u3_.Print<0>(os);
    os << std::endl;
    break;
  case 112:
    msg.u3_.PrintIndent(os);
    os << "u3b: ";
    msg.u3_.Print<1>(os);
    os << std::endl;
    break;
  }

  return os;
}

TEST(MessageTest, Basic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.x_.Set(1234);
  msg.y_.Set(0xffff);
  msg.s_.Set("Hello, world!");
  auto inner = msg.m_.Mutable();
  inner->str_.Set("Inner message");
  inner->f_.Set(0xdeadbeef);

  msg.DebugDump();

  ASSERT_TRUE(msg.x_.IsPresent());
  uint32_t x = msg.x_.Get();
  ASSERT_EQ(1234, x);

  ASSERT_TRUE(msg.y_.IsPresent());
  uint64_t y = msg.y_.Get();
  ASSERT_EQ(0xffff, y);

  ASSERT_TRUE(msg.s_.IsPresent());
  std::string_view s = msg.s_.Get();
  ASSERT_EQ("Hello, world!", s);

  ASSERT_TRUE(msg.m_.IsPresent());
  auto &inner2 = msg.m_.Get();
  ASSERT_EQ("Inner message", inner2.str_.Get());
  ASSERT_EQ(0xdeadbeef, inner2.f_.Get());

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    ASSERT_TRUE(msg.x_.IsPresent());
    uint32_t x = msg.x_.Get();
    ASSERT_EQ(1234, x);

    ASSERT_TRUE(msg.y_.IsPresent());
    uint64_t y = msg.y_.Get();
    ASSERT_EQ(0xffff, y);

    ASSERT_TRUE(msg.s_.IsPresent());
    std::string_view s = msg.s_.Get();
    ASSERT_EQ("Hello, world!", s);

    ASSERT_TRUE(msg.m_.IsPresent());
    auto &inner2 = msg.m_.Get();
    ASSERT_EQ("Inner message", inner2.str_.Get());
    ASSERT_EQ(0xdeadbeef, inner2.f_.Get());
  }
  free(buffer);
}

TEST(MessageTest, RepeatedPrimitive) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  // This field is absent.
  ASSERT_FALSE(msg.x_.IsPresent());

  msg.vi32_.Add(1);
  msg.vi32_.Add(2);
  msg.DebugDump();

  int i = 1;
  for (auto &v : msg.vi32_) {
    ASSERT_EQ(i, v);
    i++;
  }
  ASSERT_EQ(1, msg.vi32_.Get(0));
  ASSERT_EQ(2, msg.vi32_.Get(1));

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    int i = 1;
    for (auto &v : msg.vi32_) {
      ASSERT_EQ(i, v);
      i++;
    }
    ASSERT_EQ(1, msg.vi32_.Get(0));
    ASSERT_EQ(2, msg.vi32_.Get(1));
  }
  free(buffer);
}

TEST(MessageTest, RepeatedString) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.DebugDump();

  msg.vstr_.Add("one");
  msg.vstr_.Add("two");

  msg.DebugDump();

  const char *strings[] = {"one", "two", "three", "four"};
  int i = 0;
  for (auto &v : msg.vstr_) {
    ASSERT_EQ(strings[i], v.Get());
    i++;
  }

  ASSERT_EQ("one", msg.vstr_.Get(0));
  ASSERT_EQ("two", msg.vstr_.Get(1));

  msg.vstr_.resize(4);
  msg.vstr_.Set(2, "three");
  msg.vstr_.Set(3, "four");

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    int i = 0;
    for (auto &v : msg.vstr_) {
      ASSERT_EQ(strings[i], v.Get());
      i++;
    }
    ASSERT_EQ("one", msg.vstr_.Get(0));
    ASSERT_EQ("two", msg.vstr_.Get(1));
    ASSERT_EQ("three", msg.vstr_.Get(2));
    ASSERT_EQ("four", msg.vstr_.Get(3));
  }
  free(buffer);
}

TEST(MessageTest, RepeatedMessage) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  InnerMessage *inner1 = msg.vm_.Add();
  inner1->str_.Set("one");
  inner1->f_.Set(0xdeadbeef);

  InnerMessage *inner2 = msg.vm_.Mutable(2);
  inner2->str_.Set("two");
  inner2->f_.Set(0x1234);

  msg.DebugDump();

  {
    auto &inner1 = msg.vm_.Get(0);
    ASSERT_EQ("one", inner1.str_.Get());
    ASSERT_EQ(0xdeadbeef, inner1.f_.Get());

    auto &inner2 = msg.vm_.Get(2);
    ASSERT_EQ("two", inner2.str_.Get());
    ASSERT_EQ(0x1234, inner2.f_.Get());
  }

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    auto &inner1 = msg.vm_.Get(0);
    ASSERT_EQ("one", inner1.str_.Get());
    ASSERT_EQ(0xdeadbeef, inner1.f_.Get());

    auto &inner2 = msg.vm_.Get(2);
    ASSERT_EQ("two", inner2.str_.Get());
    ASSERT_EQ(0x1234, inner2.f_.Get());
  }
  free(buffer);
}

TEST(MessageTest, UnionField) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.u1_.Set<0>(1234);
  msg.u2_.Set<1>("Hello, world!");
  InnerMessage *inner = msg.u3_.Mutable<1, InnerMessage>();
  inner->str_.Set("Inner message");
  inner->f_.Set(0xdeadbeef);

  msg.DebugDump();

  ASSERT_EQ(107, msg.u1_.Discriminator());
  ASSERT_EQ(1234, (msg.u1_.GetValue<0, uint32_t>()));
  ASSERT_EQ(110, msg.u2_.Discriminator());
  ASSERT_EQ("Hello, world!", (msg.u2_.GetValue<1, std::string_view>()));

  auto inner2 = msg.u3_.GetReference<1, InnerMessage>();
  ASSERT_EQ("Inner message", inner2.str_.Get());
  ASSERT_EQ(0xdeadbeef, inner2.f_.Get());
  free(buffer);
}

TEST(MessageTest, ClearBasic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.x_.Set(1234);
  msg.y_.Set(0xffff);
  msg.s_.Set("Hello, world!");
  auto inner = msg.m_.Mutable();
  inner->str_.Set("Inner message");
  inner->f_.Set(0xdeadbeef);

  ASSERT_TRUE(msg.x_.IsPresent());
  ASSERT_TRUE(msg.y_.IsPresent());
  ASSERT_TRUE(msg.s_.IsPresent());
  ASSERT_TRUE(msg.m_.IsPresent());

  msg.DebugDump();
  msg.Clear();
  msg.DebugDump();
  ASSERT_FALSE(msg.x_.IsPresent());
  ASSERT_FALSE(msg.y_.IsPresent());
  ASSERT_FALSE(msg.s_.IsPresent());
  ASSERT_FALSE(msg.m_.IsPresent());
  free(buffer);
}

TEST(MessageTest, ClearRepeated) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.vi32_.Add(1);
  msg.vi32_.Add(2);
  msg.vi32_.Add(3);

  msg.vstr_.Add("one");
  msg.vstr_.Add("two");

  auto *inner1 = msg.vm_.Add();
  inner1->str_.Set("one");
  inner1->f_.Set(0xdeadbeef);

  auto *inner2 = msg.vm_.Add();
  inner2->str_.Set("two");
  inner2->f_.Set(0x1234);

  msg.DebugDump();

  msg.Clear();
  msg.DebugDump();

  ASSERT_EQ(0, msg.vi32_.size());
  ASSERT_TRUE(msg.vi32_.empty());

  ASSERT_EQ(0, msg.vstr_.size());
  ASSERT_TRUE(msg.vstr_.empty());

  ASSERT_EQ(0, msg.vm_.size());
  ASSERT_TRUE(msg.vm_.empty());
  free(buffer);
}

TEST(MessageTest, ClearUnion) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.u1_.Set<0>(1234);
  msg.u2_.Set<1>("Hello, world!");
  InnerMessage *inner = msg.u3_.Mutable<1, InnerMessage>();
  inner->str_.Set("Inner message");
  inner->f_.Set(0xdeadbeef);

  msg.DebugDump();
  msg.Clear();
  msg.DebugDump();

  ASSERT_EQ(0, msg.u1_.Discriminator());
  ASSERT_EQ(0, msg.u2_.Discriminator());
  ASSERT_EQ(0, msg.u3_.Discriminator());
  free(buffer);
}

TEST(MessageTest, ProtobufBasic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.set_x(1234);
  msg.set_y(0xffff);
  msg.set_s("Hello, world!");
  auto inner = msg.mutable_m();
  inner->set_str("Inner message");
  inner->set_f(0xdeadbeef);

  // Check the fields.
  ASSERT_TRUE(msg.has_x());
  ASSERT_EQ(1234, msg.x());
  ASSERT_TRUE(msg.has_y());
  ASSERT_EQ(0xffff, msg.y());
  ASSERT_TRUE(msg.has_s());
  ASSERT_EQ("Hello, world!", msg.s());
  ASSERT_TRUE(msg.has_m());
  ASSERT_EQ("Inner message", msg.m().str());
  ASSERT_EQ(0xdeadbeef, msg.m().f());

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    ASSERT_TRUE(msg.has_x());
    ASSERT_EQ(1234, msg.x());
    ASSERT_TRUE(msg.has_y());
    ASSERT_EQ(0xffff, msg.y());
    ASSERT_TRUE(msg.has_s());
    ASSERT_EQ("Hello, world!", msg.s());
    ASSERT_TRUE(msg.has_m());
    ASSERT_EQ("Inner message", msg.m().str());
    ASSERT_EQ(0xdeadbeef, msg.m().f());
  }
  free(buffer);
}

TEST(MessageTest, ProtobufSerializationSizeBasic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.set_x(1); // Tag 100
  ASSERT_EQ(3, msg.x_.SerializedSize());
  msg.set_x(0x80);
  ASSERT_EQ(4, msg.x_.SerializedSize());
  msg.set_x(0x3fff); // Max can fit into 2 bytes for the value.
  ASSERT_EQ(4, msg.x_.SerializedSize());
  msg.set_x(0x4000); // One more than can fit into 2 bytes for the value.
  ASSERT_EQ(5, msg.x_.SerializedSize());

  msg.set_s("Hello, world!");
  ASSERT_EQ(16, msg.s_.SerializedSize());

  auto inner = msg.mutable_m();
  inner->set_str("Inner message");        // Tag 10, size: 15
  inner->set_f(0xdeadbeef);               // Tag 20, size: 10
  ASSERT_EQ(25, inner->SerializedSize()); // 25 bytes

  size_t m_size = msg.m_.SerializedSize();
  size_t expected_size = 5 + 16 + m_size;
  ASSERT_EQ(expected_size, msg.SerializedSize());

  // Create protobuf message to check for the same size.
  foo::bar::TestMessage pb_msg;
  pb_msg.set_x(0x4000);
  pb_msg.set_s("Hello, world!");
  auto *pb_inner = pb_msg.mutable_m();
  pb_inner->set_str("Inner message");
  pb_inner->set_f(0xdeadbeef);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  size_t size = msg.SerializedSize();
  ASSERT_EQ(pb_msg.ByteSizeLong(), size);
  free(buffer);
}

TEST(MessageTest, ProtobufSerializationSizeUnion) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.set_u1a(1234); // Tag 107.
  ASSERT_EQ(4, msg.u1_.SerializedSize<0>(107));
  msg.set_u1b(0xdeadbeef); // Tag 108.
  ASSERT_EQ(10, msg.u1_.SerializedSize<1>(108));

  msg.set_u2a(1234); // Tag 109.
  ASSERT_EQ(4, msg.u2_.SerializedSize<0>(109));
  msg.set_u2b("Hello, world!"); // Tag 110.
  ASSERT_EQ(16, msg.u2_.SerializedSize<1>(110));

  // u3
  auto inner = msg.mutable_u3b();
  inner->set_str("Inner message");        // Tag 10, size: 15
  inner->set_f(0xdeadbeef);               // Tag 20, size: 10
  ASSERT_EQ(25, inner->SerializedSize()); // 25 bytes

  size_t m_size = msg.u3_.SerializedSize<1>(112);
  size_t expected_size = 10 + 16 + m_size;
  ASSERT_EQ(expected_size, msg.SerializedSize());

  // Create protobuf message to check for the same size.
  foo::bar::TestMessage pb_msg;
  pb_msg.set_u1b(0xdeadbeef);
  pb_msg.set_u2b("Hello, world!");
  auto *pb_inner = pb_msg.mutable_u3b();
  pb_inner->set_str("Inner message");
  pb_inner->set_f(0xdeadbeef);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  size_t size = msg.SerializedSize();
  ASSERT_EQ(pb_msg.ByteSizeLong(), size);
  free(buffer);
}

TEST(MessageTest, ProtobufSerializationSizeRepeatedPrimitive) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  // Tag 104.
  msg.add_vi32(1);
  msg.add_vi32(2);

  // Tag 105.
  msg.add_vstr("one");
  msg.add_vstr("two");

  size_t expected_vi32_size =
      phaser::ProtoBuffer::TagSize(104, phaser::WireType::kLengthDelimited) +
      1 + 2;

  ASSERT_EQ(expected_vi32_size, msg.vi32_.SerializedSize());

  size_t expected_vstr_size =
      phaser::ProtoBuffer::LengthDelimitedSize(105, 3) * 2;
  ASSERT_EQ(expected_vstr_size, msg.vstr_.SerializedSize());

  // Create protobuf message to check for the same size.
  foo::bar::TestMessage pb_msg;
  pb_msg.add_vi32(1);
  pb_msg.add_vi32(2);

  pb_msg.add_vstr("one");
  pb_msg.add_vstr("two");

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  size_t size = msg.SerializedSize();
  ASSERT_EQ(pb_msg.ByteSizeLong(), size);
  free(buffer);
}

TEST(MessageTest, ProtobufSerializationBasic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  phaser::ProtoBuffer pb;
  msg.set_x(1);
  msg.set_y(0xffff);
  msg.set_s("Hello, world!");
  auto inner = msg.mutable_m();
  inner->set_str("Inner message");
  inner->set_f(0xdeadbeef);

  ASSERT_TRUE(msg.Serialize(pb).ok());
  toolbelt::Hexdump(pb.data(), pb.size());

  foo::bar::TestMessage pb_msg;
  pb_msg.set_x(1);
  pb_msg.set_y(0xffff);
  pb_msg.set_s("Hello, world!");
  auto *pb_inner = pb_msg.mutable_m();
  pb_inner->set_str("Inner message");
  pb_inner->set_f(0xdeadbeef);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  std::string pb_str(pb.data(), pb.size());
  ASSERT_EQ(str, pb_str);
  free(buffer);
}

TEST(MessageTest, ProtobufSerializationRepeated) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  phaser::ProtoBuffer pb;
  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(200);
  msg.add_vi32(201);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  auto *inner1 = msg.vm_.Add();
  inner1->set_str("one");
  inner1->set_f(0xdeadbeef);

  auto *inner2 = msg.vm_.Add();
  inner2->set_str("two");
  inner2->set_f(0x1234);

  ASSERT_TRUE(msg.Serialize(pb).ok());
  toolbelt::Hexdump(pb.data(), pb.size());

  foo::bar::TestMessage pb_msg;
  pb_msg.add_vi32(1);
  pb_msg.add_vi32(2);
  pb_msg.add_vi32(200);
  pb_msg.add_vi32(201);

  pb_msg.add_vstr("one");
  pb_msg.add_vstr("two");
  pb_msg.add_vstr("three");

  auto *pb_inner1 = pb_msg.add_vm();
  pb_inner1->set_str("one");
  pb_inner1->set_f(0xdeadbeef);

  auto *pb_inner2 = pb_msg.add_vm();
  pb_inner2->set_str("two");
  pb_inner2->set_f(0x1234);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  std::string pb_str(pb.data(), pb.size());
  ASSERT_EQ(str, pb_str);
  free(buffer);
}

TEST(MessageTest, ProtobufSerializationUnion) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  phaser::ProtoBuffer pb;
  msg.set_u1a(1234);
  msg.set_u2b("Hello, world!");

  auto inner = msg.mutable_u3b();
  inner->set_str("Inner message");
  inner->set_f(0xdeadbeef);

  ASSERT_TRUE(msg.Serialize(pb).ok());
  toolbelt::Hexdump(pb.data(), pb.size());

  foo::bar::TestMessage pb_msg;
  pb_msg.set_u1a(1234);
  pb_msg.set_u2b("Hello, world!");

  auto *pb_inner = pb_msg.mutable_u3b();
  pb_inner->set_str("Inner message");
  pb_inner->set_f(0xdeadbeef);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  std::string pb_str(pb.data(), pb.size());
  ASSERT_EQ(str, pb_str);
  free(buffer);
}

TEST(MessageTest, ProtobufDeserializationBasic) {
  foo::bar::TestMessage pb_msg;

  pb_msg.set_x(1);
  pb_msg.set_y(0xffff);
  pb_msg.set_s("Hello, world!");

  auto *pb_inner = pb_msg.mutable_m();
  pb_inner->set_str("Inner message");
  pb_inner->set_f(0xdeadbeef);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);
  phaser::ProtoBuffer pb_buffer(str);

  auto status = msg.Deserialize(pb_buffer);
  ASSERT_TRUE(status.ok());

  // Serialize it again to a new buffer and check it matches the original.
  phaser::ProtoBuffer pb;
  status = msg.Serialize(pb);
  ASSERT_TRUE(status.ok());

  toolbelt::Hexdump(pb.data(), pb.size());
  ASSERT_EQ(str, std::string(pb.data(), pb.size()));
  free(buffer);
}

TEST(MessageTest, ProtobufDeserializationRepeated) {
  foo::bar::TestMessage pb_msg;

  pb_msg.add_vi32(1);
  pb_msg.add_vi32(2);
  pb_msg.add_vi32(200);
  pb_msg.add_vi32(201);

  pb_msg.add_vstr("one");
  pb_msg.add_vstr("two");
  pb_msg.add_vstr("three");

  auto *pb_inner1 = pb_msg.add_vm();
  pb_inner1->set_str("one");
  pb_inner1->set_f(0xdeadbeef);

  auto *pb_inner2 = pb_msg.add_vm();
  pb_inner2->set_str("two");
  pb_inner2->set_f(0x1234);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);
  phaser::ProtoBuffer pb_buffer(str);

  auto status = msg.Deserialize(pb_buffer);
  std::cout << status << std::endl;
  ASSERT_TRUE(status.ok());

  // Serialize it again to a new buffer and check it matches the original.
  phaser::ProtoBuffer pb;
  status = msg.Serialize(pb);
  ASSERT_TRUE(status.ok());

  toolbelt::Hexdump(pb.data(), pb.size());
  ASSERT_EQ(str, std::string(pb.data(), pb.size()));
  free(buffer);
}

TEST(MessageTest, ProtobufDeserializationUnion) {
  foo::bar::TestMessage pb_msg;

  pb_msg.set_u1a(1234);
  pb_msg.set_u2b("Hello, world!");

  auto *pb_inner = pb_msg.mutable_u3b();
  pb_inner->set_str("Inner message");
  pb_inner->set_f(0xdeadbeef);

  std::string str = pb_msg.SerializeAsString();
  toolbelt::Hexdump(str.data(), str.size());

  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);
  phaser::ProtoBuffer pb_buffer(str);

  auto status = msg.Deserialize(pb_buffer);
  ASSERT_TRUE(status.ok());

  // Serialize it again to a new buffer and check it matches the original.
  phaser::ProtoBuffer pb;
  status = msg.Serialize(pb);
  ASSERT_TRUE(status.ok());

  toolbelt::Hexdump(pb.data(), pb.size());
  ASSERT_EQ(str, std::string(pb.data(), pb.size()));
  free(buffer);
}

TEST(MessageTest, Dynamic) {
  TestMessage msg;
  msg.x_.Set(1234);
  msg.y_.Set(0xffff);
  msg.s_.Set("Hello, world!");
  auto inner = msg.m_.Mutable();
  inner->str_.Set("Inner message");
  inner->f_.Set(0xdeadbeef);

  ASSERT_TRUE(msg.x_.IsPresent());
  uint32_t x = msg.x_.Get();
  ASSERT_EQ(1234, x);

  ASSERT_TRUE(msg.y_.IsPresent());
  uint64_t y = msg.y_.Get();
  ASSERT_EQ(0xffff, y);

  ASSERT_TRUE(msg.s_.IsPresent());
  std::string_view s = msg.s_.Get();
  ASSERT_EQ("Hello, world!", s);
}

TEST(MessageTest, Print) {
  TestMessage msg;
  msg.x_.Set(1234);
  msg.y_.Set(0xffff);
  msg.s_.Set("Hello, world!");
  auto inner = msg.m_.Mutable();
  inner->str_.Set("Inner message");
  inner->f_.Set(0xdeadbeef);
  inner->e_.Set(EnumTest::BAR);
  inner->ev_.Add(EnumTest::FOO);
  inner->ev_.Add(EnumTest::BAR);
  inner->uv_.Set<0>(1234);

  msg.vi32_.Add(1);
  msg.vi32_.Add(2);
  msg.vi32_.Add(3);

  msg.vstr_.Add("one");
  msg.vstr_.Add("two");

  auto *inner1 = msg.vm_.Add();
  inner1->str_.Set("one");
  inner1->f_.Set(0xdeadbeef);

  auto *inner2 = msg.vm_.Add();
  inner2->str_.Set("two");
  inner2->f_.Set(0x1234);

  // Unions.
  msg.u1_.Set<0>(1234);
  msg.u2_.Set<1>("Hello, world!");
  InnerMessage *inner3 = msg.u3_.Mutable<1, InnerMessage>();
  inner3->str_.Set("Inner message");
  inner3->f_.Set(0xdeadbeef);

  std::cout << msg << std::endl;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
