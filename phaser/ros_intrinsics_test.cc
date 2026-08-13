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

}  // namespace
}  // namespace foo::bar::phaser
