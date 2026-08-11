// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "absl/types/span.h"
#include "phaser/runtime/ros_wireformat.h"
#include "phaser/testdata/RosCompile.pb.h"
#include "phaser/testdata/RosCompile.phaser.h"
#include "phaser/testdata/RosIntrinsics.phaser.h"
#include "phaser/testdata/RosIntrinsicsProtobufFrontend.phaser.h"

namespace {

using RosCompileMessage = ::foo::bar::phaser::RosCompileMessage;
using RosInner = ::foo::bar::phaser::RosInner;
using RosIntrinsicMessage = ::foo::bar::phaser::RosIntrinsicMessage;
using ProtobufFrontendIntrinsicMessage =
    ::foo::bar::pb::protobuf_phaser::RosIntrinsicMessage;
using RosColor = ::foo::bar::phaser::RosColor;

template <typename T>
void AppendIntegral(std::string& bytes, T value) {
  static_assert(std::is_integral_v<T>);
  using U = std::make_unsigned_t<T>;
  U bits = static_cast<U>(value);
  for (size_t i = 0; i < sizeof(U); ++i) {
    bytes.push_back(static_cast<char>(bits & static_cast<U>(0xff)));
    if constexpr (sizeof(U) > 1) {
      bits >>= 8;
    }
  }
}

void AppendDouble(std::string& bytes, double value) {
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  AppendIntegral(bytes, bits);
}

void AppendString(std::string& bytes, std::string_view value) {
  AppendIntegral(bytes, static_cast<uint32_t>(value.size()));
  bytes.append(value);
}

void PopulatePhaserMessage(RosCompileMessage& message) {
  message.x = -7;
  message.name = "robot";
  message.flag = true;
  message.value = 1.5;
  message.color = RosColor::ROS_COLOR_RED;
  message.inner->id = 42;

  message.xs.push_back(10);
  message.xs.push_back(-20);
  message.names.push_back("a");
  message.names.push_back("beta");
  message.colors.push_back(RosColor::ROS_COLOR_RED);
  message.colors.push_back(RosColor::ROS_COLOR_BLUE);
  message.inners.Add()->id = 100;
  message.inners.Add()->id = 200;

  message.fixed_ints[0] = 1;
  message.fixed_ints[1] = 2;
  message.fixed_ints[2] = 3;
  message.fixed_ints[3] = 4;
  message.fixed_colors[0] = RosColor::ROS_COLOR_RED;
  message.fixed_colors[1] = RosColor::ROS_COLOR_BLUE;
  message.fixed_colors[2] = RosColor::ROS_COLOR_UNSPECIFIED;
  message.fixed_names[0] = "left";
  message.fixed_names[1] = "right";
  message.fixed_inners[0]->id = 300;
  message.fixed_inners[1]->id = 400;

  using ChoiceName = RosCompileMessage::ChoiceNameAlternative;
  message.choice.emplace<ChoiceName>("selected");
}

void PopulateProtobufMessage(::foo::bar::RosCompileMessage& message) {
  message.set_x(-7);
  message.set_name("robot");
  message.set_flag(true);
  message.set_value(1.5);
  message.set_color(::foo::bar::ROS_COLOR_RED);
  message.mutable_inner()->set_id(42);

  message.add_xs(10);
  message.add_xs(-20);
  message.add_names("a");
  message.add_names("beta");
  message.add_colors(::foo::bar::ROS_COLOR_RED);
  message.add_colors(::foo::bar::ROS_COLOR_BLUE);
  message.add_inners()->set_id(100);
  message.add_inners()->set_id(200);

  for (int32_t value : {1, 2, 3, 4}) {
    message.add_fixed_ints(value);
  }
  message.add_fixed_colors(::foo::bar::ROS_COLOR_RED);
  message.add_fixed_colors(::foo::bar::ROS_COLOR_BLUE);
  message.add_fixed_colors(::foo::bar::ROS_COLOR_UNSPECIFIED);
  message.add_fixed_names("left");
  message.add_fixed_names("right");
  message.add_fixed_inners()->set_id(300);
  message.add_fixed_inners()->set_id(400);
  message.set_choice_name("selected");
}

std::string ExpectedRosCompileBytes(bool include_oneof = true) {
  std::string bytes;
  AppendIntegral(bytes, static_cast<int32_t>(-7));
  AppendString(bytes, "robot");
  AppendIntegral(bytes, static_cast<uint8_t>(1));
  AppendDouble(bytes, 1.5);
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_RED));
  AppendIntegral(bytes, static_cast<int32_t>(42));

  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, static_cast<int32_t>(10));
  AppendIntegral(bytes, static_cast<int32_t>(-20));
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendString(bytes, "a");
  AppendString(bytes, "beta");
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_RED));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_BLUE));
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, static_cast<int32_t>(100));
  AppendIntegral(bytes, static_cast<int32_t>(200));

  for (int32_t value : {1, 2, 3, 4}) {
    AppendIntegral(bytes, value);
  }
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_RED));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_BLUE));
  AppendIntegral(bytes,
                 static_cast<int32_t>(RosColor::ROS_COLOR_UNSPECIFIED));
  AppendString(bytes, "left");
  AppendString(bytes, "right");
  AppendIntegral(bytes, static_cast<int32_t>(300));
  AppendIntegral(bytes, static_cast<int32_t>(400));

  if (include_oneof) {
    AppendIntegral(bytes, static_cast<uint32_t>(17));
    AppendString(bytes, "selected");
  } else {
    AppendIntegral(bytes, static_cast<uint32_t>(0));
  }
  return bytes;
}

std::string ExpectedIntrinsicBytes() {
  std::string bytes;
  AppendIntegral(bytes, static_cast<uint32_t>(12));
  AppendIntegral(bytes, static_cast<uint32_t>(345));
  AppendIntegral(bytes, static_cast<int32_t>(-4));
  AppendIntegral(bytes, static_cast<int32_t>(500));
  AppendIntegral(bytes, static_cast<uint32_t>(9));
  AppendIntegral(bytes, static_cast<uint32_t>(21));
  AppendIntegral(bytes, static_cast<uint32_t>(654));
  AppendString(bytes, "map");

  AppendIntegral(bytes, static_cast<int32_t>(0));  // count
  AppendString(bytes, "");                         // name
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // samples
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // tags
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // children
  AppendString(bytes, "");                          // fixed_names[0]
  AppendString(bytes, "");                          // fixed_names[1]
  for (int i = 0; i < 2; ++i) {
    AppendIntegral(bytes, static_cast<int32_t>(0));  // child id
    AppendString(bytes, "");                         // child label
  }
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // choice unset
  return bytes;
}

TEST(ROSWireConversionTest, LiveProtobufAndNativePathsMatchKnownBytes) {
  RosCompileMessage phaser_message;
  PopulatePhaserMessage(phaser_message);
  const std::string expected = ExpectedRosCompileBytes();

  ::phaser::ROSBuffer live_output(16);
  ASSERT_TRUE(phaser_message.SerializeToROS(live_output).ok());
  EXPECT_EQ(live_output.AsString(), expected);
  EXPECT_EQ(phaser_message.ROSSerializedSize(), expected.size());

  std::string string_output;
  ASSERT_TRUE(phaser_message.SerializeToROSString(&string_output).ok());
  EXPECT_EQ(string_output, expected);

  ::foo::bar::RosCompileMessage protobuf_message;
  PopulateProtobufMessage(protobuf_message);
  const std::string protobuf_wire = protobuf_message.SerializeAsString();
  EXPECT_EQ(::phaser::InferMessageWireFormat(protobuf_wire),
            ::phaser::MessageWireFormat::kProtobuf);
  ::phaser::ROSBuffer protobuf_output;
  ASSERT_TRUE(RosCompileMessage::ProtobufToROS(protobuf_wire, protobuf_output)
                  .ok());
  EXPECT_EQ(protobuf_output.AsString(), expected);

  ::phaser::ROSBuffer native_output;
  const auto* native_data =
      reinterpret_cast<const char*>(phaser_message.Data());
  const absl::Span<const char> native_bytes(native_data,
                                            phaser_message.Size());
  EXPECT_EQ(::phaser::InferMessageWireFormat(native_bytes),
            ::phaser::MessageWireFormat::kPhaser);
  ASSERT_TRUE(RosCompileMessage::PhaserToROS(
                  native_bytes, native_output)
                  .ok());
  EXPECT_EQ(native_output.AsString(), expected);

  ::phaser::ROSBuffer inferred_protobuf_output;
  ASSERT_TRUE(RosCompileMessage::ConvertToROS(
                  absl::Span<const char>(protobuf_wire.data(),
                                         protobuf_wire.size()),
                  inferred_protobuf_output)
                  .ok());
  EXPECT_EQ(inferred_protobuf_output.AsString(), expected);

  ::phaser::ROSBuffer inferred_native_output;
  ASSERT_TRUE(
      RosCompileMessage::ConvertToROS(native_bytes, inferred_native_output)
          .ok());
  EXPECT_EQ(inferred_native_output.AsString(), expected);
}

TEST(ROSWireConversionTest, FixedOutputAndErrorsAreReported) {
  RosCompileMessage message;
  PopulatePhaserMessage(message);
  const std::string expected = ExpectedRosCompileBytes();

  std::vector<char> exact(expected.size());
  ASSERT_TRUE(message.SerializeToROSArray(exact.data(), exact.size()).ok());
  EXPECT_EQ(std::string(exact.data(), exact.size()), expected);

  std::vector<char> too_small(expected.size() - 1);
  EXPECT_FALSE(
      message.SerializeToROSArray(too_small.data(), too_small.size()).ok());
  EXPECT_FALSE(message.SerializeToROSString(nullptr).ok());

  ::phaser::ROSBuffer output;
  EXPECT_FALSE(
      RosCompileMessage::ProtobufToROS(std::string(1, '\x80'), output).ok());
  EXPECT_TRUE(output.empty());
  EXPECT_FALSE(RosCompileMessage::PhaserToROS({}, output).ok());
  EXPECT_FALSE(RosCompileMessage::ConvertToROS(
                   absl::Span<const char>("\0", 1), output)
                   .ok());
}

TEST(ROSWireConversionTest, OneofWritesFieldNumberDiscriminator) {
  RosCompileMessage message;
  PopulatePhaserMessage(message);
  message.choice.reset();

  ::phaser::ROSBuffer output;
  ASSERT_TRUE(message.SerializeToROS(output).ok());
  EXPECT_EQ(output.AsString(), ExpectedRosCompileBytes(false));
}

TEST(ROSWireConversionTest, ROS1IntrinsicsUseNativeLayoutsAndFlushCaches) {
  RosIntrinsicMessage message;
  message.stamp = ::ros::Time(12, 345);
  message.timeout = ::ros::Duration(-4, 500);
  message.header->seq = 9;
  message.header->stamp = ::ros::Time(21, 654);
  message.header->frame_id = "map";
  const std::string expected = ExpectedIntrinsicBytes();

  ::phaser::ROSBuffer live_output;
  ASSERT_TRUE(message.SerializeToROS(live_output).ok());
  EXPECT_EQ(live_output.AsString(), expected);
  EXPECT_EQ(message.ROSSerializedSize(), expected.size());

  ::phaser::ROSBuffer protobuf_output;
  ASSERT_TRUE(RosIntrinsicMessage::ProtobufToROS(message.SerializeAsString(),
                                                protobuf_output)
                  .ok());
  EXPECT_EQ(protobuf_output.AsString(), expected);

  ::phaser::ROSBuffer native_output;
  const auto* native_data = reinterpret_cast<const char*>(message.Data());
  ASSERT_TRUE(RosIntrinsicMessage::PhaserToROS(
                  absl::Span<const char>(native_data, message.Size()),
                  native_output)
                  .ok());
  EXPECT_EQ(native_output.AsString(), expected);

  ::phaser::ROSBuffer protobuf_frontend_native_output;
  ASSERT_TRUE(ProtobufFrontendIntrinsicMessage::PhaserToROS(
                  absl::Span<const char>(native_data, message.Size()),
                  protobuf_frontend_native_output)
                  .ok());
  EXPECT_EQ(protobuf_frontend_native_output.AsString(), expected);
}

TEST(ROSWireConversionTest, ParsesKnownROSBytesIntoNativePayload) {
  RosCompileMessage message;
  const std::string input = ExpectedRosCompileBytes();
  ASSERT_TRUE(message.ParseFromROS(
                         absl::Span<const char>(input.data(), input.size()))
                  .ok());

  EXPECT_EQ(message.x.Get(), -7);
  EXPECT_EQ(message.name.Get(), "robot");
  EXPECT_TRUE(message.flag.Get());
  EXPECT_DOUBLE_EQ(message.value.Get(), 1.5);
  EXPECT_EQ(message.color.Get(), RosColor::ROS_COLOR_RED);
  EXPECT_EQ(message.inner->id.Get(), 42);
  ASSERT_EQ(message.xs.size(), 2u);
  EXPECT_EQ(message.xs[0], 10);
  EXPECT_EQ(message.xs[1], -20);
  ASSERT_EQ(message.names.size(), 2u);
  EXPECT_EQ(message.names[0].Get(), "a");
  EXPECT_EQ(message.names[1].Get(), "beta");
  ASSERT_EQ(message.inners.size(), 2u);
  EXPECT_EQ(message.inners[0]->id.Get(), 100);
  EXPECT_EQ(message.inners[1]->id.Get(), 200);
  EXPECT_EQ(message.fixed_ints[0], 1);
  EXPECT_EQ(message.fixed_ints[3], 4);
  EXPECT_EQ(message.fixed_names[0].Get(), "left");
  EXPECT_EQ(message.fixed_inners[1]->id.Get(), 400);

  using ChoiceName = RosCompileMessage::ChoiceNameAlternative;
  ASSERT_TRUE(message.choice.holds_alternative<ChoiceName>());
  EXPECT_EQ(message.choice.get<ChoiceName>(), "selected");

  ::foo::bar::RosCompileMessage protobuf;
  ASSERT_TRUE(protobuf.ParseFromString(message.SerializeAsString()));
  EXPECT_EQ(protobuf.x(), -7);
  EXPECT_EQ(protobuf.name(), "robot");
  ASSERT_EQ(protobuf.xs_size(), 2);
  EXPECT_EQ(protobuf.xs(1), -20);
  EXPECT_EQ(protobuf.fixed_inners(1).id(), 400);
  EXPECT_EQ(protobuf.choice_name(), "selected");
}

TEST(ROSWireConversionTest, ParsedROSPayloadUsesEitherFrontend) {
  RosIntrinsicMessage ros_message;
  const std::string input = ExpectedIntrinsicBytes();
  ASSERT_TRUE(ros_message
                  .ParseFromROS(
                      absl::Span<const char>(input.data(), input.size()))
                  .ok());

  EXPECT_EQ(ros_message.stamp->sec, 12u);
  EXPECT_EQ(ros_message.stamp->nsec, 345u);
  EXPECT_EQ(ros_message.timeout->sec, -4);
  EXPECT_EQ(ros_message.timeout->nsec, 500);
  EXPECT_EQ(ros_message.header->seq, 9u);
  EXPECT_EQ(ros_message.header->stamp.sec, 21u);
  EXPECT_EQ(ros_message.header->stamp.nsec, 654u);
  EXPECT_EQ(ros_message.header->frame_id, "map");
  EXPECT_EQ(ros_message.choice.index(), std::variant_npos);

  const size_t native_size = ros_message.Size();
  std::vector<char> native_payload(native_size);
  std::memcpy(native_payload.data(), ros_message.Data(), native_size);
  const ProtobufFrontendIntrinsicMessage protobuf_view =
      ProtobufFrontendIntrinsicMessage::CreateReadonly(native_payload.data(),
                                                       native_payload.size());
  EXPECT_EQ(protobuf_view.stamp().seconds(), 12);
  EXPECT_EQ(protobuf_view.stamp().nanos(), 345);
  EXPECT_EQ(protobuf_view.timeout().seconds(), -4);
  EXPECT_EQ(protobuf_view.timeout().nanos(), 500);
  EXPECT_EQ(protobuf_view.header().seq(), 9u);
  EXPECT_EQ(protobuf_view.header().frame_id(), "map");
  EXPECT_FALSE(protobuf_view.has_choice_number());
  EXPECT_FALSE(protobuf_view.has_choice_text());
  EXPECT_FALSE(protobuf_view.has_choice_child());

  ProtobufFrontendIntrinsicMessage parsed_protobuf_frontend;
  ASSERT_TRUE(parsed_protobuf_frontend
                  .ParseFromROS(
                      absl::Span<const char>(input.data(), input.size()))
                  .ok());
  EXPECT_EQ(parsed_protobuf_frontend.stamp().seconds(), 12);
  EXPECT_EQ(parsed_protobuf_frontend.timeout().nanos(), 500);
  EXPECT_EQ(parsed_protobuf_frontend.header().stamp().nanos(), 654);
  EXPECT_EQ(parsed_protobuf_frontend.header().frame_id(), "map");
}

TEST(ROSWireConversionTest, ParsesScalarAndMessageOneofArms) {
  std::string scalar_input = ExpectedRosCompileBytes(false);
  scalar_input.resize(scalar_input.size() - sizeof(uint32_t));
  AppendIntegral(scalar_input, static_cast<uint32_t>(15));
  AppendIntegral(scalar_input, static_cast<int32_t>(123));

  RosCompileMessage scalar_message;
  ASSERT_TRUE(
      scalar_message
          .ParseFromROS(
              absl::Span<const char>(scalar_input.data(), scalar_input.size()))
          .ok());
  using ChoiceCount = RosCompileMessage::ChoiceCountAlternative;
  ASSERT_TRUE(scalar_message.choice.holds_alternative<ChoiceCount>());
  EXPECT_EQ(scalar_message.choice.get<ChoiceCount>(), 123);

  std::string message_input = ExpectedRosCompileBytes(false);
  message_input.resize(message_input.size() - sizeof(uint32_t));
  AppendIntegral(message_input, static_cast<uint32_t>(18));
  AppendIntegral(message_input, static_cast<int32_t>(456));

  RosCompileMessage message;
  ASSERT_TRUE(
      message
          .ParseFromROS(
              absl::Span<const char>(message_input.data(), message_input.size()))
          .ok());
  using ChoiceInner = RosCompileMessage::ChoiceInnerAlternative;
  ASSERT_TRUE(message.choice.holds_alternative<ChoiceInner>());
  EXPECT_EQ(message.choice.get<ChoiceInner>().id.Get(), 456);
}

TEST(ROSWireConversionTest, RejectsMalformedROSInput) {
  const std::string valid = ExpectedRosCompileBytes();

  std::string truncated = valid;
  truncated.pop_back();
  RosCompileMessage truncated_message;
  EXPECT_FALSE(truncated_message
                   .ParseFromROS(absl::Span<const char>(truncated.data(),
                                                        truncated.size()))
                   .ok());

  std::string trailing = valid;
  trailing.push_back('\0');
  RosCompileMessage trailing_message;
  EXPECT_FALSE(
      trailing_message
          .ParseFromROS(
              absl::Span<const char>(trailing.data(), trailing.size()))
          .ok());

  std::string invalid_length = valid;
  invalid_length[4] = static_cast<char>(0xff);
  invalid_length[5] = static_cast<char>(0xff);
  invalid_length[6] = static_cast<char>(0xff);
  invalid_length[7] = static_cast<char>(0x7f);
  RosCompileMessage invalid_length_message;
  EXPECT_FALSE(
      invalid_length_message
          .ParseFromROS(absl::Span<const char>(invalid_length.data(),
                                               invalid_length.size()))
          .ok());

  std::string invalid_discriminator = valid;
  const size_t discriminator_offset =
      invalid_discriminator.size() - sizeof(uint32_t) - sizeof(uint32_t) - 8;
  invalid_discriminator[discriminator_offset] = 99;
  RosCompileMessage invalid_discriminator_message;
  EXPECT_FALSE(
      invalid_discriminator_message
          .ParseFromROS(absl::Span<const char>(invalid_discriminator.data(),
                                               invalid_discriminator.size()))
          .ok());

  std::string oversized_sequence;
  AppendIntegral(oversized_sequence, static_cast<int32_t>(0));
  AppendString(oversized_sequence, "");
  AppendIntegral(oversized_sequence, static_cast<uint8_t>(0));
  AppendDouble(oversized_sequence, 0);
  AppendIntegral(oversized_sequence, static_cast<int32_t>(0));
  AppendIntegral(oversized_sequence, static_cast<int32_t>(0));
  AppendIntegral(oversized_sequence, static_cast<uint32_t>(100));
  RosCompileMessage oversized_sequence_message;
  EXPECT_FALSE(
      oversized_sequence_message
          .ParseFromROS(absl::Span<const char>(oversized_sequence.data(),
                                               oversized_sequence.size()))
          .ok());
  EXPECT_TRUE(oversized_sequence_message.xs.empty());
}

}  // namespace
