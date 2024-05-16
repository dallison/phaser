
#include "absl/strings/str_format.h"
#include "toolbelt/payload_buffer.h"
#include "phaser/runtime/runtime.h"
#include "phaser/testdata/TestMessage.pb.h"
#include "phaser/testdata/TestMessage.phaser.h"
#include "toolbelt/hexdump.h"
#include <gtest/gtest.h>
#include <sstream>

TEST(PhaserTest, ProtobufCompat) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto m = msg.mutable_m();
  m->set_str("Inner message");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  auto inner = msg.mutable_m();
  inner->set_str("Inner message");

  msg.set_u1b(4321);
  msg.set_u2b("u2b");

  auto u3 = msg.mutable_u3b();
  u3->set_str("u3b");

  std::string buffer_data;
  for (int i = 0; i < 100; i++) {
    buffer_data.push_back(rand() & 0xff);
  }
  buffer_data.push_back('\n');
  buffer_data.push_back('\r');

  buffer_data.push_back('\v');
  buffer_data.push_back('\t');

  msg.set_buffer(buffer_data);

  msg.set_e(foo::bar::phaser::FOO);

  std::cout << msg;

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());
  ASSERT_EQ("Inner message", msg.m().str());

  // Check enum printing and parsing functions.
  ASSERT_EQ(foo::bar::phaser::FOO, msg.e());
  ASSERT_EQ("FOO", foo::bar::phaser::EnumTest_Name(msg.e()));

  foo::bar::phaser::EnumTest ee;
  foo::bar::phaser::EnumTest_Parse("BAR", &ee);
  ASSERT_EQ(foo::bar::phaser::BAR, ee);

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Serialize to a string.
  std::string serialized;
  msg.SerializeToString(&serialized);

  // Deserialize from a string into a protobuf message.
  foo::bar::TestMessage msg2;
  msg2.ParseFromString(serialized);
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());
  ASSERT_EQ(msg2.m().str(), msg.m().str());

  std::string pb_debug_string = msg2.DebugString();
  std::string phaser_debug_string = msg.DebugString();
  ASSERT_EQ(pb_debug_string, phaser_debug_string);
}

TEST(PhaserTest, NewFieldsBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Create a TestMessageNewFields using the binary from TestMessage.
  auto msg2 = foo::bar::phaser::TestMessageNewFields::CreateReadonly(
      msg.Data(), msg.Size());
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());

  // Check that the new fields are not present.
  ASSERT_FALSE(msg2.has_new1());
  ASSERT_FALSE(msg2.has_new2());
  ASSERT_EQ(0, msg2.new3_size());
}

TEST(PhaserTest, NewFieldsRepeatedBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Create a TestMessageNewFields using the binary from TestMessage.
  auto msg2 = foo::bar::phaser::TestMessageNewFields::CreateReadonly(
      msg.Data(), msg.Size());
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());

  ASSERT_EQ(3, msg2.vi32_size());
  ASSERT_EQ(3, msg2.vstr_size());
  ASSERT_EQ(msg2.vi32(0), msg.vi32(0));
  ASSERT_EQ(msg2.vi32(1), msg.vi32(1));
  ASSERT_EQ(msg2.vi32(2), msg.vi32(2));
  ASSERT_EQ(msg2.vstr(0), msg.vstr(0));
  ASSERT_EQ(msg2.vstr(1), msg.vstr(1));
  ASSERT_EQ(msg2.vstr(2), msg.vstr(2));
}

TEST(PhaserTest, DeletedFieldsBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Create a TestMessageDeletedFields using the binary from TestMessage.
  auto msg2 = foo::bar::phaser::TestMessageDeletedFields::CreateReadonly(
      msg.Data(), msg.Size());
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.s(), msg.s());
}

TEST(PhaserTest, CopySimple) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto m = msg.mutable_m();
  m->set_str("Inner message");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  msg.set_u1a(4321);
  msg.set_u2a(8765);

  auto u3 = msg.mutable_u3b();
  u3->set_str("u3b");
  msg.runtime->pb->Dump(std::cout);
  toolbelt::Hexdump(msg.runtime->pb, msg.runtime->pb->Size());

  // Copy to phaser.
  foo::bar::phaser::TestMessage msg2;
  ASSERT_TRUE(msg2.CloneFrom(msg).ok());

  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());
  ASSERT_EQ(msg2.m().str(), msg.m().str());
  ASSERT_EQ(msg2.vi32_size(), msg.vi32_size());
  ASSERT_EQ(msg2.vstr_size(), msg.vstr_size());
  ASSERT_EQ(msg2.vi32(0), msg.vi32(0));
  ASSERT_EQ(msg2.vi32(1), msg.vi32(1));
  ASSERT_EQ(msg2.vi32(2), msg.vi32(2));
  ASSERT_EQ(msg2.vstr(0), msg.vstr(0));
  ASSERT_EQ(msg2.vstr(1), msg.vstr(1));
  ASSERT_EQ(msg2.vstr(2), msg.vstr(2));

  ASSERT_EQ(msg2.u1a(), msg.u1a());
  ASSERT_EQ(msg2.u2a(), msg.u2a());
  ASSERT_EQ(msg2.u3b().str(), msg.u3b().str());
}

TEST(PhaserTest, Any) {
  foo::bar::phaser::TestMessage msg;
  foo::bar::phaser::InnerMessage inner;
  inner.set_str("Any message is inner");

  auto any = msg.mutable_any();

  ASSERT_TRUE(any->PackFrom(inner));

  foo::bar::phaser::TestMessage msg2;
  ASSERT_TRUE(msg2.CloneFrom(msg).ok());
  foo::bar::phaser::InnerMessage inner2;
  ASSERT_TRUE(msg2.any().Is<foo::bar::phaser::InnerMessage>());
  ASSERT_TRUE(msg2.any().UnpackTo(&inner2));
  ASSERT_EQ(inner.str(), inner2.str());

  foo::bar::phaser::TestMessage msg3;
  foo::bar::phaser::InnerMessage inner3 =
      msg3.mutable_any()->MutableAny<foo::bar::phaser::InnerMessage>();
  inner3.set_str("Any message is inner3");
  ASSERT_EQ(inner3.str(),
            msg3.any().As<foo::bar::phaser::InnerMessage>().str());

  std::cout << msg3;

  foo::bar::TestMessage msg4;
  foo::bar::InnerMessage inner4;
  inner4.set_str("Any message is inner");

  auto any4 = msg4.mutable_any();
  ASSERT_TRUE(any4->PackFrom(inner4));

  std::string pb_debug_string = msg4.DebugString();
  std::string phaser_debug_string = msg.DebugString();
  ASSERT_EQ(pb_debug_string, phaser_debug_string);
}

TEST(PhaserTest, Garbage) {
  char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = rand() & 0xff;
  }
  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, GarbageSmall) {
  char buffer[10];
  for (int i = 0; i < 10; i++) {
    buffer[i] = rand() & 0xff;
  }
  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, GarbageLoop) {
  char buffer[256];
  for (int i = 0; i < 1000; i++) {
    for (int i = 0; i < 256; i++) {
      buffer[i] = rand() & 0xff;
    }
    auto msg =
        foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
    ASSERT_FALSE(msg.has_x());
    ASSERT_EQ(0, msg.x());
    ASSERT_FALSE(msg.has_y());
    ASSERT_EQ(0, msg.y());

    ASSERT_FALSE(msg.has_s());
    ASSERT_EQ("", msg.s());

    ASSERT_FALSE(msg.has_m());
    ASSERT_EQ("", msg.m().str());

    ASSERT_EQ(0, msg.vi32_size());
    ASSERT_EQ(0, msg.vstr_size());

    ASSERT_FALSE(msg.has_u1a());
    ASSERT_EQ(0, msg.u1a());
    ASSERT_FALSE(msg.has_u2a());
    ASSERT_EQ(0, msg.u2a());
    ASSERT_FALSE(msg.has_u3b());
    ASSERT_EQ("", msg.u3b().str());

    ASSERT_FALSE(msg.has_any());
    ASSERT_FALSE(msg.any().has_type_url());
    ASSERT_FALSE(msg.any().has_value());
    ASSERT_EQ(0, msg.any().value().size());

    std::cout << msg;
  }
}

TEST(PhaserTest, GarbageWithValidMagic) {
  char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = rand() & 0xff;
  }
  // Set the magic.
  *reinterpret_cast<uint32_t *>(buffer) = ::toolbelt::kFixedBufferMagic;

  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, GarbageWithTailoring) {
  char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = rand() & 0xff;
  }
  // Set the magic and the header fields.
  *reinterpret_cast<uint32_t *>(buffer) = ::toolbelt::kFixedBufferMagic;
  *reinterpret_cast<uint32_t *>(buffer + 4) = 0x1c;  // message location.
  *reinterpret_cast<uint32_t *>(buffer + 8) = 1024;  // hwm.
  *reinterpret_cast<uint32_t *>(buffer + 12) = 1000; // full_size
  *reinterpret_cast<uint32_t *>(buffer + 16) = 50;   // free list

  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}


int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
