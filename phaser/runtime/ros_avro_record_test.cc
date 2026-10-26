// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include "phaser/runtime/ros_avro_record.h"
#include "phaser/testdata/ros_metadata_protobuf_phaser/phaser/testdata/RosMetadata.phaser.h"

namespace example_msgs::proto::protobuf_api {
namespace {

TEST(RosAvroRecordTest, StampedRosBytesRecordEncoding) {
  std::string record;
  ASSERT_TRUE(
      ::phaser::ros_avro_record::WriteStampedRosBytesAvroRecord(1, 2, "abc", &record)
          .ok());
  EXPECT_EQ(record.size(), 3U + 3U);
  EXPECT_EQ(record.substr(record.size() - 3), "abc");
}

TEST(RosAvroRecordTest, ProtobufToStampedRosBytesUsesRosWire) {
  Bool message;
  message.set_data(true);
  std::string protobuf = message.SerializeAsString();
  std::string record;
  ASSERT_TRUE(::phaser::ros_avro_record::PhaserBankProtobufToStampedRosBytesAvroRecord(
                  Bool::FullName(), protobuf, 10, 20, &record)
                  .ok());
  EXPECT_GT(record.size(), 0U);

  auto metadata =
      ::phaser::ros_avro_record::PhaserBankRosConnectionMetadata(Bool::FullName());
  ASSERT_TRUE(metadata.ok());
  EXPECT_EQ(metadata->ros_datatype, "example_msgs/Bool");
  EXPECT_EQ(metadata->protobuf_full_name, Bool::FullName());
}

}  // namespace
}  // namespace example_msgs::proto::protobuf_api
