// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include "phaser/runtime/md5.h"
#include "phaser/runtime/phaser_bank.h"
#include "phaser/runtime/phaser_bank_protobuf.h"
#include "phaser/testdata/ros_metadata_ros_phaser/phaser/testdata/RosMetadata.phaser.h"

namespace example_msgs::proto::ros_api {
namespace {

TEST(RosMetadataRosFrontendTest, MatchesRosMetadata) {
  EXPECT_EQ(Bool::RosDataType(), "example_msgs/Bool");
  EXPECT_EQ(Bool::RosDefinition(), "bool data\n");
  EXPECT_EQ(Bool::RosMd5(), "8b94c1b53db61fb6aed406028ad6332a");

  EXPECT_EQ(Status::RosDataType(), "example_msgs/Status");
  EXPECT_EQ(Status::RosDefinition(), "int32 OK=0\nint32 BAD=1\nint32 value\n");
  EXPECT_EQ(Status::RosMd5(),
            ::phaser::Md5("int32 OK=0\nint32 BAD=1\nint32 value"));

  EXPECT_EQ(Wrapper::RosDataType(), "example_msgs/Wrapper");
  EXPECT_EQ(Wrapper::RosMd5(),
            ::phaser::Md5("uint8 READY=1\n"
                          "8b94c1b53db61fb6aed406028ad6332a child\n"
                          "int32[3] samples\n" +
                          Status::RosMd5() +
                          " status\nbool ready\ntime stamp\ntime[] stamps"));

  EXPECT_EQ(LegacyStatusWrapper::RosDefinition(),
            "example_msgs/LegacyStatus status\n\n"
            "=================================================================="
            "==============\n"
            "MSG: example_msgs/LegacyStatus\n"
            "# Legacy status.\nuint8 OK = 0\n");

  absl::StatusOr<std::string_view> data_type =
      ::phaser::PhaserBankRosDataType(Bool::FullName());
  ASSERT_TRUE(data_type.ok());
  EXPECT_EQ(*data_type, Bool::RosDataType());
  absl::StatusOr<std::string> md5 =
      ::phaser::PhaserBankRosMd5(Bool::FullName());
  ASSERT_TRUE(md5.ok());
  EXPECT_EQ(*md5, Bool::RosMd5());
  absl::StatusOr<std::string_view> definition =
      ::phaser::PhaserBankRosDefinition(Bool::FullName());
  ASSERT_TRUE(definition.ok());
  EXPECT_EQ(*definition, Bool::RosDefinition());

  absl::StatusOr<std::string_view> protobuf_full_name =
      ::phaser::PhaserBankProtobufFullName(Bool::FullName());
  ASSERT_TRUE(protobuf_full_name.ok());
  EXPECT_EQ(*protobuf_full_name, "example_msgs.proto.Bool");
  absl::StatusOr<std::string_view> protobuf_name =
      ::phaser::PhaserBankProtobufName(Bool::FullName());
  ASSERT_TRUE(protobuf_name.ok());
  EXPECT_EQ(*protobuf_name, "Bool");

  absl::StatusOr<const google::protobuf::Descriptor*> descriptor =
      ::phaser::PhaserBankProtobufDescriptor(Bool::FullName());
  ASSERT_TRUE(descriptor.ok()) << descriptor.status();
  EXPECT_EQ((*descriptor)->full_name(), "example_msgs.proto.Bool");
  EXPECT_EQ((*descriptor)->name(), "Bool");
  ASSERT_EQ((*descriptor)->field_count(), 1);
  EXPECT_EQ((*descriptor)->field(0)->name(), "data");
  EXPECT_EQ((*descriptor)->field(0)->number(), 1);

  absl::StatusOr<const google::protobuf::Descriptor*> wrapper =
      ::phaser::PhaserBankProtobufDescriptor(Wrapper::FullName());
  ASSERT_TRUE(wrapper.ok()) << wrapper.status();
  const google::protobuf::FieldDescriptor* stamp =
      (*wrapper)->FindFieldByName("stamp");
  ASSERT_NE(stamp, nullptr);
  ASSERT_NE(stamp->message_type(), nullptr);
  EXPECT_FALSE(stamp->message_type()->is_placeholder());
  EXPECT_EQ(stamp->message_type()->full_name(), "google.protobuf.Timestamp");
}

}  // namespace
}  // namespace example_msgs::proto::ros_api
