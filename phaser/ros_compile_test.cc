// Compile and runtime fixture for frontend=ros generated messages.
#include "phaser/testdata/RosCompile.phaser.h"
#include "phaser/testdata/RosCompile.pb.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace foo::bar::phaser {
namespace {

TEST(RosCompileTest, ScalarConversionAndAssignment) {
  RosCompileMessage msg;
  msg.x = 42;
  msg.flag = true;
  msg.value = 3.5;
  msg.color = RosColor::ROS_COLOR_RED;

  EXPECT_TRUE(msg.x.IsPresent());
  EXPECT_TRUE(msg.flag.IsPresent());
  EXPECT_TRUE(msg.value.IsPresent());
  EXPECT_TRUE(msg.color.IsPresent());

  int32_t x = msg.x;
  bool flag = msg.flag;
  double value = msg.value;
  RosColor color = msg.color;
  EXPECT_EQ(x, 42);
  EXPECT_TRUE(flag);
  EXPECT_DOUBLE_EQ(value, 3.5);
  EXPECT_EQ(color, RosColor::ROS_COLOR_RED);

  msg.x = 7;
  msg.flag = false;
  msg.color = RosColor::ROS_COLOR_BLUE;
  EXPECT_EQ(msg.x.Get(), 7);
  EXPECT_FALSE(static_cast<bool>(msg.flag));
  EXPECT_EQ(msg.color.Get(), RosColor::ROS_COLOR_BLUE);
}

TEST(RosCompileTest, StringConversionAndAssignment) {
  RosCompileMessage msg;
  msg.name = "hello";
  EXPECT_TRUE(msg.name.IsPresent());
  EXPECT_EQ(std::string_view(msg.name), "hello");

  msg.name = std::string("world");
  EXPECT_EQ(msg.name.Get(), "world");

  const char* cstr = "from_cstr";
  msg.name = cstr;
  EXPECT_EQ(msg.name.Get(), "from_cstr");

  RosCompileMessage other;
  other.name = "proxy source";
  msg.name = other.name;
  EXPECT_EQ(msg.name.Get(), "proxy source");
  EXPECT_TRUE(other.name.IsPresent());
}

TEST(RosCompileTest, IndirectMessageAccess) {
  RosCompileMessage msg;
  EXPECT_FALSE(msg.inner.IsPresent());

  const RosCompileMessage& cmsg = msg;
  EXPECT_FALSE(cmsg.inner.IsPresent());
  EXPECT_EQ(cmsg.inner->id.Get(), 0);

  msg.inner->id = 99;
  EXPECT_TRUE(msg.inner.IsPresent());
  EXPECT_EQ(msg.inner->id.Get(), 99);
  EXPECT_EQ((*msg.inner).id.Get(), 99);

  RosInner& inner_ref = msg.inner;
  inner_ref.id = 100;
  EXPECT_EQ(msg.inner->id.Get(), 100);

  const RosInner& inner_cref = cmsg.inner;
  EXPECT_EQ(inner_cref.id.Get(), 100);
}

TEST(RosCompileTest, PrimitiveVectorSyntax) {
  RosCompileMessage msg;
  msg.xs.push_back(1);
  msg.xs.push_back(2);
  msg.xs.reserve(8);
  EXPECT_EQ(msg.xs.size(), 2u);
  EXPECT_EQ(msg.xs[0], 1);
  EXPECT_EQ(msg.xs[1], 2);

  msg.xs[0] = 10;
  EXPECT_EQ(msg.xs.front(), 10);
  EXPECT_EQ(msg.xs.back(), 2);

  msg.xs.resize(4);
  EXPECT_EQ(msg.xs.size(), 4u);
  msg.xs[3] = 40;
  EXPECT_EQ(msg.xs[3], 40);

  std::vector<int32_t> seen;
  for (int32_t v : msg.xs) {
    seen.push_back(v);
  }
  EXPECT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0], 10);
  EXPECT_EQ(seen[3], 40);

  msg.xs.clear();
  EXPECT_TRUE(msg.xs.empty());
}

TEST(RosCompileTest, EnumVectorSyntax) {
  RosCompileMessage msg;
  msg.colors.push_back(RosColor::ROS_COLOR_RED);
  msg.colors.push_back(RosColor::ROS_COLOR_BLUE);
  EXPECT_EQ(msg.colors.size(), 2u);
  EXPECT_EQ(msg.colors[0], RosColor::ROS_COLOR_RED);
  msg.colors[1] = RosColor::ROS_COLOR_UNSPECIFIED;
  EXPECT_EQ(msg.colors[1], RosColor::ROS_COLOR_UNSPECIFIED);
}

TEST(RosCompileTest, StringVectorSyntax) {
  RosCompileMessage msg;
  msg.names.push_back("a");
  msg.names.push_back("b");
  EXPECT_EQ(msg.names.size(), 2u);
  EXPECT_EQ(std::string_view(msg.names[0]), "a");

  msg.names[1] = "beta";
  EXPECT_EQ(msg.names[1].Get(), "beta");

  msg.names[0] = msg.names[1];
  EXPECT_EQ(msg.names[0].Get(), "beta");

  size_t count = 0;
  for (std::string_view s : msg.names) {
    EXPECT_FALSE(s.empty());
    ++count;
  }
  EXPECT_EQ(count, 2u);
}

TEST(RosCompileTest, MessageVectorSyntax) {
  RosCompileMessage msg;
  auto a = msg.inners.Add();
  a->id = 1;
  auto b = msg.inners.Add();
  b->id = 2;
  EXPECT_EQ(msg.inners.size(), 2u);
  EXPECT_EQ(msg.inners[0]->id.Get(), 1);
  EXPECT_EQ(msg.inners[1]->id.Get(), 2);

  msg.inners[0]->id = 11;
  EXPECT_EQ(msg.inners.front()->id.Get(), 11);

  size_t count = 0;
  for (auto elem : msg.inners) {
    EXPECT_TRUE(elem->id.IsPresent());
    ++count;
  }
  EXPECT_EQ(count, 2u);
}

TEST(RosCompileTest, ProxyAssignmentDoesNotRebind) {
  RosCompileMessage dst;
  RosCompileMessage src;
  src.x = 5;
  src.name = "src";
  src.inner->id = 77;

  dst.x = src.x;
  dst.name = src.name;
  dst.inner = src.inner;

  EXPECT_EQ(dst.x.Get(), 5);
  EXPECT_EQ(dst.name.Get(), "src");
  EXPECT_EQ(dst.inner->id.Get(), 77);

  src.x = 999;
  src.name = "mutated";
  src.inner->id = 0;
  EXPECT_EQ(dst.x.Get(), 5);
  EXPECT_EQ(dst.name.Get(), "src");
  EXPECT_EQ(dst.inner->id.Get(), 77);
}

TEST(RosCompileTest, ProxyMoveAssignIsValueSemantics) {
  RosCompileMessage dst;
  RosCompileMessage src;
  src.x = 5;
  src.name = "move-me";
  src.inner->id = 12;

  dst.x = std::move(src.x);
  dst.name = std::move(src.name);
  dst.inner = std::move(src.inner);

  EXPECT_EQ(dst.x.Get(), 5);
  EXPECT_EQ(dst.name.Get(), "move-me");
  EXPECT_EQ(dst.inner->id.Get(), 12);

  // Source proxies keep their owner binding and values (no structural rebind).
  EXPECT_EQ(src.x.Get(), 5);
  EXPECT_EQ(src.name.Get(), "move-me");
  EXPECT_EQ(src.inner->id.Get(), 12);

  src.x = 999;
  EXPECT_EQ(dst.x.Get(), 5);
}


TEST(RosCompileTest, MessageCopyAssignUsesCloneFrom) {
  RosCompileMessage src;
  src.x = 11;
  src.name = "copy";
  src.xs.push_back(3);

  RosCompileMessage dst;
  dst = src;

  EXPECT_EQ(dst.x.Get(), 11);
  EXPECT_EQ(dst.name.Get(), "copy");
  ASSERT_EQ(dst.xs.size(), 1u);
  EXPECT_EQ(dst.xs[0], 3);

  EXPECT_NE(dst.runtime.get(), src.runtime.get());
  src.x = 0;
  EXPECT_EQ(dst.x.Get(), 11);
}

TEST(RosCompileTest, MessageMoveAssignUsesCloneFrom) {
  RosCompileMessage src;
  src.x = 21;
  src.name = "moved";
  src.xs.push_back(9);

  RosCompileMessage dst;
  dst = std::move(src);

  EXPECT_EQ(dst.x.Get(), 21);
  EXPECT_EQ(dst.name.Get(), "moved");
  ASSERT_EQ(dst.xs.size(), 1u);
  EXPECT_EQ(dst.xs[0], 9);
  EXPECT_FALSE(src.x.IsPresent());
  EXPECT_FALSE(src.name.IsPresent());
  EXPECT_TRUE(src.xs.empty());
}

TEST(RosCompileTest, MessageCopyCtorDeepCopies) {
  RosCompileMessage src;
  src.x = 31;
  src.names.push_back("deep");

  RosCompileMessage copy(src);
  EXPECT_EQ(copy.x.Get(), 31);
  ASSERT_EQ(copy.names.size(), 1u);
  EXPECT_EQ(copy.names[0].Get(), "deep");
  EXPECT_NE(copy.runtime.get(), src.runtime.get());

  src.x = 0;
  EXPECT_EQ(copy.x.Get(), 31);
}

TEST(RosCompileTest, MessageMoveCtorRebindsStringCaches) {
  auto source = std::make_unique<RosCompileMessage>();
  source->names.push_back("vector");
  source->fixed_names[0] = "array";
  // Populate both source-side caches before moving the owner.
  EXPECT_EQ(source->names[0].Get(), "vector");
  EXPECT_EQ(source->fixed_names[0].Get(), "array");

  RosCompileMessage moved(std::move(*source));
  source.reset();

  EXPECT_EQ(moved.names[0].Get(), "vector");
  EXPECT_EQ(moved.fixed_names[0].Get(), "array");
  moved.names[0] = "updated";
  moved.fixed_names[0] = "updated-array";
  EXPECT_EQ(moved.names[0].Get(), "updated");
  EXPECT_EQ(moved.fixed_names[0].Get(), "updated-array");
}

TEST(RosCompileTest, EnumVectorDataAccessor) {
  RosCompileMessage msg;
  msg.colors.push_back(RosColor::ROS_COLOR_RED);
  ASSERT_NE(msg.colors.data(), nullptr);
  EXPECT_EQ(msg.colors.data()[0], RosColor::ROS_COLOR_RED);

  const RosCompileMessage& cmsg = msg;
  ASSERT_NE(cmsg.colors.data(), nullptr);
  EXPECT_EQ(cmsg.colors.data()[0], RosColor::ROS_COLOR_RED);
}

TEST(RosCompileTest, CloneFromCopyAndBufferGrowth) {
  RosCompileMessage src;
  src.x = 1;
  src.name = "grow";
  src.xs.reserve(64);
  for (int i = 0; i < 32; i++) {
    src.xs.push_back(i);
    src.names.push_back(std::to_string(i));
  }
  src.inner->id = 42;
  EXPECT_TRUE(src.fixed_names.Get(0).empty());

  RosCompileMessage copy;
  ASSERT_TRUE(copy.CloneFrom(src).ok());
  EXPECT_EQ(copy.x.Get(), 1);
  EXPECT_EQ(copy.name.Get(), "grow");
  EXPECT_EQ(copy.xs.size(), 32u);
  EXPECT_EQ(copy.names.size(), 32u);
  EXPECT_EQ(copy.inner->id.Get(), 42);

  RosCompileMessage moved_from;
  moved_from.x = 99;
  moved_from.name = "move";
  moved_from.xs = src.xs;
  moved_from.names = src.names;
  moved_from.inner = src.inner;

  RosCompileMessage move_dst;
  ASSERT_TRUE(move_dst.CloneFrom(moved_from).ok());
  EXPECT_EQ(move_dst.x.Get(), 99);
  EXPECT_EQ(move_dst.names.size(), 32u);
  EXPECT_EQ(move_dst.inner->id.Get(), 42);

  copy.xs.reserve(128);
  for (int i = 0; i < 64; i++) {
    copy.xs.push_back(1000 + i);
  }
  EXPECT_EQ(copy.xs.size(), 96u);
  EXPECT_EQ(copy.xs[95], 1000 + 63);
}

TEST(RosCompileTest, FixedPrimitiveArrayExtentAndMutation) {
  RosCompileMessage msg;
  EXPECT_EQ(msg.fixed_ints.size(), 4u);
  EXPECT_EQ(msg.fixed_ints.max_size(), 4u);

  msg.fixed_ints[0] = 10;
  msg.fixed_ints[1] = 20;
  msg.fixed_ints[3] = 40;
  EXPECT_EQ(msg.fixed_ints.front(), 10);
  EXPECT_EQ(msg.fixed_ints.back(), 40);
  EXPECT_EQ(msg.fixed_ints[2], 0);

  std::vector<int32_t> seen;
  for (int32_t v : msg.fixed_ints) {
    seen.push_back(v);
  }
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[1], 20);
}

TEST(RosCompileTest, FixedEnumArrayExtent) {
  RosCompileMessage msg;
  EXPECT_EQ(msg.fixed_colors.size(), 3u);
  msg.fixed_colors[0] = RosColor::ROS_COLOR_RED;
  msg.fixed_colors[2] = RosColor::ROS_COLOR_BLUE;
  EXPECT_EQ(msg.fixed_colors[0], RosColor::ROS_COLOR_RED);
  EXPECT_EQ(msg.fixed_colors[1], RosColor::ROS_COLOR_UNSPECIFIED);
  EXPECT_EQ(msg.fixed_colors[2], RosColor::ROS_COLOR_BLUE);
}

TEST(RosCompileTest, FixedStringArrayExtentAndAssignment) {
  RosCompileMessage msg;
  EXPECT_EQ(msg.fixed_names.size(), 2u);
  msg.fixed_names[0] = "alpha";
  msg.fixed_names[1] = "beta";
  EXPECT_EQ(msg.fixed_names[0].Get(), "alpha");
  EXPECT_EQ(std::string_view(msg.fixed_names[1]), "beta");

  msg.fixed_names[0] = msg.fixed_names[1];
  EXPECT_EQ(msg.fixed_names[0].Get(), "beta");
}

TEST(RosCompileTest, UntouchedFixedStringArrayReadsDefault) {
  const RosCompileMessage msg;
  EXPECT_TRUE(msg.fixed_names.Get(0).empty());
  EXPECT_TRUE(msg.fixed_names.Get(1).empty());
}

TEST(RosCompileTest, FixedMessageArrayExtent) {
  RosCompileMessage msg;
  EXPECT_EQ(msg.fixed_inners.size(), 2u);
  msg.fixed_inners[0]->id = 7;
  msg.fixed_inners[1]->id = 8;
  EXPECT_EQ(msg.fixed_inners[0]->id.Get(), 7);
  EXPECT_EQ(msg.fixed_inners[1]->id.Get(), 8);

  size_t count = 0;
  for (auto inner : msg.fixed_inners) {
    EXPECT_TRUE(inner->id.IsPresent());
    ++count;
  }
  EXPECT_EQ(count, 2u);
}

TEST(RosCompileTest, FixedArrayClearRestoresLogicalDefaults) {
  RosCompileMessage msg;
  msg.fixed_ints[0] = 99;
  msg.fixed_ints[3] = 42;
  msg.fixed_names[0] = "keep";
  msg.fixed_inners[1]->id = 5;

  msg.fixed_ints.clear();
  msg.fixed_names.clear();
  msg.fixed_inners.clear();

  EXPECT_EQ(msg.fixed_ints.size(), 4u);
  EXPECT_EQ(msg.fixed_ints[0], 0);
  EXPECT_EQ(msg.fixed_ints[3], 0);
  EXPECT_EQ(msg.fixed_names.size(), 2u);
  EXPECT_TRUE(msg.fixed_names[0].Get().empty());
  EXPECT_EQ(msg.fixed_inners.size(), 2u);
  EXPECT_FALSE(msg.fixed_inners[1]->id.IsPresent());
}

TEST(RosCompileTest, FixedArrayWireRoundtrip) {
  RosCompileMessage msg;
  msg.fixed_ints[0] = 1;
  msg.fixed_ints[2] = 3;
  msg.fixed_colors[1] = RosColor::ROS_COLOR_RED;
  msg.fixed_names[0] = "wire";
  msg.fixed_inners[0]->id = 55;

  std::string wire = msg.SerializeAsString();
  ASSERT_FALSE(wire.empty());

  RosCompileMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ(parsed.fixed_ints.size(), 4u);
  EXPECT_EQ(parsed.fixed_ints[0], 1);
  EXPECT_EQ(parsed.fixed_ints[2], 3);
  EXPECT_EQ(parsed.fixed_colors[1], RosColor::ROS_COLOR_RED);
  EXPECT_EQ(parsed.fixed_names[0].Get(), "wire");
  EXPECT_EQ(parsed.fixed_inners[0]->id.Get(), 55);
}

TEST(RosCompileTest, FixedArrayPartialWireDefaultFillsExtent) {
  RosCompileMessage msg;
  msg.fixed_ints[0] = 100;
  std::string wire = msg.SerializeAsString();

  RosCompileMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ(parsed.fixed_ints.size(), 4u);
  EXPECT_EQ(parsed.fixed_ints[0], 100);
  EXPECT_EQ(parsed.fixed_ints[1], 0);
  EXPECT_EQ(parsed.fixed_ints[3], 0);
}

TEST(RosCompileTest, FixedArrayOverflowRejected) {
  RosCompileMessage msg;
  msg.fixed_ints[0] = 1;
  msg.fixed_ints[1] = 2;
  msg.fixed_ints[2] = 3;
  msg.fixed_ints[3] = 4;
  std::string good = msg.SerializeAsString();

  // Append another packed fixed_ints field (tag 11, wire type 2) with one int.
  std::string overflow = good;
  overflow.push_back(static_cast<char>(0x5A));  // (11 << 3) | 2
  overflow.push_back(static_cast<char>(0x04));  // length 4
  overflow.push_back(static_cast<char>(0x05));
  overflow.push_back(static_cast<char>(0x00));
  overflow.push_back(static_cast<char>(0x00));
  overflow.push_back(static_cast<char>(0x00));

  RosCompileMessage parsed;
  EXPECT_FALSE(parsed.ParseFromString(overflow));
}

TEST(RosCompileTest, FixedArrayConstViewAfterWireParse) {
  RosCompileMessage msg;
  msg.fixed_ints[1] = 22;
  msg.fixed_names[0] = "readonly";
  std::string wire = msg.SerializeAsString();

  RosCompileMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));

  const RosCompileMessage& view = parsed;
  EXPECT_EQ(view.fixed_ints.size(), 4u);
  EXPECT_EQ(view.fixed_ints[1], 22);
  EXPECT_EQ(view.fixed_names[0], "readonly");
}

TEST(RosCompileTest, FixedArrayCreateReadonlyConstAccess) {
  RosCompileMessage msg;
  msg.fixed_ints[0] = 10;
  msg.fixed_ints[2] = 30;
  msg.fixed_names[1] = "ro";
  msg.fixed_inners[0]->id = 4;

  std::vector<char> buffer(msg.ByteSizeLong());
  std::memcpy(buffer.data(), msg.Data(), buffer.size());

  RosCompileMessage ro =
      RosCompileMessage::CreateReadonly(buffer.data(), buffer.size());
  const RosCompileMessage& view = ro;

  EXPECT_EQ(view.fixed_ints.size(), 4u);
  EXPECT_EQ(view.fixed_ints[0], 10);
  EXPECT_EQ(view.fixed_ints[1], 0);
  EXPECT_EQ(view.fixed_ints[2], 30);
  EXPECT_EQ(view.fixed_names[0], "");
  EXPECT_EQ(view.fixed_names[1], "ro");
  EXPECT_EQ(view.fixed_inners[0]->id.Get(), 4);
  EXPECT_FALSE(view.fixed_inners[1]->id.IsPresent());

  std::string wire = view.SerializeAsString();
  EXPECT_FALSE(wire.empty());

  RosCompileMessage reparsed;
  ASSERT_TRUE(reparsed.ParseFromString(wire));
  EXPECT_EQ(reparsed.fixed_ints[0], 10);
  EXPECT_EQ(reparsed.fixed_ints[2], 30);
  EXPECT_EQ(reparsed.fixed_names[1].Get(), "ro");
  EXPECT_EQ(reparsed.fixed_inners[0]->id.Get(), 4);
}

TEST(RosCompileTest, FixedArrayReadonlyShortBufferDefaults) {
  RosCompileMessage msg;
  msg.fixed_ints[0] = 7;
  std::string partial = msg.SerializeAsString();

  std::vector<char> buffer(msg.ByteSizeLong());
  std::memcpy(buffer.data(), msg.Data(), buffer.size());

  RosCompileMessage ro =
      RosCompileMessage::CreateReadonly(buffer.data(), buffer.size());
  const RosCompileMessage& view = ro;

  EXPECT_EQ(view.fixed_ints[0], 7);
  EXPECT_EQ(view.fixed_ints[1], 0);
  EXPECT_EQ(view.fixed_ints[3], 0);
  EXPECT_TRUE(view.fixed_names[0].empty());
  (void)partial;
}

TEST(RosCompileTest, FixedArrayCloneFromCopiesExtent) {
  RosCompileMessage src;
  src.fixed_ints[0] = 9;
  src.fixed_names[1] = "clone";
  src.fixed_inners[0]->id = 3;

  RosCompileMessage dst;
  ASSERT_TRUE(dst.CloneFrom(src).ok());
  EXPECT_EQ(dst.fixed_ints.size(), 4u);
  EXPECT_EQ(dst.fixed_ints[0], 9);
  EXPECT_EQ(dst.fixed_names[1].Get(), "clone");
  EXPECT_EQ(dst.fixed_inners[0]->id.Get(), 3);
}

TEST(RosCompileTest, VariantOneofNamedAlternatives) {
  using Count = RosCompileMessage::ChoiceCountAlternative;
  using Code = RosCompileMessage::ChoiceCodeAlternative;
  using Name = RosCompileMessage::ChoiceNameAlternative;
  using Inner = RosCompileMessage::ChoiceInnerAlternative;

  RosCompileMessage msg;
  EXPECT_EQ(msg.choice.index(), std::variant_npos);
  EXPECT_TRUE(msg.choice.valueless_by_exception());

  EXPECT_EQ(msg.choice.emplace<Count>(42), 42);
  EXPECT_EQ(msg.choice.index(), 0u);
  EXPECT_EQ(msg.choice.case_number(), 15);
  EXPECT_TRUE(msg.choice.holds_alternative<Count>());
  EXPECT_FALSE(msg.choice.holds_alternative<Code>());
  EXPECT_EQ(msg.choice.get<Count>(), 42);
  EXPECT_THROW((void)msg.choice.get<Code>(), std::bad_variant_access);

  EXPECT_EQ(msg.choice.emplace<Code>(7), 7);
  EXPECT_FALSE(msg.choice.holds_alternative<Count>());
  EXPECT_TRUE(msg.choice.holds_alternative<Code>());

  EXPECT_EQ(msg.choice.emplace<Name>("laser"), "laser");
  EXPECT_TRUE(msg.choice.holds_alternative<Name>());
  EXPECT_EQ(msg.choice.get<Name>(), "laser");

  RosInner& inner = msg.choice.emplace<Inner>();
  inner.id = 99;
  EXPECT_TRUE(msg.choice.holds_alternative<Inner>());
  EXPECT_EQ(msg.choice.get<Inner>().id.Get(), 99);

  msg.choice.reset();
  EXPECT_EQ(msg.choice.index(), std::variant_npos);
  EXPECT_EQ(msg.choice.case_number(), 0);
}

TEST(RosCompileTest, VariantOneofSwitchingCleansVariableArms) {
  using Count = RosCompileMessage::ChoiceCountAlternative;
  using Name = RosCompileMessage::ChoiceNameAlternative;
  using Inner = RosCompileMessage::ChoiceInnerAlternative;

  RosCompileMessage msg(256, ::phaser::Tuning::kSize);
  for (int i = 0; i < 100; ++i) {
    msg.choice.emplace<Name>(std::string(128, static_cast<char>('a' + i % 26)));
    EXPECT_EQ(msg.choice.get<Name>().size(), 128u);
    msg.choice.emplace<Inner>().id = i;
    EXPECT_EQ(msg.choice.get<Inner>().id.Get(), i);
    msg.choice.emplace<Count>(i);
    EXPECT_EQ(msg.choice.get<Count>(), i);
  }
  msg.choice.reset();
  EXPECT_EQ(msg.choice.case_number(), 0);
}

TEST(RosCompileTest, VariantOneofWireRoundtrip) {
  using Name = RosCompileMessage::ChoiceNameAlternative;
  using Inner = RosCompileMessage::ChoiceInnerAlternative;

  RosCompileMessage source;
  source.choice.emplace<Name>("wire");
  std::string wire = source.SerializeAsString();

  RosCompileMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  ASSERT_TRUE(parsed.choice.holds_alternative<Name>());
  EXPECT_EQ(parsed.choice.get<Name>(), "wire");

  source.choice.emplace<Inner>().id = 123;
  wire = source.SerializeAsString();
  ASSERT_TRUE(parsed.ParseFromString(wire));
  ASSERT_TRUE(parsed.choice.holds_alternative<Inner>());
  EXPECT_EQ(parsed.choice.get<Inner>().id.Get(), 123);
}

TEST(RosCompileTest, StandardProtobufWireCompatibility) {
  using Name = RosCompileMessage::ChoiceNameAlternative;

  RosCompileMessage ros;
  ros.x = 41;
  ros.name = "phaser";
  ros.fixed_ints[0] = 5;
  ros.fixed_ints[3] = 8;
  ros.choice.emplace<Name>("map");

  ::foo::bar::RosCompileMessage protobuf;
  ASSERT_TRUE(protobuf.ParseFromString(ros.SerializeAsString()));
  EXPECT_EQ(protobuf.x(), 41);
  EXPECT_EQ(protobuf.name(), "phaser");
  ASSERT_EQ(protobuf.fixed_ints_size(), 4);
  EXPECT_EQ(protobuf.fixed_ints(0), 5);
  EXPECT_EQ(protobuf.fixed_ints(3), 8);
  EXPECT_EQ(protobuf.choice_name(), "map");

  protobuf.set_x(73);
  protobuf.set_name("protobuf");
  protobuf.set_choice_name("odom");

  RosCompileMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(protobuf.SerializeAsString()));
  EXPECT_EQ(parsed.x.Get(), 73);
  EXPECT_EQ(parsed.name.Get(), "protobuf");
  ASSERT_TRUE(parsed.choice.holds_alternative<Name>());
  EXPECT_EQ(parsed.choice.get<Name>(), "odom");
}

}  // namespace
}  // namespace foo::bar::phaser
