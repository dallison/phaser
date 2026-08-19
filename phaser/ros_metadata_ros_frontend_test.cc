// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include "phaser/runtime/md5.h"
#include "phaser/testdata/ros_metadata_ros_phaser/phaser/testdata/RosMetadata.phaser.h"

namespace ros_metadata::ros_api {
namespace {

TEST(RosMetadataRosFrontendTest, MatchesRosMetadata) {
  EXPECT_EQ(Bool::RosDataType(), "std_msgs/Bool");
  EXPECT_EQ(Bool::RosDefinition(), "bool data\n");
  EXPECT_EQ(Bool::RosMd5(), "8b94c1b53db61fb6aed406028ad6332a");

  EXPECT_EQ(Wrapper::RosDataType(), "example_msgs/Wrapper");
  EXPECT_EQ(
      Wrapper::RosMd5(),
      ::phaser::Md5("uint8 READY=1\n"
                    "8b94c1b53db61fb6aed406028ad6332a child\n"
                    "int32[3] samples\n" +
          ::phaser::Md5("uint8 OK=0\nuint8 BAD=1") + " status\nbool ready"));
}

}  // namespace
}  // namespace ros_metadata::ros_api
