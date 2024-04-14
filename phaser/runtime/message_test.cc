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
      : str(offsetof(InnerMessage, str), HeaderSize() + 0, 0, 10),
        f(offsetof(InnerMessage, f), HeaderSize() + 8, 1, 20) {}
  explicit InnerMessage(std::shared_ptr<phaser::MessageRuntime> runtime)
      : str(offsetof(InnerMessage, str), HeaderSize() + 0, 0, 10),
        f(offsetof(InnerMessage, f), HeaderSize() + 8, 1, 20) {

    this->runtime = runtime;
    void *data = phaser::PayloadBuffer::Allocate(&runtime->pb, BinarySize(), 8);
    this->absolute_binary_offset = runtime->pb->ToOffset(data);

    std::cout << "InnerMessage start: " << std::hex
              << this->absolute_binary_offset << std::dec << std::endl;
  }
  InnerMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
               phaser::BufferOffset offset)
      : Message(runtime, offset),
        str(offsetof(InnerMessage, str), HeaderSize() + 0, 0, 10),
        f(offsetof(InnerMessage, f), HeaderSize() + 8, 1, 20) {}
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

  void Clear() {
    str.Clear();
    f.Clear();
  }
  phaser::StringField str;
  phaser::Uint64Field f;
};

struct TestMessage : public Message {
  TestMessage(std::shared_ptr<phaser::MessageRuntime> runtime,
              phaser::BufferOffset offset)
      : Message(runtime, offset),
        x(offsetof(TestMessage, x), HeaderSize() + 0, 0, 100),
        y(offsetof(TestMessage, y), HeaderSize() + 8, 1, 101),
        s(offsetof(TestMessage, s), HeaderSize() + 16, 0, 102),
        m(offsetof(TestMessage, m), HeaderSize() + 20, 0, 103),
        vi32(offsetof(TestMessage, vi32), HeaderSize() + 24, 0, 104),
        vstr(offsetof(TestMessage, vstr), HeaderSize() + 32, 0, 105),
        vm(offsetof(TestMessage, vm), HeaderSize() + 40, 0, 106),
        u1(offsetof(TestMessage, u1), HeaderSize() + 48, 0, 0, {107, 108}),
        u2(offsetof(TestMessage, u2), HeaderSize() + 56, 0, 0, {109, 110}),
        u3(offsetof(TestMessage, u3), HeaderSize() + 64, 0, 0, {111, 112}) {}

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
    x.Clear();
    y.Clear();
    s.Clear();
    m.Clear();
    vi32.Clear();
    vstr.Clear();
    vm.Clear();
    u1.Clear<0>();
    u1.Clear<1>();
    u2.Clear<0>();
    u2.Clear<1>();
    u3.Clear<0>();
    u3.Clear<1>();
  }
  phaser::Uint32Field x;
  phaser::Uint64Field y;
  phaser::StringField s;
  phaser::IndirectMessageField<InnerMessage> m;

  // Repeated fields.
  phaser::PrimitiveVectorField<int32_t> vi32;
  phaser::StringVectorField vstr;
  phaser::MessageVectorField<InnerMessage> vm;

  // Union fields.
  phaser::UnionField<phaser::UnionUint32Field, phaser::UnionUint64Field> u1;
  phaser::UnionField<phaser::UnionInt64Field, phaser::UnionStringField> u2;
  phaser::UnionField<phaser::UnionInt64Field,
                     phaser::UnionMessageField<InnerMessage>>
      u3;
  void DebugDump() {
    runtime->pb->Dump(std::cout);
    toolbelt::Hexdump(runtime->pb, runtime->pb->hwm);
  }
};

#pragma clang diagnostic pop

TEST(MessageTest, Basic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.x.Set(1234);
  msg.y.Set(0xffff);
  msg.s.Set("Hello, world!");
  auto inner = msg.m.Mutable();
  inner->str.Set("Inner message");
  inner->f.Set(0xdeadbeef);

  msg.DebugDump();

  ASSERT_TRUE(msg.x.IsPresent());
  uint32_t x = msg.x.Get();
  ASSERT_EQ(1234, x);

  ASSERT_TRUE(msg.y.IsPresent());
  uint64_t y = msg.y.Get();
  ASSERT_EQ(0xffff, y);

  ASSERT_TRUE(msg.s.IsPresent());
  std::string_view s = msg.s.Get();
  ASSERT_EQ("Hello, world!", s);

  ASSERT_TRUE(msg.m.IsPresent());
  auto &inner2 = msg.m.Get();
  ASSERT_EQ("Inner message", inner2.str.Get());
  ASSERT_EQ(0xdeadbeef, inner2.f.Get());

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    ASSERT_TRUE(msg.x.IsPresent());
    uint32_t x = msg.x.Get();
    ASSERT_EQ(1234, x);

    ASSERT_TRUE(msg.y.IsPresent());
    uint64_t y = msg.y.Get();
    ASSERT_EQ(0xffff, y);

    ASSERT_TRUE(msg.s.IsPresent());
    std::string_view s = msg.s.Get();
    ASSERT_EQ("Hello, world!", s);

    ASSERT_TRUE(msg.m.IsPresent());
    auto &inner2 = msg.m.Get();
    ASSERT_EQ("Inner message", inner2.str.Get());
    ASSERT_EQ(0xdeadbeef, inner2.f.Get());
  }
}

TEST(MessageTest, RepeatedPrimitive) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  // This field is absent.
  ASSERT_FALSE(msg.x.IsPresent());

  msg.vi32.Add(1);
  msg.vi32.Add(2);
  msg.DebugDump();

  int i = 1;
  for (auto &v : msg.vi32) {
    ASSERT_EQ(i, v);
    i++;
  }
  ASSERT_EQ(1, msg.vi32.Get(0));
  ASSERT_EQ(2, msg.vi32.Get(1));

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    int i = 1;
    for (auto &v : msg.vi32) {
      ASSERT_EQ(i, v);
      i++;
    }
    ASSERT_EQ(1, msg.vi32.Get(0));
    ASSERT_EQ(2, msg.vi32.Get(1));
  }
}

TEST(MessageTest, RepeatedString) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.DebugDump();

  msg.vstr.Add("one");
  msg.vstr.Add("two");

  msg.DebugDump();

  const char *strings[] = {"one", "two", "three", "four"};
  int i = 0;
  for (auto &v : msg.vstr) {
    ASSERT_EQ(strings[i], v.Get());
    i++;
  }

  ASSERT_EQ("one", msg.vstr.Get(0));
  ASSERT_EQ("two", msg.vstr.Get(1));

  msg.vstr.resize(4);
  msg.vstr.Set(2, "three");
  msg.vstr.Set(3, "four");

  // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    int i = 0;
    for (auto &v : msg.vstr) {
      ASSERT_EQ(strings[i], v.Get());
      i++;
    }
    ASSERT_EQ("one", msg.vstr.Get(0));
    ASSERT_EQ("two", msg.vstr.Get(1));
    ASSERT_EQ("three", msg.vstr.Get(2));
    ASSERT_EQ("four", msg.vstr.Get(3));
  }
}

TEST(MessageTest, RepeatedMessage) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  InnerMessage *inner1 = msg.vm.Add();
  inner1->str.Set("one");
  inner1->f.Set(0xdeadbeef);

  InnerMessage *inner2 = msg.vm.Mutable(2);
  inner2->str.Set("two");
  inner2->f.Set(0x1234);

  msg.DebugDump();

  {
    auto &inner1 = msg.vm.Get(0);
    ASSERT_EQ("one", inner1.str.Get());
    ASSERT_EQ(0xdeadbeef, inner1.f.Get());

    auto &inner2 = msg.vm.Get(2);
    ASSERT_EQ("two", inner2.str.Get());
    ASSERT_EQ(0x1234, inner2.f.Get());
  }

    // Copy message to test reading.
  {
    char *buffer2 = (char *)malloc(4096);
    memcpy(buffer2, buffer, 4096);
    TestMessage msg = TestMessage::CreateReadonly(buffer2);

    auto &inner1 = msg.vm.Get(0);
    ASSERT_EQ("one", inner1.str.Get());
    ASSERT_EQ(0xdeadbeef, inner1.f.Get());

    auto &inner2 = msg.vm.Get(2);
    ASSERT_EQ("two", inner2.str.Get());
    ASSERT_EQ(0x1234, inner2.f.Get());
  }
}

TEST(MessageTest, UnionField) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.u1.Set<0>(1234);
  msg.u2.Set<1>("Hello, world!");
  InnerMessage *inner = msg.u3.Mutable<1, InnerMessage>();
  inner->str.Set("Inner message");
  inner->f.Set(0xdeadbeef);

  msg.DebugDump();

  ASSERT_EQ(107, msg.u1.Discriminator());
  ASSERT_EQ(1234, (msg.u1.GetValue<0, uint32_t>()));
  ASSERT_EQ(110, msg.u2.Discriminator());
  ASSERT_EQ("Hello, world!", (msg.u2.GetValue<1, std::string_view>()));

  auto inner2 = msg.u3.GetReference<1, InnerMessage>();
  ASSERT_EQ("Inner message", inner2.str.Get());
  ASSERT_EQ(0xdeadbeef, inner2.f.Get());
}

TEST(MessageTest, ClearBasic) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.x.Set(1234);
  msg.y.Set(0xffff);
  msg.s.Set("Hello, world!");
  auto inner = msg.m.Mutable();
  inner->str.Set("Inner message");
  inner->f.Set(0xdeadbeef);

  ASSERT_TRUE(msg.x.IsPresent());
  ASSERT_TRUE(msg.y.IsPresent());
  ASSERT_TRUE(msg.s.IsPresent());
  ASSERT_TRUE(msg.m.IsPresent());

  msg.DebugDump();
  msg.Clear();
  msg.DebugDump();
  ASSERT_FALSE(msg.x.IsPresent());
  ASSERT_FALSE(msg.y.IsPresent());
  ASSERT_FALSE(msg.s.IsPresent());
  ASSERT_FALSE(msg.m.IsPresent());
}

TEST(MessageTest, ClearRepeated) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.vi32.Add(1);
  msg.vi32.Add(2);
  msg.vi32.Add(3);
  
  msg.vstr.Add("one");
  msg.vstr.Add("two");

  auto* inner1 = msg.vm.Add();
  inner1->str.Set("one");
  inner1->f.Set(0xdeadbeef);

  auto* inner2 = msg.vm.Add();
  inner2->str.Set("two");
  inner2->f.Set(0x1234);

  msg.DebugDump();

  msg.Clear();
  msg.DebugDump();

  ASSERT_EQ(0, msg.vi32.size());
  ASSERT_TRUE(msg.vi32.empty());

  ASSERT_EQ(0, msg.vstr.size());
  ASSERT_TRUE(msg.vstr.empty());

  ASSERT_EQ(0, msg.vm.size());
  ASSERT_TRUE(msg.vm.empty());
}

TEST(MessageTest, ClearUnion) {
  char *buffer = (char *)malloc(4096);
  TestMessage msg = TestMessage::CreateMutable(buffer, 4096);

  msg.u1.Set<0>(1234);
  msg.u2.Set<1>("Hello, world!");
  InnerMessage *inner = msg.u3.Mutable<1, InnerMessage>();
  inner->str.Set("Inner message");
  inner->f.Set(0xdeadbeef);

  msg.DebugDump();
  msg.Clear();
  msg.DebugDump();

  ASSERT_EQ(0, msg.u1.Discriminator());
  ASSERT_EQ(0, msg.u2.Discriminator());
  ASSERT_EQ(0, msg.u3.Discriminator());
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
