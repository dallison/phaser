// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
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
using RosNarrowScalars = ::foo::bar::phaser::RosNarrowScalars;
using RosPackedFixedMessage = ::foo::bar::phaser::RosPackedFixedMessage;
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

void AppendFloat(std::string& bytes, float value) {
  uint32_t bits = 0;
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
  AppendIntegral(bytes, int32_t{-7});
  AppendString(bytes, "robot");
  AppendIntegral(bytes, static_cast<uint8_t>(1));
  AppendDouble(bytes, 1.5);
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_RED));
  AppendIntegral(bytes, int32_t{42});

  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, int32_t{10});
  AppendIntegral(bytes, int32_t{-20});
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendString(bytes, "a");
  AppendString(bytes, "beta");
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_RED));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_BLUE));
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, int32_t{100});
  AppendIntegral(bytes, int32_t{200});

  for (int32_t value : {1, 2, 3, 4}) {
    AppendIntegral(bytes, value);
  }
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_RED));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_BLUE));
  AppendIntegral(bytes, static_cast<int32_t>(RosColor::ROS_COLOR_UNSPECIFIED));
  AppendString(bytes, "left");
  AppendString(bytes, "right");
  AppendIntegral(bytes, int32_t{300});
  AppendIntegral(bytes, int32_t{400});

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
  AppendIntegral(bytes, int32_t{-4});
  AppendIntegral(bytes, int32_t{500});
  AppendIntegral(bytes, static_cast<uint32_t>(9));
  AppendIntegral(bytes, static_cast<uint32_t>(21));
  AppendIntegral(bytes, static_cast<uint32_t>(654));
  AppendString(bytes, "map");

  AppendIntegral(bytes, int32_t{0});                // count
  AppendString(bytes, "");                          // name
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // samples
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // tags
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // children
  AppendString(bytes, "");                          // fixed_names[0]
  AppendString(bytes, "");                          // fixed_names[1]
  for (int i = 0; i < 2; ++i) {
    AppendIntegral(bytes, int32_t{0});               // child id
    AppendString(bytes, "");                         // child label
  }
  AppendIntegral(bytes, static_cast<uint32_t>(0));  // choice unset

  // Repeated ROS intrinsics: a sequence length only when the extent is not
  // fixed, then sec and nsec per element, exactly as a singular one.
  AppendIntegral(bytes, static_cast<uint32_t>(2));  // stamps count
  AppendIntegral(bytes, static_cast<uint32_t>(1));
  AppendIntegral(bytes, static_cast<uint32_t>(2));
  AppendIntegral(bytes, static_cast<uint32_t>(3));
  AppendIntegral(bytes, static_cast<uint32_t>(4));
  AppendIntegral(bytes, static_cast<uint32_t>(1));  // timeouts count
  AppendIntegral(bytes, int32_t{-5});
  AppendIntegral(bytes, int32_t{6});
  AppendIntegral(bytes, static_cast<uint32_t>(7));  // fixed_stamps[0]
  AppendIntegral(bytes, static_cast<uint32_t>(8));
  AppendIntegral(bytes, static_cast<uint32_t>(9));  // fixed_stamps[1]
  AppendIntegral(bytes, static_cast<uint32_t>(10));
  AppendIntegral(bytes, int32_t{-11});  // fixed_timeouts[0]
  AppendIntegral(bytes, int32_t{12});
  AppendIntegral(bytes, int32_t{13});  // fixed_timeouts[1]
  AppendIntegral(bytes, int32_t{14});
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
  ASSERT_TRUE(
      RosCompileMessage::ProtobufToROS(protobuf_wire, protobuf_output).ok());
  EXPECT_EQ(protobuf_output.AsString(), expected);
  std::vector<char> exact_output(expected.size());
  ::phaser::ROSBuffer fixed_protobuf_output(exact_output.data(),
                                            exact_output.size());
  ASSERT_TRUE(
      RosCompileMessage::ProtobufToROS(protobuf_wire, fixed_protobuf_output)
          .ok());
  EXPECT_EQ(fixed_protobuf_output.AsSpan(),
            absl::Span<const char>(expected.data(), expected.size()));

  ::phaser::ROSBuffer native_output;
  const auto* native_data =
      reinterpret_cast<const char*>(phaser_message.Data());
  const absl::Span<const char> native_bytes(native_data, phaser_message.Size());
  EXPECT_EQ(::phaser::InferMessageWireFormat(native_bytes),
            ::phaser::MessageWireFormat::kPhaser);
  ASSERT_TRUE(RosCompileMessage::PhaserToROS(native_bytes, native_output).ok());
  EXPECT_EQ(native_output.AsString(), expected);

  ::phaser::ROSBuffer inferred_protobuf_output;
  ASSERT_TRUE(
      RosCompileMessage::ConvertToROS(
          absl::Span<const char>(protobuf_wire.data(), protobuf_wire.size()),
          inferred_protobuf_output)
          .ok());
  EXPECT_EQ(inferred_protobuf_output.AsString(), expected);

  ::phaser::ROSBuffer inferred_native_output;
  ASSERT_TRUE(
      RosCompileMessage::ConvertToROS(native_bytes, inferred_native_output)
          .ok());
  EXPECT_EQ(inferred_native_output.AsString(), expected);
}

TEST(ROSWireConversionTest, DirectPackedFixedFieldsUseCompatibleRawLayout) {
  ::foo::bar::RosPackedFixedMessage protobuf;
  protobuf.add_fixed32_values(0x01020304u);
  protobuf.add_fixed32_values(0xfedcba98u);
  protobuf.add_sfixed32_values(-1);
  protobuf.add_sfixed32_values(-1234567);
  protobuf.add_float_values(1.5f);
  protobuf.add_float_values(-0.0f);
  protobuf.add_fixed64_values(0x0102030405060708ULL);
  protobuf.add_sfixed64_values(-1234567890123LL);
  protobuf.add_double_values(1.25);
  protobuf.add_double_values(-2.5);
  protobuf.add_fixed_array(0xfedcba9876543210ULL);
  protobuf.add_fixed_array(0);
  protobuf.add_fixed_array(0x1122334455667788ULL);

  std::string expected;
  AppendIntegral(expected, uint32_t{2});
  AppendIntegral(expected, uint32_t{0x01020304});
  AppendIntegral(expected, uint32_t{0xfedcba98});
  AppendIntegral(expected, uint32_t{2});
  AppendIntegral(expected, int32_t{-1});
  AppendIntegral(expected, int32_t{-1234567});
  AppendIntegral(expected, uint32_t{2});
  AppendFloat(expected, 1.5f);
  AppendFloat(expected, -0.0f);
  AppendIntegral(expected, uint32_t{1});
  AppendIntegral(expected, uint64_t{0x0102030405060708});
  AppendIntegral(expected, uint32_t{1});
  AppendIntegral(expected, int64_t{-1234567890123LL});
  AppendIntegral(expected, uint32_t{2});
  AppendDouble(expected, 1.25);
  AppendDouble(expected, -2.5);
  AppendIntegral(expected, uint64_t{0xfedcba9876543210});
  AppendIntegral(expected, uint64_t{0});
  AppendIntegral(expected, uint64_t{0x1122334455667788});

  ::phaser::ROSBuffer ros_output;
  ASSERT_TRUE(RosPackedFixedMessage::ProtobufToROS(protobuf.SerializeAsString(),
                                                   ros_output)
                  .ok());
  EXPECT_EQ(ros_output.AsString(), expected);

  std::vector<char> protobuf_storage(1024);
  ::phaser::ProtoBuffer protobuf_output(protobuf_storage.data(),
                                        protobuf_storage.size());
  ASSERT_TRUE(RosPackedFixedMessage::ROSToProtobuf(
                  absl::Span<const char>(expected.data(), expected.size()),
                  protobuf_output)
                  .ok());
  ::foo::bar::RosPackedFixedMessage reparsed;
  ASSERT_TRUE(reparsed.ParseFromArray(protobuf_storage.data(),
                                     int(protobuf_output.Size())));
  EXPECT_EQ(reparsed.SerializeAsString(), protobuf.SerializeAsString());
}

TEST(ROSWireConversionTest, DirectPackedFixedFieldRejectsPartialElement) {
  std::string malformed;
  malformed.push_back(static_cast<char>(0x0a));  // field 1, packed
  malformed.push_back(static_cast<char>(0x03));
  malformed.append("\x01\x02\x03", 3);

  ::phaser::ROSBuffer output;
  EXPECT_FALSE(RosPackedFixedMessage::ProtobufToROS(malformed, output).ok());
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
  EXPECT_FALSE(
      RosCompileMessage::ConvertToROS(absl::Span<const char>("\0", 1), output)
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
  message.stamps.Add(::ros::Time(1, 2));
  message.stamps.Add(::ros::Time(3, 4));
  message.timeouts.Add(::ros::Duration(-5, 6));
  message.fixed_stamps.Set(0, ::ros::Time(7, 8));
  message.fixed_stamps.Set(1, ::ros::Time(9, 10));
  message.fixed_timeouts.Set(0, ::ros::Duration(-11, 12));
  message.fixed_timeouts.Set(1, ::ros::Duration(13, 14));
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
  ASSERT_TRUE(
      RosIntrinsicMessage::PhaserToROS(
          absl::Span<const char>(native_data, message.Size()), native_output)
          .ok());
  EXPECT_EQ(native_output.AsString(), expected);

  ::phaser::ROSBuffer protobuf_frontend_native_output;
  ASSERT_TRUE(ProtobufFrontendIntrinsicMessage::PhaserToROS(
                  absl::Span<const char>(native_data, message.Size()),
                  protobuf_frontend_native_output)
                  .ok());
  EXPECT_EQ(protobuf_frontend_native_output.AsString(), expected);
}

TEST(ROSWireConversionTest, ROSNarrowScalarsUseDeclaredWireWidths) {
  RosNarrowScalars message;
  message.uint8_value = 0xa1;
  message.int8_value = -2;
  message.uint16_value = 0xb2c3;
  message.int16_value = -4;

  std::string expected;
  AppendIntegral(expected, static_cast<uint8_t>(0xa1));
  AppendIntegral(expected, static_cast<int8_t>(-2));
  AppendIntegral(expected, static_cast<uint16_t>(0xb2c3));
  AppendIntegral(expected, static_cast<int16_t>(-4));

  std::string output;
  ASSERT_TRUE(message.SerializeToROSString(&output).ok());
  EXPECT_EQ(output, expected);
  EXPECT_EQ(message.ROSSerializedSize(), expected.size());

  RosNarrowScalars decoded;
  ASSERT_TRUE(
      decoded.ParseFromROS(absl::Span<const char>(output.data(), output.size()))
          .ok());
  EXPECT_EQ(decoded.uint8_value.Get(), 0xa1);
  EXPECT_EQ(decoded.int8_value.Get(), -2);
  EXPECT_EQ(decoded.uint16_value.Get(), 0xb2c3);
  EXPECT_EQ(decoded.int16_value.Get(), -4);

  // The converters between protobuf and ROS use the same widths.
  ::phaser::ROSBuffer protobuf_output;
  ASSERT_TRUE(RosNarrowScalars::ProtobufToROS(message.SerializeAsString(),
                                              protobuf_output)
                  .ok());
  EXPECT_EQ(protobuf_output.AsString(), expected);

  std::vector<char> wire(64);
  ::phaser::ProtoBuffer wire_output(wire.data(), wire.size());
  ASSERT_TRUE(RosNarrowScalars::ROSToProtobuf(
                  absl::Span<const char>(expected.data(), expected.size()),
                  wire_output)
                  .ok());
  ::foo::bar::RosNarrowScalars protobuf;
  ASSERT_TRUE(protobuf.ParseFromArray(wire.data(), int(wire_output.Size())));
  EXPECT_EQ(protobuf.uint8_value(), 0xa1u);
  EXPECT_EQ(protobuf.int8_value(), -2);
  EXPECT_EQ(protobuf.uint16_value(), 0xb2c3u);
  EXPECT_EQ(protobuf.int16_value(), -4);
}

TEST(ROSWireConversionTest, ParsesKnownROSBytesIntoNativePayload) {
  RosCompileMessage message;
  const std::string input = ExpectedRosCompileBytes();
  ASSERT_TRUE(
      message.ParseFromROS(absl::Span<const char>(input.data(), input.size()))
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

  std::vector<char> direct_wire(4096);
  ::phaser::ProtoBuffer direct_output(direct_wire.data(), direct_wire.size());
  ASSERT_TRUE(
      RosCompileMessage::ROSToProtobuf(
          absl::Span<const char>(input.data(), input.size()), direct_output)
          .ok());
  ::foo::bar::RosCompileMessage direct_protobuf;
  ASSERT_TRUE(direct_protobuf.ParseFromArray(direct_wire.data(),
                                            int(direct_output.Size())));
  EXPECT_EQ(direct_protobuf.SerializeAsString(), protobuf.SerializeAsString());
}

TEST(ROSWireConversionTest, ParsedROSPayloadUsesEitherFrontend) {
  RosIntrinsicMessage ros_message;
  const std::string input = ExpectedIntrinsicBytes();
  ASSERT_TRUE(
      ros_message
          .ParseFromROS(absl::Span<const char>(input.data(), input.size()))
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
  ASSERT_EQ(ros_message.stamps.size(), 2u);
  EXPECT_EQ(ros_message.stamps.Get(0), ::ros::Time(1, 2));
  EXPECT_EQ(ros_message.stamps.Get(1), ::ros::Time(3, 4));
  ASSERT_EQ(ros_message.timeouts.size(), 1u);
  EXPECT_EQ(ros_message.timeouts.Get(0), ::ros::Duration(-5, 6));
  EXPECT_EQ(ros_message.fixed_stamps.Get(1), ::ros::Time(9, 10));
  EXPECT_EQ(ros_message.fixed_timeouts.Get(0), ::ros::Duration(-11, 12));

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
  // The ROS frontend wrote these as ROS values; the protobuf frontend reads
  // the same payload back as ordinary repeated Timestamp and Duration.
  ASSERT_EQ(protobuf_view.stamps_size(), 2);
  EXPECT_EQ(protobuf_view.stamps(1).seconds(), 3);
  EXPECT_EQ(protobuf_view.stamps(1).nanos(), 4);
  ASSERT_EQ(protobuf_view.timeouts_size(), 1);
  EXPECT_EQ(protobuf_view.timeouts(0).seconds(), -5);
  EXPECT_EQ(protobuf_view.fixed_stamps(0).seconds(), 7);
  EXPECT_EQ(protobuf_view.fixed_timeouts(1).nanos(), 14);

  ProtobufFrontendIntrinsicMessage parsed_protobuf_frontend;
  ASSERT_TRUE(
      parsed_protobuf_frontend
          .ParseFromROS(absl::Span<const char>(input.data(), input.size()))
          .ok());
  EXPECT_EQ(parsed_protobuf_frontend.stamp().seconds(), 12);
  EXPECT_EQ(parsed_protobuf_frontend.timeout().nanos(), 500);
  EXPECT_EQ(parsed_protobuf_frontend.header().stamp().nanos(), 654);
  EXPECT_EQ(parsed_protobuf_frontend.header().frame_id(), "map");

  std::vector<char> direct_wire(4096);
  ::phaser::ProtoBuffer direct_output(direct_wire.data(), direct_wire.size());
  ASSERT_TRUE(
      RosIntrinsicMessage::ROSToProtobuf(
          absl::Span<const char>(input.data(), input.size()), direct_output)
          .ok());
  ProtobufFrontendIntrinsicMessage direct_protobuf;
  ASSERT_TRUE(
      direct_protobuf.ParseFromArray(direct_wire.data(), direct_output.Size()));
  EXPECT_EQ(direct_protobuf.stamp().seconds(), 12);
  EXPECT_EQ(direct_protobuf.timeout().nanos(), 500);
  EXPECT_EQ(direct_protobuf.header().frame_id(), "map");
}

TEST(ROSWireConversionTest, ParsesScalarAndMessageOneofArms) {
  std::string scalar_input = ExpectedRosCompileBytes(false);
  scalar_input.resize(scalar_input.size() - sizeof(uint32_t));
  AppendIntegral(scalar_input, static_cast<uint32_t>(15));
  AppendIntegral(scalar_input, int32_t{123});

  RosCompileMessage scalar_message;
  ASSERT_TRUE(scalar_message
                  .ParseFromROS(absl::Span<const char>(scalar_input.data(),
                                                       scalar_input.size()))
                  .ok());
  using ChoiceCount = RosCompileMessage::ChoiceCountAlternative;
  ASSERT_TRUE(scalar_message.choice.holds_alternative<ChoiceCount>());
  EXPECT_EQ(scalar_message.choice.get<ChoiceCount>(), 123);

  std::string message_input = ExpectedRosCompileBytes(false);
  message_input.resize(message_input.size() - sizeof(uint32_t));
  AppendIntegral(message_input, static_cast<uint32_t>(18));
  AppendIntegral(message_input, int32_t{456});

  RosCompileMessage message;
  ASSERT_TRUE(message
                  .ParseFromROS(absl::Span<const char>(message_input.data(),
                                                       message_input.size()))
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
  EXPECT_FALSE(trailing_message
                   .ParseFromROS(
                       absl::Span<const char>(trailing.data(), trailing.size()))
                   .ok());

  std::string invalid_length = valid;
  invalid_length[4] = static_cast<char>(0xff);
  invalid_length[5] = static_cast<char>(0xff);
  invalid_length[6] = static_cast<char>(0xff);
  invalid_length[7] = static_cast<char>(0x7f);
  RosCompileMessage invalid_length_message;
  EXPECT_FALSE(invalid_length_message
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
  AppendIntegral(oversized_sequence, int32_t{0});
  AppendString(oversized_sequence, "");
  AppendIntegral(oversized_sequence, static_cast<uint8_t>(0));
  AppendDouble(oversized_sequence, 0);
  AppendIntegral(oversized_sequence, int32_t{0});
  AppendIntegral(oversized_sequence, int32_t{0});
  AppendIntegral(oversized_sequence, static_cast<uint32_t>(100));
  RosCompileMessage oversized_sequence_message;
  EXPECT_FALSE(oversized_sequence_message
                   .ParseFromROS(absl::Span<const char>(
                       oversized_sequence.data(), oversized_sequence.size()))
                   .ok());
  EXPECT_TRUE(oversized_sequence_message.xs.empty());
}

using DirectElement = ::foo::bar::phaser::DirectElement;
using DirectElements = ::foo::bar::phaser::DirectElements;
using DirectKind = ::foo::bar::phaser::DirectKind;
using ProtobufDirectElement = ::foo::bar::pb::protobuf_phaser::DirectElement;
using ProtobufDirectElements = ::foo::bar::pb::protobuf_phaser::DirectElements;
using ProtobufDirectKind = ::foo::bar::pb::protobuf_phaser::DirectKind;
using DirectFixedLeaf = ::foo::bar::phaser::DirectFixedLeaf;
using DirectLeaf = ::foo::bar::phaser::DirectLeaf;
using ProtobufDirectFixedLeaf =
    ::foo::bar::pb::protobuf_phaser::DirectFixedLeaf;
using ProtobufDirectLeaf = ::foo::bar::pb::protobuf_phaser::DirectLeaf;
using DirectBranch = ::foo::bar::phaser::DirectBranch;
using ProtobufDirectBranch = ::foo::bar::pb::protobuf_phaser::DirectBranch;

// Every third element leaves its singular messages unset.
bool HasMessages(int i) { return i % 3 != 0; }

void PopulateDirectElement(DirectElement& element, int i, int depth) {
  element.id = i;
  element.label = "element " + std::to_string(i);
  element.flag = i % 2 == 0;
  element.value = i * 0.25;
  element.big = uint64_t{1} << (32 + i % 30);
  element.small = static_cast<uint32_t>(200 + i % 50);
  element.level = -300 - i;
  element.ratio = static_cast<float>(i) / 8;
  element.kind = DirectKind::DIRECT_KIND_SECOND;
  element.blob = std::string("\0\1\2", 3);
  if (HasMessages(i)) {
    element.stamp = ::ros::Time(static_cast<uint32_t>(i), 7);
    element.timeout = ::ros::Duration(-i, 8);
    element.header->seq = static_cast<uint32_t>(i + 1);
    element.header->stamp = ::ros::Time(9, static_cast<uint32_t>(i));
    element.header->frame_id = "frame";
    element.child->id = i * 2;
    element.child->label = "child";
  }
  for (int j = 0; j < i % 4; ++j) {
    element.samples.push_back(i * 100 + j);
    element.octets.push_back(static_cast<uint32_t>(250 + j));
    element.flags.push_back(j % 2 == 1);
    element.kinds.push_back(DirectKind::DIRECT_KIND_FIRST);
    element.tags.push_back("tag " + std::to_string(j));
    auto child = element.children.Add();
    child.id = j;
    child.label = j == 0 ? "" : "c";
    element.stamps.Add(::ros::Time(static_cast<uint32_t>(j), 1));
    element.timeouts.Add(::ros::Duration(-j, 2));
    auto stamped = element.stamped.Add();
    stamped.stamp = ::ros::Time(3, static_cast<uint32_t>(j));
    stamped.header->frame_id = "stamped";
  }
  if (depth > 0) {
    for (int j = 0; j < 2; ++j) {
      auto nested = element.nested.Add();
      PopulateDirectElement(nested, i * 10 + j, depth - 1);
    }
  }
}

void PopulateProtobufDirectElement(ProtobufDirectElement& element, int i,
                                   int depth) {
  element.set_id(i);
  element.set_label("element " + std::to_string(i));
  element.set_flag(i % 2 == 0);
  element.set_value(i * 0.25);
  element.set_big(uint64_t{1} << (32 + i % 30));
  element.set_small(static_cast<uint32_t>(200 + i % 50));
  element.set_level(-300 - i);
  element.set_ratio(static_cast<float>(i) / 8);
  element.set_kind(ProtobufDirectKind::DIRECT_KIND_SECOND);
  element.set_blob(std::string("\0\1\2", 3));
  if (HasMessages(i)) {
    element.mutable_stamp()->set_seconds(i);
    element.mutable_stamp()->set_nanos(7);
    element.mutable_timeout()->set_seconds(-i);
    element.mutable_timeout()->set_nanos(8);
    element.mutable_header()->set_seq(static_cast<uint32_t>(i + 1));
    element.mutable_header()->mutable_stamp()->set_seconds(9);
    element.mutable_header()->mutable_stamp()->set_nanos(i);
    element.mutable_header()->set_frame_id("frame");
    element.mutable_child()->set_id(i * 2);
    element.mutable_child()->set_label("child");
  }
  for (int j = 0; j < i % 4; ++j) {
    element.add_samples(i * 100 + j);
    element.add_octets(static_cast<uint32_t>(250 + j));
    element.add_flags(j % 2 == 1);
    element.add_kinds(ProtobufDirectKind::DIRECT_KIND_FIRST);
    element.add_tags("tag " + std::to_string(j));
    auto child = element.add_children();
    child.set_id(j);
    child.set_label(j == 0 ? "" : "c");
    auto stamp = element.add_stamps();
    stamp.set_seconds(j);
    stamp.set_nanos(1);
    auto timeout = element.add_timeouts();
    timeout.set_seconds(-j);
    timeout.set_nanos(2);
    auto stamped = element.add_stamped();
    stamped.mutable_stamp()->set_seconds(3);
    stamped.mutable_stamp()->set_nanos(j);
    stamped.mutable_header()->set_frame_id("stamped");
  }
  if (depth > 0) {
    for (int j = 0; j < 2; ++j) {
      auto nested = element.add_nested();
      PopulateProtobufDirectElement(nested, i * 10 + j, depth - 1);
    }
  }
}

// Leaf 0 holds only zeros and empty strings.
void PopulateFixedLeaf(DirectFixedLeaf& leaf, int i) {
  leaf.id = -i;
  leaf.flag = i % 2 == 1;
  leaf.value = i * 1.5;
  leaf.big = static_cast<uint64_t>(i) << 40;
  leaf.small = static_cast<uint32_t>(i * 50 % 256);
  leaf.level = -100 * i;
  leaf.ratio = static_cast<float>(i) / 4;
  leaf.kind = static_cast<DirectKind>(i % 3);
  leaf.marker = DirectKind::DIRECT_KIND_UNSPECIFIED;
  leaf.delta = -(int64_t{i} << 33);
}

void PopulateProtobufFixedLeaf(ProtobufDirectFixedLeaf& leaf, int i) {
  leaf.set_id(-i);
  leaf.set_flag(i % 2 == 1);
  leaf.set_value(i * 1.5);
  leaf.set_big(static_cast<uint64_t>(i) << 40);
  leaf.set_small(static_cast<uint32_t>(i * 50 % 256));
  leaf.set_level(-100 * i);
  leaf.set_ratio(static_cast<float>(i) / 4);
  leaf.set_kind(static_cast<ProtobufDirectKind>(i % 3));
  leaf.set_marker(ProtobufDirectKind::DIRECT_KIND_UNSPECIFIED);
  leaf.set_delta(-(int64_t{i} << 33));
}

void PopulateLeaf(DirectLeaf& leaf, int i) {
  leaf.name = i == 0 ? std::string() : "leaf " + std::to_string(i);
  leaf.count = static_cast<uint32_t>(i);
  leaf.payload = std::string(static_cast<size_t>(i % 3), '\xff');
  leaf.kind = static_cast<DirectKind>(i % 3);
}

void PopulateProtobufLeaf(ProtobufDirectLeaf& leaf, int i) {
  leaf.set_name(i == 0 ? std::string() : "leaf " + std::to_string(i));
  leaf.set_count(static_cast<uint32_t>(i));
  leaf.set_payload(std::string(static_cast<size_t>(i % 3), '\xff'));
  leaf.set_kind(static_cast<ProtobufDirectKind>(i % 3));
}

// Branch 0 holds only zeros, empty strings and empty vectors.
void PopulateBranch(DirectBranch& branch, int i, int depth) {
  branch.id = i;
  branch.label = i == 0 ? std::string() : "branch " + std::to_string(i);
  branch.stamp = ::ros::Time(static_cast<uint32_t>(i), 7);
  branch.timeout = ::ros::Duration(-i, 8);
  branch.header->seq = static_cast<uint32_t>(i);
  branch.header->stamp = ::ros::Time(9, static_cast<uint32_t>(i));
  branch.header->frame_id = i == 0 ? "" : "frame";
  PopulateLeaf(*branch.leaf, i);
  PopulateDirectElement(*branch.element, i, 0);
  for (int j = 0; j < i % 4; ++j) {
    branch.samples.push_back(i * 100 + j);
    branch.octets.push_back(static_cast<uint32_t>(250 + j));
    branch.flags.push_back(j % 2 == 1);
    branch.kinds.push_back(static_cast<DirectKind>(j % 3));
    auto fixed_leaf = branch.fixed_leaves.Add();
    PopulateFixedLeaf(fixed_leaf, j);
    branch.stamps.Add(::ros::Time(static_cast<uint32_t>(j), 1));
  }
  if (i % 4 == 1) {
    auto element = branch.elements.Add();
    PopulateDirectElement(element, i + 1, 0);
  }
  if (depth > 0) {
    for (int j = 0; j < 2; ++j) {
      auto nested = branch.branches.Add();
      PopulateBranch(nested, i * 10 + j + 1, depth - 1);
    }
  }
}

void PopulateProtobufBranch(ProtobufDirectBranch& branch, int i, int depth) {
  branch.set_id(i);
  branch.set_label(i == 0 ? std::string() : "branch " + std::to_string(i));
  branch.mutable_stamp()->set_seconds(i);
  branch.mutable_stamp()->set_nanos(7);
  branch.mutable_timeout()->set_seconds(-i);
  branch.mutable_timeout()->set_nanos(8);
  branch.mutable_header()->set_seq(static_cast<uint32_t>(i));
  branch.mutable_header()->mutable_stamp()->set_seconds(9);
  branch.mutable_header()->mutable_stamp()->set_nanos(i);
  branch.mutable_header()->set_frame_id(i == 0 ? "" : "frame");
  PopulateProtobufLeaf(*branch.mutable_leaf(), i);
  PopulateProtobufDirectElement(*branch.mutable_element(), i, 0);
  for (int j = 0; j < i % 4; ++j) {
    branch.add_samples(i * 100 + j);
    branch.add_octets(static_cast<uint32_t>(250 + j));
    branch.add_flags(j % 2 == 1);
    branch.add_kinds(static_cast<ProtobufDirectKind>(j % 3));
    auto fixed_leaf = branch.add_fixed_leaves();
    PopulateProtobufFixedLeaf(fixed_leaf, j);
    auto stamp = branch.add_stamps();
    stamp.set_seconds(j);
    stamp.set_nanos(1);
  }
  if (i % 4 == 1) {
    auto element = branch.add_elements();
    PopulateProtobufDirectElement(element, i + 1, 0);
  }
  if (depth > 0) {
    for (int j = 0; j < 2; ++j) {
      auto nested = branch.add_branches();
      PopulateProtobufBranch(nested, i * 10 + j + 1, depth - 1);
    }
  }
}

// Enough elements to fill several chunks of the vector.
constexpr int kDirectElementCount = 40;

TEST(ROSWireConversionTest, MutableVectorElementsMatchTheHandlePath) {
  DirectElements message;
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto element = message.elements.Add();
    PopulateDirectElement(element, i, 1);
  }
  for (int i = 0; i < 3; ++i) {
    auto handled = message.handled.Add();
    handled.count = i;
    handled.name = "handled";
    handled.stamp = ::ros::Time(static_cast<uint32_t>(i), 5);
    handled.children.Add()->id = i;
  }
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto fixed_leaf = message.fixed_leaves.Add();
    PopulateFixedLeaf(fixed_leaf, i);
    auto leaf = message.leaves.Add();
    PopulateLeaf(leaf, i);
  }

  ::phaser::ROSBuffer direct_output;
  ASSERT_TRUE(message.SerializeToROS(direct_output).ok());
  const std::string expected = direct_output.AsString();
  EXPECT_EQ(message.ROSSerializedSize(), expected.size());

  // A readonly message reads each element through a handle.
  const auto* native_data = reinterpret_cast<const char*>(message.Data());
  const absl::Span<const char> native_bytes(native_data, message.Size());
  ::phaser::ROSBuffer native_output;
  ASSERT_TRUE(DirectElements::PhaserToROS(native_bytes, native_output).ok());
  EXPECT_EQ(native_output.AsString(), expected);

  ::phaser::ROSBuffer protobuf_output;
  ASSERT_TRUE(
      DirectElements::ProtobufToROS(message.SerializeAsString(), protobuf_output)
          .ok());
  EXPECT_EQ(protobuf_output.AsString(), expected);

  std::vector<char> protobuf_wire(expected.size() * 4 + 4096);
  ::phaser::ProtoBuffer protobuf_wire_output(protobuf_wire.data(),
                                             protobuf_wire.size());
  ASSERT_TRUE(DirectElements::ROSToProtobuf(
                  absl::Span<const char>(expected.data(), expected.size()),
                  protobuf_wire_output)
                  .ok());
  DirectElements from_protobuf;
  ASSERT_TRUE(from_protobuf.ParseFromArray(protobuf_wire.data(),
                                           protobuf_wire_output.Size()));
  ::phaser::ROSBuffer from_protobuf_output;
  ASSERT_TRUE(from_protobuf.SerializeToROS(from_protobuf_output).ok());
  EXPECT_EQ(from_protobuf_output.AsString(), expected);

  // The protobuf frontend stores its nested messages through offsets.
  ProtobufDirectElements protobuf_message;
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto element = protobuf_message.add_elements();
    PopulateProtobufDirectElement(element, i, 1);
  }
  for (int i = 0; i < 3; ++i) {
    auto handled = protobuf_message.add_handled();
    handled.set_count(i);
    handled.set_name("handled");
    handled.mutable_stamp()->set_seconds(i);
    handled.mutable_stamp()->set_nanos(5);
    handled.add_children().set_id(i);
  }
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto fixed_leaf = protobuf_message.add_fixed_leaves();
    PopulateProtobufFixedLeaf(fixed_leaf, i);
    auto leaf = protobuf_message.add_leaves();
    PopulateProtobufLeaf(leaf, i);
  }
  std::string protobuf_frontend_output;
  ASSERT_TRUE(
      protobuf_message.SerializeToROSString(&protobuf_frontend_output).ok());
  EXPECT_EQ(protobuf_frontend_output, expected);
  EXPECT_EQ(protobuf_message.ROSSerializedSize(), expected.size());

  DirectElements decoded;
  ASSERT_TRUE(
      decoded.ParseFromROS(absl::Span<const char>(expected.data(),
                                                  expected.size()))
          .ok());
  ASSERT_EQ(decoded.elements.size(), size_t{kDirectElementCount});
  ASSERT_EQ(decoded.fixed_leaves.size(), size_t{kDirectElementCount});
  ASSERT_EQ(decoded.leaves.size(), size_t{kDirectElementCount});
  std::string reencoded;
  ASSERT_TRUE(decoded.SerializeToROSString(&reencoded).ok());
  EXPECT_EQ(reencoded, expected);

  ProtobufDirectElements protobuf_decoded;
  ASSERT_TRUE(protobuf_decoded
                  .ParseFromROS(absl::Span<const char>(expected.data(),
                                                       expected.size()))
                  .ok());
  std::string protobuf_reencoded;
  ASSERT_TRUE(protobuf_decoded.SerializeToROSString(&protobuf_reencoded).ok());
  EXPECT_EQ(protobuf_reencoded, expected);
}

TEST(ROSWireConversionTest, InsertedElementsMatchAppendedOnes) {
  DirectElements appended;
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto element = appended.elements.Add();
    PopulateDirectElement(element, i, 1);
  }

  // The same elements, inserted in another order at the positions that put
  // each in the same place, so that the insertions move elements across the
  // vector's chunks.
  DirectElements inserted;
  std::vector<int> order;
  for (int n = 0; n < kDirectElementCount; ++n) {
    const int i = n * 17 % kDirectElementCount;
    const auto at = std::lower_bound(order.begin(), order.end(), i);
    const auto pos = std::next(inserted.elements.begin(), at - order.begin());
    order.insert(at, i);
    if (n % 3 == 0) {
      auto element = *inserted.elements.emplace(pos);
      PopulateDirectElement(element, i, 1);
      continue;
    }
    DirectElement source;
    PopulateDirectElement(source, i, 1);
    const auto element = n % 3 == 1 ? inserted.elements.insert(pos, source)
                                    : inserted.elements.emplace(pos, source);
    EXPECT_EQ((*element).id.Get(), i);
  }

  std::string expected;
  ASSERT_TRUE(appended.SerializeToROSString(&expected).ok());
  std::string actual;
  ASSERT_TRUE(inserted.SerializeToROSString(&actual).ok());
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(inserted.SerializeAsString(), appended.SerializeAsString());
}

TEST(ROSWireConversionTest, InsertedElementOfTheSameVectorIsCopied) {
  // Large enough that copying it grows the buffer.
  const std::string label(1 << 20, 'x');
  DirectElements message;
  for (int i = 0; i < 10; ++i) {
    auto element = message.elements.Add();
    PopulateDirectElement(element, i, 1);
  }
  message.elements[7].label = label;
  message.elements.insert(std::next(message.elements.begin(), 2),
                          message.elements[7]);

  DirectElements expected;
  for (const int i : {0, 1, 7, 2, 3, 4, 5, 6, 7, 8, 9}) {
    auto element = expected.elements.Add();
    PopulateDirectElement(element, i, 1);
    if (i == 7) {
      element.label = label;
    }
  }
  std::string expected_wire;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_wire).ok());
  std::string actual_wire;
  ASSERT_TRUE(message.SerializeToROSString(&actual_wire).ok());
  EXPECT_EQ(actual_wire, expected_wire);
}

TEST(ROSWireConversionTest, InsertedHandleKeepsItsChangesWithItsElement) {
  const ::ros::Time stamp(77, 88);
  DirectElements message;
  for (int i = 0; i < 4; ++i) {
    auto element = message.elements.Add();
    PopulateDirectElement(element, i, 0);
  }
  {
    // The handle holds its stamp until it is destroyed, after its element
    // has moved.
    auto element = message.elements[2];
    element.stamp = stamp;
    message.elements.insert(message.elements.begin(), element);
  }

  DirectElements expected;
  for (const int i : {2, 0, 1, 2, 3}) {
    auto element = expected.elements.Add();
    PopulateDirectElement(element, i, 0);
    if (i == 2) {
      element.stamp = stamp;
    }
  }
  std::string expected_wire;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_wire).ok());
  std::string actual_wire;
  ASSERT_TRUE(message.SerializeToROSString(&actual_wire).ok());
  EXPECT_EQ(actual_wire, expected_wire);
}

TEST(ROSWireConversionTest, ElementsCopiedFromTheSameBuffer) {
  // Long enough that each copy grows the buffer.
  const std::string label(100000, 'l');
  DirectElements message;
  {
    auto element = message.elements.Add();
    PopulateDirectElement(element, 1, 1);
    element.label = label;
  }
  for (int i = 0; i < 3; ++i) {
    message.elements.push_back(message.elements[0]);
  }
  message.elements[1] = message.elements[2];

  DirectElements expected;
  for (int i = 0; i < 4; ++i) {
    auto element = expected.elements.Add();
    PopulateDirectElement(element, 1, 1);
    element.label = label;
  }
  std::string expected_wire;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_wire).ok());
  std::string actual_wire;
  ASSERT_TRUE(message.SerializeToROSString(&actual_wire).ok());
  EXPECT_EQ(actual_wire, expected_wire);
}

TEST(ROSWireConversionTest, ErasedElementsLeaveTheOthers) {
  DirectElements message;
  std::vector<int> ids;
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto element = message.elements.Add();
    PopulateDirectElement(element, i, 1);
    ids.push_back(i);
  }
  // Single elements, runs and an empty run, over the vector's chunks.
  const std::pair<size_t, size_t> erasures[] = {
      {0, 1}, {3, 7}, {10, 1}, {5, 0}, {1, 15}, {15, 1}, {2, 4}};
  for (const auto& [first, count] : erasures) {
    const auto at = static_cast<ptrdiff_t>(first);
    const auto end = static_cast<ptrdiff_t>(first + count);
    const auto next =
        message.elements.erase(std::next(message.elements.begin(), at),
                               std::next(message.elements.begin(), end));
    ids.erase(ids.begin() + at, ids.begin() + end);
    if (first < ids.size()) {
      EXPECT_EQ((*next).id.Get(), ids[first]);
    }
  }

  DirectElements expected;
  for (const int i : ids) {
    auto element = expected.elements.Add();
    PopulateDirectElement(element, i, 1);
  }
  std::string expected_wire;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_wire).ok());
  std::string actual_wire;
  ASSERT_TRUE(message.SerializeToROSString(&actual_wire).ok());
  EXPECT_EQ(actual_wire, expected_wire);
  EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
}

TEST(ROSWireConversionTest, SwapInOneBufferMatchesSwapBetweenBuffers) {
  // The vectors of element 4 are empty and those of element 7 are not.
  DirectElements message;
  for (const int i : {4, 7}) {
    auto element = message.elements.Add();
    PopulateDirectElement(element, i, 1);
  }
  DirectElements expected;
  ASSERT_TRUE(expected.CloneFrom(message).ok());

  const auto swap_vectors = [](auto&& a, auto&& b) {
    a.samples.swap(b.samples);
    a.octets.swap(b.octets);
    a.kinds.swap(b.kinds);
    a.tags.swap(b.tags);
    a.children.swap(b.children);
    a.stamped.swap(b.stamped);
    a.nested.swap(b.nested);
  };
  // In one buffer, the vectors are exchanged without being copied.
  const size_t size = message.Size();
  swap_vectors(message.elements[0], message.elements[1]);
  EXPECT_EQ(message.Size(), size);
  EXPECT_EQ(message.elements[0].tags.size(), 3u);
  EXPECT_TRUE(message.elements[1].tags.empty());
  EXPECT_EQ(message.elements[0].nested[0].id.Get(), 70);
  EXPECT_EQ(message.elements[1].nested[0].id.Get(), 40);

  // Between buffers, copies of them are.
  {
    DirectElement first;
    ASSERT_TRUE(first.CloneFrom(expected.elements[0]).ok());
    DirectElement second;
    ASSERT_TRUE(second.CloneFrom(expected.elements[1]).ok());
    swap_vectors(expected.elements[0], second);
    swap_vectors(expected.elements[1], first);
  }

  std::string expected_wire;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_wire).ok());
  std::string actual_wire;
  ASSERT_TRUE(message.SerializeToROSString(&actual_wire).ok());
  EXPECT_EQ(actual_wire, expected_wire);
  EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
}

TEST(ROSWireConversionTest, LeafElementsParseLikeTheirSetters) {
  ProtobufDirectElements original;
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto fixed_leaf = original.add_fixed_leaves();
    PopulateProtobufFixedLeaf(fixed_leaf, i);
    auto leaf = original.add_leaves();
    PopulateProtobufLeaf(leaf, i);
  }
  std::string ros;
  ASSERT_TRUE(original.SerializeToROSString(&ros).ok());
  const std::string protobuf = original.SerializeAsString();

  // Parsing marks the fields present, as the setters do.
  ProtobufDirectElements decoded;
  ASSERT_TRUE(
      decoded.ParseFromROS(absl::Span<const char>(ros.data(), ros.size()))
          .ok());
  EXPECT_EQ(decoded.SerializeAsString(), protobuf);
  DirectElements ros_decoded;
  ASSERT_TRUE(
      ros_decoded.ParseFromROS(absl::Span<const char>(ros.data(), ros.size()))
          .ok());
  EXPECT_EQ(ros_decoded.SerializeAsString(), protobuf);

  // Each truncation is in its own allocation, so a read past it is caught
  // under ASan.  Every one fails, some before and some after the elements
  // that fit are added.
  for (size_t size = 0; size < ros.size(); ++size) {
    const std::vector<char> truncated(ros.begin(),
                                      ros.begin() + static_cast<std::ptrdiff_t>(size));
    ProtobufDirectElements partial;
    EXPECT_FALSE(partial
                     .ParseFromROS(absl::Span<const char>(truncated.data(),
                                                          truncated.size()))
                     .ok())
        << size;
  }
}

TEST(ROSWireConversionTest, ParsedLeavesAppendAfterExistingChunks) {
  constexpr int kExisting = 3;
  DirectElements expected;
  DirectElements tail;
  DirectElements message;
  for (int i = 0; i < kDirectElementCount; ++i) {
    for (DirectElements* target : {&expected, i < kExisting ? &message : &tail}) {
      auto fixed_leaf = target->fixed_leaves.Add();
      PopulateFixedLeaf(fixed_leaf, i);
      auto leaf = target->leaves.Add();
      PopulateLeaf(leaf, i);
    }
  }
  std::string tail_ros;
  ASSERT_TRUE(tail.SerializeToROSString(&tail_ros).ok());

  // Add() left the existing elements in several small chunks, so the parsed
  // ones start partway through the chunks.
  ::phaser::ROSReader reader(tail_ros);
  ASSERT_TRUE(message.DeserializeFromROSUncleared(reader).ok());
  EXPECT_TRUE(reader.Eof());
  ASSERT_EQ(message.fixed_leaves.size(), size_t{kDirectElementCount});
  std::string actual;
  std::string want;
  ASSERT_TRUE(message.SerializeToROSString(&actual).ok());
  ASSERT_TRUE(expected.SerializeToROSString(&want).ok());
  EXPECT_EQ(actual, want);
  EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
}

// PopulateFixedLeaf() through the PhaserWriter of either frontend.
template <typename Kind, typename Writer>
void WriteFixedLeaf(Writer leaf, int i) {
  leaf.set_id(-i);
  leaf.set_flag(i % 2 == 1);
  leaf.set_value(i * 1.5);
  leaf.set_big(static_cast<uint64_t>(i) << 40);
  leaf.set_small(static_cast<uint32_t>(i * 50 % 256));
  leaf.set_level(-100 * i);
  leaf.set_ratio(static_cast<float>(i) / 4);
  leaf.set_kind(static_cast<Kind>(i % 3));
  leaf.set_marker(Kind::DIRECT_KIND_UNSPECIFIED);
  leaf.set_delta(-(int64_t{i} << 33));
}

TEST(ROSWireConversionTest, WrittenLeavesMatchTheirSetters) {
  constexpr int kFirst = 3;
  DirectElements expected;
  ProtobufDirectElements protobuf_expected;
  for (int i = 0; i < kDirectElementCount; ++i) {
    auto fixed_leaf = expected.fixed_leaves.Add();
    PopulateFixedLeaf(fixed_leaf, i);
    auto protobuf_leaf = protobuf_expected.add_fixed_leaves();
    PopulateProtobufFixedLeaf(protobuf_leaf, i);
  }

  // The second Add() starts partway through the first chunk and goes on into
  // new ones.
  DirectElements message;
  message.fixed_leaves.reserve(kFirst + 2);
  message.fixed_leaves.Add(kFirst, [](auto leaf, size_t i) {
    WriteFixedLeaf<DirectKind>(leaf, static_cast<int>(i));
  });
  message.fixed_leaves.Add(kDirectElementCount - kFirst,
                           [](auto leaf, size_t i) {
                             WriteFixedLeaf<DirectKind>(
                                 leaf, kFirst + static_cast<int>(i));
                           });
  ProtobufDirectElements protobuf_message;
  protobuf_message.add_fixed_leaves(
      kDirectElementCount, [](auto leaf, size_t i) {
        WriteFixedLeaf<ProtobufDirectKind>(leaf, static_cast<int>(i));
      });

  ASSERT_EQ(message.fixed_leaves.size(), size_t{kDirectElementCount});
  ASSERT_EQ(protobuf_message.fixed_leaves_size(),
            size_t{kDirectElementCount});
  std::string actual;
  std::string want;
  ASSERT_TRUE(message.SerializeToROSString(&actual).ok());
  ASSERT_TRUE(expected.SerializeToROSString(&want).ok());
  EXPECT_EQ(actual, want);
  std::string protobuf_actual;
  ASSERT_TRUE(protobuf_message.SerializeToROSString(&protobuf_actual).ok());
  EXPECT_EQ(protobuf_actual, want);
  // The protobuf encoding includes only the fields marked present.
  EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
  EXPECT_EQ(protobuf_message.SerializeAsString(),
            protobuf_expected.SerializeAsString());
}

TEST(ROSWireConversionTest, BranchElementsParseLikeTheHandlePath) {
  constexpr int kBranchCount = 6;
  DirectElements message;
  ProtobufDirectElements protobuf_message;
  for (int i = 0; i < kBranchCount; ++i) {
    auto branch = message.branches.Add();
    PopulateBranch(branch, i, 1);
    auto protobuf_branch = protobuf_message.add_branches();
    PopulateProtobufBranch(protobuf_branch, i, 1);
  }
  std::string ros;
  ASSERT_TRUE(message.SerializeToROSString(&ros).ok());
  std::string protobuf_ros;
  ASSERT_TRUE(protobuf_message.SerializeToROSString(&protobuf_ros).ok());
  ASSERT_EQ(protobuf_ros, ros);

  const absl::Span<const char> input(ros.data(), ros.size());
  DirectElements decoded;
  ASSERT_TRUE(decoded.ParseFromROS(input).ok());
  ASSERT_EQ(decoded.branches.size(), size_t{kBranchCount});
  std::string reencoded;
  ASSERT_TRUE(decoded.SerializeToROSString(&reencoded).ok());
  EXPECT_EQ(reencoded, ros);
  ProtobufDirectElements protobuf_decoded;
  ASSERT_TRUE(protobuf_decoded.ParseFromROS(input).ok());
  std::string protobuf_reencoded;
  ASSERT_TRUE(protobuf_decoded.SerializeToROSString(&protobuf_reencoded).ok());
  EXPECT_EQ(protobuf_reencoded, ros);

  // DeserializeFromROSUncleared() reads a message's own fields through
  // handles, so it stores each as the field setters do.
  for (int i = 0; i < kBranchCount; ++i) {
    std::string branch_ros;
    ASSERT_TRUE(message.branches[i].SerializeToROSString(&branch_ros).ok());
    const absl::Span<const char> branch_input(branch_ros.data(),
                                              branch_ros.size());
    DirectBranch handled;
    ::phaser::ROSReader reader(branch_ros);
    ASSERT_TRUE(handled.DeserializeFromROSUncleared(reader).ok());
    EXPECT_EQ(decoded.branches[i].SerializeAsString(),
              handled.SerializeAsString())
        << i;
    DirectBranch root;
    ASSERT_TRUE(root.ParseFromROS(branch_input).ok());
    EXPECT_EQ(root.SerializeAsString(), handled.SerializeAsString()) << i;
    ProtobufDirectBranch protobuf_handled;
    ::phaser::ROSReader protobuf_reader(branch_ros);
    ASSERT_TRUE(protobuf_handled.DeserializeFromROSUncleared(protobuf_reader)
                    .ok());
    EXPECT_EQ(protobuf_decoded.branches(static_cast<size_t>(i))
                  .SerializeAsString(),
              protobuf_handled.SerializeAsString())
        << i;
    ProtobufDirectBranch protobuf_root;
    ASSERT_TRUE(protobuf_root.ParseFromROS(branch_input).ok());
    EXPECT_EQ(protobuf_root.SerializeAsString(),
              protobuf_handled.SerializeAsString())
        << i;
  }

  // Each truncation is in its own allocation, so a read past it is caught
  // under ASan.
  for (size_t size = 0; size < ros.size(); ++size) {
    const std::vector<char> truncated(
        ros.begin(), ros.begin() + static_cast<std::ptrdiff_t>(size));
    const absl::Span<const char> partial_input(truncated.data(),
                                               truncated.size());
    DirectElements partial;
    EXPECT_FALSE(partial.ParseFromROS(partial_input).ok()) << size;
    ProtobufDirectElements protobuf_partial;
    EXPECT_FALSE(protobuf_partial.ParseFromROS(partial_input).ok()) << size;
  }
}

TEST(ROSWireConversionTest, MutableVectorElementsReportShortOutput) {
  DirectElements message;
  for (int i = 0; i < 4; ++i) {
    auto element = message.elements.Add();
    PopulateDirectElement(element, i, 1);
    auto fixed_leaf = message.fixed_leaves.Add();
    PopulateFixedLeaf(fixed_leaf, i);
  }
  const size_t size = message.ROSSerializedSize();
  std::vector<char> output(size);
  ASSERT_TRUE(message.SerializeToROSArray(output.data(), output.size()).ok());
  for (size_t short_size : {size_t{1}, size_t{5}, size / 2, size - 1}) {
    std::vector<char> too_small(short_size);
    EXPECT_FALSE(
        message.SerializeToROSArray(too_small.data(), too_small.size()).ok())
        << short_size;
  }
}

// A main message owns everything allocated in its buffer after it was made,
// so clearing it frees all of that at once.
TEST(ROSWireConversionTest, ClearingAMainMessageResetsItsBuffer) {
  DirectBranch fresh;
  ::phaser::ROSBuffer fresh_ros;
  ASSERT_TRUE(fresh.SerializeToROS(fresh_ros).ok());

  DirectBranch message;
  const size_t made_size = message.Size();
  for (int round = 0; round < 3; ++round) {
    PopulateBranch(message, round + 1, 2);
    DirectBranch expected;
    PopulateBranch(expected, round + 1, 2);
    EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
    EXPECT_GT(message.Size(), made_size);

    // Receiving clears twice, the second time with nothing to free.
    for (int clear = 0; clear < 2; ++clear) {
      message.Clear();
      EXPECT_EQ(message.Size(), made_size);
      EXPECT_EQ(message.SerializeAsString(), fresh.SerializeAsString());
      ::phaser::ROSBuffer cleared_ros;
      ASSERT_TRUE(message.SerializeToROS(cleared_ros).ok());
      EXPECT_EQ(cleared_ros.AsString(), fresh_ros.AsString());
    }
  }

  // A message inside the main message frees only what it owns.
  PopulateBranch(message, 1, 1);
  DirectBranch expected;
  PopulateBranch(expected, 1, 1);
  expected.element.Clear();
  (void)expected.element.Mutable();
  message.element->Clear();
  EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
}

TEST(ROSWireConversionTest, ClearingAMainMessageThatAllocatedNothingEmptiesIt) {
  DirectBranch fresh;
  DirectBranch message;
  const size_t made_size = message.Size();
  for (int round = 0; round < 2; ++round) {
    message.id = 3;
    message.timeout = ::ros::Duration(4, 5);
    message.header->seq = 6;
    ASSERT_EQ(message.Size(), made_size);
    message.Clear();
    EXPECT_EQ(message.id, 0);
    EXPECT_FALSE(message.timeout.IsPresent());
    EXPECT_FALSE(message.header.IsPresent());
    EXPECT_EQ(message.SerializeAsString(), fresh.SerializeAsString());
  }
}

TEST(ROSWireConversionTest, ClearingAMainMessageResetsItsCachedROSValues) {
  DirectBranch message;
  message.stamp = ::ros::Time(5, 6);
  message.Clear();
  EXPECT_FALSE(message.stamp.IsPresent());
  EXPECT_EQ(message.stamp.Get().sec, 0u);

  DirectBranch first;
  first.stamp = ::ros::Time(1, 2);
  DirectBranch second;
  second.stamp = ::ros::Time(3, 4);
  std::string first_ros;
  ASSERT_TRUE(first.SerializeToROSString(&first_ros).ok());
  std::string second_ros;
  ASSERT_TRUE(second.SerializeToROSString(&second_ros).ok());

  ASSERT_TRUE(message
                  .ParseFromROS(absl::Span<const char>(first_ros.data(),
                                                       first_ros.size()))
                  .ok());
  EXPECT_EQ(message.stamp.Get().sec, 1u);
  ASSERT_TRUE(message
                  .ParseFromROS(absl::Span<const char>(second_ros.data(),
                                                       second_ros.size()))
                  .ok());
  EXPECT_EQ(message.stamp.Get().sec, 3u);
  EXPECT_EQ(message.stamp.Get().nsec, 4u);
}

TEST(ROSWireConversionTest, ParsingDropsROSValuesNotYetWritten) {
  DirectBranch expected;
  expected.stamp = ::ros::Time(1, 2);
  expected.header->stamp = ::ros::Time(3, 4);
  expected.header->frame_id = "map";
  expected.label = "parsed";
  std::string expected_ros;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_ros).ok());

  DirectBranch message;
  message.stamp = ::ros::Time(5, 6);
  message.header->stamp = ::ros::Time(7, 8);
  message.header->frame_id = "odom";
  message.label = "old";
  ASSERT_TRUE(message
                  .ParseFromROS(absl::Span<const char>(expected_ros.data(),
                                                       expected_ros.size()))
                  .ok());
  EXPECT_EQ(message.stamp.Get().sec, 1u);
  EXPECT_EQ(message.header->stamp.sec, 3u);
  EXPECT_EQ(message.label.Get(), "parsed");
  std::string message_ros;
  ASSERT_TRUE(message.SerializeToROSString(&message_ros).ok());
  EXPECT_EQ(message_ros, expected_ros);
}

TEST(ROSWireConversionTest, EachSyncWritesWhatChangedSinceTheLast) {
  DirectBranch message;
  message.stamp = ::ros::Time(7, 0);
  message.element->timeout = ::ros::Duration(-3, 0);
  for (uint32_t round = 1; round <= 3; ++round) {
    message.header->stamp = ::ros::Time(round, 1);
    message.element->stamp = ::ros::Time(round, 2);
    message.element->timeout->nsec = static_cast<int32_t>(round);
    message.stamp->nsec = round;

    DirectBranch expected;
    expected.header->stamp = ::ros::Time(round, 1);
    expected.element->stamp = ::ros::Time(round, 2);
    expected.element->timeout =
        ::ros::Duration(-3, static_cast<int32_t>(round));
    expected.stamp = ::ros::Time(7, round);
    std::string expected_ros;
    ASSERT_TRUE(expected.SerializeToROSString(&expected_ros).ok());

    // Size() writes the cached values to the payload, which PhaserToROS()
    // reads alone.  The second has nothing left to write.
    for (int sync = 0; sync < 2; ++sync) {
      const size_t size = message.Size();
      ::phaser::ROSBuffer payload_ros;
      ASSERT_TRUE(DirectBranch::PhaserToROS(
                      absl::Span<const char>(
                          reinterpret_cast<const char*>(message.Data()), size),
                      payload_ros)
                      .ok());
      EXPECT_EQ(payload_ros.AsString(), expected_ros)
          << "round " << round << " sync " << sync;
    }
  }
}

TEST(ROSWireConversionTest,
     ClearingAMainMessageResetsTheCachedROSValuesOfItsMessages) {
  DirectBranch expected;
  expected.header->seq = 1;
  std::string expected_ros;
  ASSERT_TRUE(expected.SerializeToROSString(&expected_ros).ok());

  DirectBranch message;
  for (int round = 0; round < 2; ++round) {
    message.header->stamp = ::ros::Time(7, 8);
    message.Clear();
    message.header->seq = 1;
    EXPECT_EQ(message.header->stamp.sec, 0u);
    std::string message_ros;
    ASSERT_TRUE(message.SerializeToROSString(&message_ros).ok());
    EXPECT_EQ(message_ros, expected_ros);
  }
}

TEST(ROSWireConversionTest, ClearingAMainMessageRemakesItsFixedArrays) {
  // User metadata allocated after the message was made keeps it from
  // resetting its buffer, so it clears field by field.
  RosCompileMessage field_by_field;
  ASSERT_TRUE(field_by_field
                  .SetUserMetadata(field_by_field.ToOffset(
                      field_by_field.Allocate(sizeof(uint32_t))))
                  .ok());
  RosCompileMessage message;
  RosCompileMessage fresh;
  std::string fresh_ros;
  ASSERT_TRUE(fresh.SerializeToROSString(&fresh_ros).ok());

  size_t cleared_size = 0;
  for (int round = 0; round < 3; ++round) {
    PopulatePhaserMessage(message);
    PopulatePhaserMessage(field_by_field);
    EXPECT_EQ(message.SerializeAsString(), field_by_field.SerializeAsString());
    message.Clear();
    field_by_field.Clear();
    EXPECT_EQ(message.SerializeAsString(), field_by_field.SerializeAsString());
    EXPECT_EQ(message.choice.index(), std::variant_npos);
    EXPECT_EQ(message.fixed_names[0].Get(), "");
    EXPECT_EQ(message.fixed_names[1].Get(), "");
    std::string cleared_ros;
    ASSERT_TRUE(message.SerializeToROSString(&cleared_ros).ok());
    EXPECT_EQ(cleared_ros, fresh_ros);
    if (round == 0) {
      cleared_size = message.Size();
    }
    EXPECT_EQ(message.Size(), cleared_size);
  }
}

TEST(ROSWireConversionTest, ClearingAMainMessageDropsMetadataInstalledLater) {
  auto populate = [](DirectElements& message) {
    for (int i = 0; i < 3; ++i) {
      auto element = message.elements.Add();
      PopulateDirectElement(element, i, 1);
      auto fixed_leaf = message.fixed_leaves.Add();
      PopulateFixedLeaf(fixed_leaf, i);
      auto branch = message.branches.Add();
      PopulateBranch(branch, i, 1);
    }
  };
  DirectElements message;
  const auto* control = message.runtime->GetRuntimeControl();
  ASSERT_NE(control, nullptr);
  const uint32_t made_count = control->count;
  ASSERT_LT(made_count, DirectElements::MetadataTypeCount());
  const size_t made_size = message.Size();

  DirectElements expected;
  populate(expected);
  for (int round = 0; round < 2; ++round) {
    populate(message);
    EXPECT_GT(message.runtime->GetRuntimeControl()->count, made_count);
    EXPECT_EQ(message.SerializeAsString(), expected.SerializeAsString());
    message.Clear();
    EXPECT_EQ(message.runtime->GetRuntimeControl()->count, made_count);
    EXPECT_EQ(message.Size(), made_size);
  }
}

}  // namespace
