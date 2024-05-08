
#include "absl/strings/str_format.h"
#include "phaser/runtime/runtime.h"
#include "phaser/testdata/TestMessage.pb.h"
#include "phaser/testdata/TestMessage.phaser.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"
#include <gtest/gtest.h>
#include <sstream>

TEST(PhaserTest, ProtobufCompatBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto m = msg.mutable_m();
  m->set_str("Inner message");

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());
  ASSERT_EQ("Inner message", msg.m().str());

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
  auto msg2 = foo::bar::phaser::TestMessageNewFields::CreateReadonly(msg.Data());
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
  auto msg2 = foo::bar::phaser::TestMessageNewFields::CreateReadonly(msg.Data());
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
  auto msg2 = foo::bar::phaser::TestMessageDeletedFields::CreateReadonly(msg.Data());
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
  msg2.CopyFrom(msg);

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


int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}


