#include "absl/strings/str_format.h"
#include "phaser/runtime/payload_buffer.h"
#include "phaser/runtime/runtime.h"
#include "toolbelt/hexdump.h"
#include <gtest/gtest.h>
#include <sstream>

using PayloadBuffer = phaser::PayloadBuffer;
using BufferOffset = phaser::BufferOffset;
using Message = phaser::Message;
using VectorHeader = phaser::VectorHeader;
using StringHeader = phaser::StringHeader;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

enum class EnumTest : uint16_t { FOO = 0xda, BAR = 0xad };

struct InnerMessage : public Message {
  InnerMessage()
      : str_(offsetof(InnerMessage, str_), HeaderSize() + 0, 0, 10),
        f_(offsetof(InnerMessage, f_), HeaderSize() + 8, 1, 20) {}
  // explicit InnerMessage(std::shared_ptr<phaser::MessageRuntime> runtime)
  //     : str_(offsetof(InnerMessage, str_), HeaderSize() + 0, 0, 10),
  //       f_(offsetof(InnerMessage, f_), HeaderSize() + 8, 1, 20) {
  //   this->runtime = runtime;
  //   void *data = phaser::PayloadBuffer::Allocate(&runtime->pb, BinarySize(), 8);
  //   this->absolute_binary_offset = runtime->pb->ToOffset(data);

  //   std::cout << "InnerMessage start: " << std::hex
  //             << this->absolute_binary_offset << std::dec << std::endl;
  // }
  InnerMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
               phaser::BufferOffset offset)
      : Message(runtime, offset),
        str_(offsetof(InnerMessage, str_), HeaderSize() + 0, 0, 10),
        f_(offsetof(InnerMessage, f_), HeaderSize() + 8, 1, 20) {}
  static constexpr size_t BinarySize() { return HeaderSize() + 16; }
  static constexpr size_t PresenceMaskSize() { return 4; }
  static constexpr uint32_t HeaderSize() { return 4 + PresenceMaskSize(); }
  struct FieldData {
    uint32_t num;
    struct Field {
      uint32_t number;
      uint32_t offset : 24;
      uint32_t id : 8;
    } fields[12];
  };
  static constexpr FieldData field_data = {
      .num = 2,
      .fields = {
          {.number = 10, .offset = 8, .id = 0},
          {.number = 20, .offset = 16, .id = 1},
      }};
  static std::string GetName() { return "InnerMessage"; }

  // Protobuf accessors.
  std::string_view str() const { return str_.Get(); }
  void set_str(const std::string &str) { str_.Set(str); }
  void clear_str() { str_.Clear(); }
  bool has_str() const { return str_.IsPresent(); }

  uint64_t f() const { return f_.Get(); }
  void set_f(uint64_t f) { f_.Set(f); }
  void clear_f() { f_.Clear(); }
  bool has_f() const { return f_.IsPresent(); }

  void Clear() {
    str_.Clear();
    f_.Clear();
  }
  phaser::StringField str_;
  phaser::Uint64Field f_;
};

struct TestMessage : public Message {
  TestMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
              phaser::BufferOffset offset)
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
    phaser::PayloadBuffer *pb = new (addr) phaser::PayloadBuffer(size);
    phaser::PayloadBuffer::AllocateMainMessage(&pb, TestMessage::BinarySize());
    auto runtime = std::make_shared<phaser::MutableMessageRuntime>(pb);
    auto msg = TestMessage(runtime, pb->message);
    msg.InstallMetadata<TestMessage>();
    return msg;
  }

  static TestMessage CreateReadonly(void *addr) {
    phaser::PayloadBuffer *pb = reinterpret_cast<phaser::PayloadBuffer *>(addr);
    auto runtime = std::make_shared<phaser::MessageRuntime>(pb);
    return TestMessage(runtime, pb->message);
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
  phaser::PrimitiveVectorField<int32_t> &vi32() { return vi32_; }

  size_t vstr_size() const { return vstr_.size(); }
  std::string_view vstr(size_t i) const { return vstr_.Get(i); }
  void add_vstr(const std::string &v) { vstr_.Add(v); }
  void set_vstr(size_t i, const std::string &v) { vstr_.Set(i, v); }
  void clear_vstr() { vstr_.Clear(); }
  phaser::StringVectorField &vstr() { return vstr_; }

  size_t vm_size() const { return vm_.size(); }
  const InnerMessage &vm(size_t i) const { return vm_.Get(i); }
  InnerMessage *mutable_vm(size_t i) { return vm_.Mutable(i); }
  void add_vm() { vm_.Add(); }
  void clear_vm() { vm_.Clear(); }
  phaser::MessageVectorField<InnerMessage> &vm() { return vm_; }

  // Union fields.
  int u1_case() const { return u1_.Discriminator(); }

  uint32_t u1a() const { return u1_.GetValue<0, uint32_t>(); }
  void set_u1a(uint32_t v) { u1_.Set<0>(v); }
  void clear_u1a() { u1_.Clear<0>(); }

  uint64_t u1b() const { return u1_.GetValue<1, uint64_t>(); }
  void set_u1b(uint64_t v) { u1_.Set<1>(v); }
  void clear_u1b() { u1_.Clear<1>(); }

  int u2_case() const { return u2_.Discriminator(); }

  int64_t u2a() const { return u2_.GetValue<0, int64_t>(); }
  void set_u2a(int64_t v) { u2_.Set<0>(v); }
  void clear_u2a() { u2_.Clear<0>(); }

  std::string_view u2b() const { return u2_.GetValue<1, std::string_view>(); }
  void set_u2b(const std::string &v) { u2_.Set<1>(v); }
  void clear_u2b() { u2_.Clear<1>(); }

  int u3_case() const { return u3_.Discriminator(); }

  int64_t u3a() const { return u3_.GetValue<0, int64_t>(); }
  void set_u3a(int64_t v) { u3_.Set<0>(v); }
  void clear_u3a() { u3_.Clear<0>(); }

  const InnerMessage &u3b() const {
    return u3_.GetReference<1, InnerMessage>();
  }
  InnerMessage *mutable_u3b() { return u3_.Mutable<1, InnerMessage>(); }
  void clear_u3b() { u3_.Clear<1>(); }

  phaser::Uint32Field x_;
  phaser::Uint64Field y_;
  phaser::StringField s_;
  phaser::IndirectMessageField<InnerMessage> m_;

  // Repeated fields.
  phaser::PrimitiveVectorField<int32_t> vi32_;
  phaser::StringVectorField vstr_;
  phaser::MessageVectorField<InnerMessage> vm_;

  // Union fields.
  phaser::UnionField<phaser::UnionUint32Field, phaser::UnionUint64Field> u1_;
  phaser::UnionField<phaser::UnionInt64Field, phaser::UnionStringField> u2_;
  phaser::UnionField<phaser::UnionInt64Field,
                     phaser::UnionMessageField<InnerMessage>>
      u3_;

  void DebugDump() {
    runtime->pb->Dump(std::cout);
    toolbelt::Hexdump(runtime->pb, runtime->pb->hwm);
  }
};

#pragma clang diagnostic pop

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
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
