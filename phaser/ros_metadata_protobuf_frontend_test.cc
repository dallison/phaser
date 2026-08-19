// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include "phaser/runtime/md5.h"
#include "phaser/testdata/ros_metadata_protobuf_phaser/phaser/testdata/RosMetadata.phaser.h"

namespace example_msgs::proto::protobuf_api {
namespace {

TEST(RosMetadataProtobufFrontendTest, MatchesRosMetadata) {
  EXPECT_EQ(Bool::RosDataType(), "example_msgs/Bool");
  EXPECT_EQ(Bool::RosDefinition(), "bool data\n");
  EXPECT_EQ(Bool::RosMd5(), "8b94c1b53db61fb6aed406028ad6332a");

  EXPECT_EQ(Status::RosDataType(), "example_msgs/Status");
  EXPECT_EQ(Status::RosDefinition(), "int32 OK=0\nint32 BAD=1\nint32 value\n");
  EXPECT_EQ(Status::RosMd5(),
            ::phaser::Md5("int32 OK=0\nint32 BAD=1\nint32 value"));

  EXPECT_EQ(Wrapper::RosDataType(), "example_msgs/Wrapper");
  EXPECT_EQ(Wrapper::RosDefinition(),
            "uint8 READY=1\nexample_msgs/Bool child\nint32[3] samples\n"
            "example_msgs/Status status\nbool ready\ntime stamp\n"
            "=================================================================="
            "==============\n"
            "MSG: example_msgs/Bool\n"
            "bool data\n"
            "=================================================================="
            "==============\n"
            "MSG: example_msgs/Status\n"
            "int32 OK=0\nint32 BAD=1\nint32 value\n");
  EXPECT_EQ(
      Wrapper::RosMd5(),
      ::phaser::Md5("uint8 READY=1\n"
                    "8b94c1b53db61fb6aed406028ad6332a child\n"
                    "int32[3] samples\n" +
                    Status::RosMd5() + " status\nbool ready\ntime stamp"));
}

}  // namespace
}  // namespace example_msgs::proto::protobuf_api
