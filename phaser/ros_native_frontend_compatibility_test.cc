#include "phaser/testdata/RosIntrinsics.phaser.h"
#include "phaser/testdata/RosIntrinsicsProtobufFrontend.phaser.h"

#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace {

using RosMessage = ::foo::bar::phaser::RosIntrinsicMessage;
using ProtobufMessage =
    ::foo::bar::pb::protobuf_phaser::RosIntrinsicMessage;

TEST(RosNativeFrontendCompatibilityTest,
     ProtobufFrontendReadsRosFrontendNativePayload) {
  RosMessage ros_message;
  ros_message.stamp = ::ros::Time(12, 345);
  ros_message.timeout = ::ros::Duration(-4, -500);

  ::std_msgs::Header header;
  header.seq = 9;
  header.stamp = ::ros::Time(21, 654);
  header.frame_id = "map";
  ros_message.header = header;

  ros_message.count = 42;
  ros_message.name = "native";
  ros_message.samples.push_back(3);
  ros_message.samples.push_back(5);
  ros_message.tags.push_back("front");
  ros_message.tags.push_back("rear");

  auto first_child = ros_message.children.Add();
  first_child->id = 101;
  first_child->label = "left";
  auto second_child = ros_message.children.Add();
  second_child->id = 202;
  second_child->label = "right";

  ros_message.fixed_names[0] = "fixed-a";
  ros_message.fixed_names[1] = "fixed-b";
  ros_message.fixed_children[0]->id = 301;
  ros_message.fixed_children[0]->label = "fixed-left";
  ros_message.fixed_children[1]->id = 302;
  ros_message.fixed_children[1]->label = "fixed-right";

  using ChoiceChild = RosMessage::ChoiceChildAlternative;
  auto& selected = ros_message.choice.emplace<ChoiceChild>();
  selected.id = 404;
  selected.label = "selected";

  const size_t native_size = ros_message.ByteSizeLong();
  std::vector<char> native_payload(native_size);
  std::memcpy(native_payload.data(), ros_message.Data(), native_size);

  const ProtobufMessage protobuf_view = ProtobufMessage::CreateReadonly(
      native_payload.data(), native_payload.size());

  EXPECT_EQ(protobuf_view.stamp().seconds(), 12);
  EXPECT_EQ(protobuf_view.stamp().nanos(), 345);
  EXPECT_EQ(protobuf_view.timeout().seconds(), -4);
  EXPECT_EQ(protobuf_view.timeout().nanos(), -500);
  EXPECT_EQ(protobuf_view.header().seq(), 9u);
  EXPECT_EQ(protobuf_view.header().stamp().seconds(), 21);
  EXPECT_EQ(protobuf_view.header().stamp().nanos(), 654);
  EXPECT_EQ(protobuf_view.header().frame_id(), "map");

  EXPECT_EQ(protobuf_view.count(), 42);
  EXPECT_EQ(protobuf_view.name(), "native");
  ASSERT_EQ(protobuf_view.samples_size(), 2u);
  EXPECT_EQ(protobuf_view.samples(0), 3);
  EXPECT_EQ(protobuf_view.samples(1), 5);
  ASSERT_EQ(protobuf_view.tags_size(), 2u);
  EXPECT_EQ(protobuf_view.tags(0), "front");
  EXPECT_EQ(protobuf_view.tags(1), "rear");

  ASSERT_EQ(protobuf_view.children_size(), 2u);
  EXPECT_EQ(protobuf_view.children(0).id(), 101);
  EXPECT_EQ(protobuf_view.children(0).label(), "left");
  EXPECT_EQ(protobuf_view.children(1).id(), 202);
  EXPECT_EQ(protobuf_view.children(1).label(), "right");

  ASSERT_EQ(protobuf_view.fixed_names_size(), 2u);
  EXPECT_EQ(protobuf_view.fixed_names(0), "fixed-a");
  EXPECT_EQ(protobuf_view.fixed_names(1), "fixed-b");
  ASSERT_EQ(protobuf_view.fixed_children_size(), 2u);
  EXPECT_EQ(protobuf_view.fixed_children(0).id(), 301);
  EXPECT_EQ(protobuf_view.fixed_children(0).label(), "fixed-left");
  EXPECT_EQ(protobuf_view.fixed_children(1).id(), 302);
  EXPECT_EQ(protobuf_view.fixed_children(1).label(), "fixed-right");

  ASSERT_TRUE(protobuf_view.has_choice_child());
  EXPECT_EQ(protobuf_view.choice_child().id(), 404);
  EXPECT_EQ(protobuf_view.choice_child().label(), "selected");
}

// The ROS frontend stores nested messages inline and the protobuf frontend
// through offsets, so each must read the other's layout from its metadata.
TEST(RosNativeFrontendCompatibilityTest,
     RosFrontendReadsProtobufFrontendNativePayload) {
  ProtobufMessage protobuf_message;
  protobuf_message.mutable_stamp()->set_seconds(12);
  protobuf_message.mutable_stamp()->set_nanos(345);
  protobuf_message.mutable_timeout()->set_seconds(-4);
  protobuf_message.mutable_timeout()->set_nanos(-500);
  auto* header = protobuf_message.mutable_header();
  header->set_seq(9);
  header->mutable_stamp()->set_seconds(21);
  header->mutable_stamp()->set_nanos(654);
  header->set_frame_id("map");
  protobuf_message.set_count(42);

  const size_t native_size = protobuf_message.ByteSizeLong();
  std::vector<char> native_payload(native_size);
  std::memcpy(native_payload.data(), protobuf_message.Data(), native_size);

  const RosMessage ros_view =
      RosMessage::CreateReadonly(native_payload.data(), native_payload.size());
  EXPECT_EQ(ros_view.stamp->sec, 12u);
  EXPECT_EQ(ros_view.stamp->nsec, 345u);
  EXPECT_EQ(ros_view.timeout->sec, -4);
  EXPECT_EQ(ros_view.timeout->nsec, -500);
  EXPECT_EQ(ros_view.header->seq, 9u);
  EXPECT_EQ(ros_view.header->stamp.sec, 21u);
  EXPECT_EQ(ros_view.header->stamp.nsec, 654u);
  EXPECT_EQ(ros_view.header.Get().frame_id, "map");
  EXPECT_EQ(ros_view.count.Get(), 42);
}

}  // namespace
