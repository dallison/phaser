#include "phaser/testdata/RosIntrinsics.phaser.h"

#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace foo::bar::phaser {
namespace {

void MutateTime(::ros::Time& value) {
  value.sec = 12;
  value.nsec = 345;
}

void MutateDuration(::ros::Duration& value) {
  value.sec = -4;
  value.nsec = 500;
}

template <typename HeaderField>
void MutateHeader(HeaderField& field) {
  auto value = field.Mutable();
  value.seq = 9;
  value.stamp = ::ros::Time(21, 654);
  value.frame_id = "map";
}

uint32_t ReadSeconds(const ::ros::Time& value) { return value.sec; }
uint32_t ReadSecondsByValue(::ros::Time value) { return value.sec; }

template <typename HeaderField>
std::string ReadFrame(const HeaderField& value) {
  return std::string(value.Get().frame_id);
}
std::string ReadFrameByValue(::std_msgs::Header value) {
  return value.frame_id;
}

TEST(RosIntrinsicsTest, ExistingMutableReferenceFunctionsWorkUnchanged) {
  RosIntrinsicMessage message;

  MutateTime(message.stamp);
  MutateDuration(message.timeout);
  MutateHeader(message.header);

  EXPECT_EQ(ReadSeconds(message.stamp), 12u);
  EXPECT_EQ(ReadSecondsByValue(message.stamp), 12u);
  EXPECT_EQ(message.stamp->nsec, 345u);
  EXPECT_EQ(message.timeout->sec, -4);
  EXPECT_EQ(ReadFrame(message.header), "map");
  EXPECT_EQ(ReadFrameByValue(message.header.ToOwned()), "map");
  EXPECT_EQ(message.header->stamp.sec, 21u);
}

TEST(RosIntrinsicsTest, NativePayloadAccessFlushesMutableBorrows) {
  RosIntrinsicMessage message;
  MutateTime(message.stamp);
  MutateDuration(message.timeout);
  MutateHeader(message.header);

  const size_t size = message.ByteSizeLong();
  const void* data = message.Data();
  std::vector<char> buffer(size);
  std::memcpy(buffer.data(), data, size);

  RosIntrinsicMessage readonly =
      RosIntrinsicMessage::CreateReadonly(buffer.data(), buffer.size());
  const RosIntrinsicMessage& view = readonly;
  EXPECT_EQ(ReadSeconds(view.stamp), 12u);
  EXPECT_EQ(view.stamp->nsec, 345u);
  EXPECT_EQ(view.timeout->sec, -4);
  EXPECT_EQ(view.timeout->nsec, 500);
  EXPECT_EQ(view.header->seq, 9u);
  EXPECT_EQ(view.header->stamp.sec, 21u);
  EXPECT_EQ(view.header.Get().frame_id, "map");
}

TEST(RosIntrinsicsTest, ProtobufWireRoundtripFlushesMutableBorrows) {
  RosIntrinsicMessage phaser_message;
  MutateTime(phaser_message.stamp);
  MutateDuration(phaser_message.timeout);
  MutateHeader(phaser_message.header);

  RosIntrinsicMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(phaser_message.SerializeAsString()));
  EXPECT_EQ(ReadSeconds(parsed.stamp), 12u);
  EXPECT_EQ(parsed.stamp->nsec, 345u);
  EXPECT_EQ(parsed.timeout->sec, -4);
  EXPECT_EQ(parsed.timeout->nsec, 500);
  EXPECT_EQ(parsed.header->seq, 9u);
  EXPECT_EQ(parsed.header->stamp.sec, 21u);
  EXPECT_EQ(ReadFrame(parsed.header), "map");
}

TEST(RosIntrinsicsTest, CopyAndMovePreserveDeferredValues) {
  RosIntrinsicMessage source;
  MutateTime(source.stamp);
  MutateHeader(source.header);

  RosIntrinsicMessage copy(source);
  EXPECT_EQ(ReadSeconds(copy.stamp), 12u);
  EXPECT_EQ(ReadFrame(copy.header), "map");

  RosIntrinsicMessage moved(std::move(source));
  EXPECT_EQ(ReadSeconds(moved.stamp), 12u);
  EXPECT_EQ(ReadFrame(moved.header), "map");
}

void FillRepeated(RosIntrinsicMessage& message) {
  message.stamps.Add(::ros::Time(1, 2));
  message.stamps.Add(::ros::Time(3, 4));
  message.timeouts.Add(::ros::Duration(-5, 6));
  message.fixed_stamps.Set(0, ::ros::Time(7, 8));
  message.fixed_stamps.Set(1, ::ros::Time(9, 10));
  message.fixed_timeouts.Set(0, ::ros::Duration(-11, 12));
  message.fixed_timeouts.Set(1, ::ros::Duration(13, 14));
}

void ExpectRepeated(const RosIntrinsicMessage& message) {
  ASSERT_EQ(message.stamps.size(), 2u);
  EXPECT_EQ(message.stamps.Get(0), ::ros::Time(1, 2));
  EXPECT_EQ(message.stamps.Get(1), ::ros::Time(3, 4));
  ASSERT_EQ(message.timeouts.size(), 1u);
  EXPECT_EQ(message.timeouts.Get(0), ::ros::Duration(-5, 6));
  ASSERT_EQ(message.fixed_stamps.size(), 2u);
  EXPECT_EQ(message.fixed_stamps.Get(0), ::ros::Time(7, 8));
  EXPECT_EQ(message.fixed_stamps.Get(1), ::ros::Time(9, 10));
  ASSERT_EQ(message.fixed_timeouts.size(), 2u);
  EXPECT_EQ(message.fixed_timeouts.Get(0), ::ros::Duration(-11, 12));
  EXPECT_EQ(message.fixed_timeouts.Get(1), ::ros::Duration(13, 14));
}

TEST(RosIntrinsicsTest, RepeatedIntrinsicsReadAsRosValues) {
  RosIntrinsicMessage message;
  FillRepeated(message);
  ExpectRepeated(message);

  // The element type is the ROS value, not the Timestamp backing it.
  std::vector<::ros::Time> collected;
  for (::ros::Time value : message.stamps) {
    collected.push_back(value);
  }
  EXPECT_EQ(collected,
            (std::vector<::ros::Time>{::ros::Time(1, 2), ::ros::Time(3, 4)}));
  EXPECT_EQ(ReadSecondsByValue(message.stamps[1]), 3u);
}

TEST(RosIntrinsicsTest, RepeatedIntrinsicElementsAreAssignable) {
  RosIntrinsicMessage message;
  FillRepeated(message);

  message.stamps[0] = ::ros::Time(100, 200);
  message.fixed_timeouts[1] = ::ros::Duration(-300, 400);

  EXPECT_EQ(message.stamps.Get(0), ::ros::Time(100, 200));
  EXPECT_EQ(message.stamps.Get(1), ::ros::Time(3, 4));
  EXPECT_EQ(message.fixed_timeouts.Get(1), ::ros::Duration(-300, 400));
}

TEST(RosIntrinsicsTest, RepeatedIntrinsicsSurviveProtobufWireRoundtrip) {
  RosIntrinsicMessage message;
  FillRepeated(message);

  RosIntrinsicMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(message.SerializeAsString()));
  ExpectRepeated(parsed);
}

TEST(RosIntrinsicsTest, RepeatedIntrinsicsSurviveRosWireRoundtrip) {
  RosIntrinsicMessage message;
  FillRepeated(message);

  std::string ros_wire;
  ASSERT_TRUE(message.SerializeToROSString(&ros_wire).ok());
  EXPECT_EQ(message.ROSSerializedSize(), ros_wire.size());

  RosIntrinsicMessage parsed;
  ASSERT_TRUE(
      parsed.ParseFromROS(absl::Span<const char>(ros_wire.data(),
                                                 ros_wire.size()))
          .ok());
  ExpectRepeated(parsed);
}

TEST(RosIntrinsicsTest, RepeatedIntrinsicsUseEightRosBytesPerElement) {
  RosIntrinsicMessage empty;
  RosIntrinsicMessage populated;
  populated.stamps.Add(::ros::Time(1, 2));
  populated.stamps.Add(::ros::Time(3, 4));
  populated.timeouts.Add(::ros::Duration(-5, 6));

  // A fixed extent is always serialized in full, so only the unbounded fields
  // move: sec and nsec, four bytes each, with the sequence length unchanged.
  EXPECT_EQ(populated.ROSSerializedSize(), empty.ROSSerializedSize() + 3 * 8);
}

TEST(RosIntrinsicsTest, RepeatedIntrinsicsClone) {
  RosIntrinsicMessage source;
  FillRepeated(source);

  RosIntrinsicMessage copy(source);
  ExpectRepeated(copy);
}

}  // namespace
}  // namespace foo::bar::phaser
