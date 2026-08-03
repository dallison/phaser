// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#include "phaser/runtime/ros_wireformat.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace phaser {
namespace {

TEST(ROSWireformatTest, WritesCanonicalLittleEndianBytes) {
  ROSBuffer buffer;
  ASSERT_TRUE(buffer.Write(static_cast<int16_t>(-2)).ok());
  ASSERT_TRUE(buffer.Write(static_cast<uint32_t>(0x12345678)).ok());
  ASSERT_TRUE(buffer.Write(true).ok());
  ASSERT_TRUE(buffer.WriteString("hi").ok());

  const std::array<unsigned char, 13> expected = {
      0xfe, 0xff, 0x78, 0x56, 0x34, 0x12, 0x01,
      0x02, 0x00, 0x00, 0x00, 'h',  'i',
  };
  ASSERT_EQ(buffer.Size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(static_cast<unsigned char>(buffer.data()[i]), expected[i])
        << "byte " << i;
  }
}

TEST(ROSWireformatTest, DynamicBufferGrowsAndCanBeReused) {
  ROSBuffer buffer(16);
  const std::string value(4096, 'x');
  ASSERT_TRUE(buffer.WriteString(value).ok());
  EXPECT_EQ(buffer.Size(), value.size() + sizeof(uint32_t));
  EXPECT_GE(buffer.Capacity(), buffer.Size());

  buffer.Clear();
  EXPECT_TRUE(buffer.empty());
  ASSERT_TRUE(buffer.Write(static_cast<uint64_t>(7)).ok());
  EXPECT_EQ(buffer.Size(), sizeof(uint64_t));
}

TEST(ROSWireformatTest, FixedBufferFailureDoesNotAdvanceCursor) {
  std::array<char, 3> storage = {};
  ROSBuffer buffer(storage.data(), storage.size());
  EXPECT_FALSE(buffer.Write(static_cast<uint32_t>(1)).ok());
  EXPECT_EQ(buffer.Size(), 0u);

  EXPECT_FALSE(buffer.WriteString("x").ok());
  EXPECT_EQ(buffer.Size(), 0u);
}

TEST(ROSWireformatTest, RejectsInvalidRawWrite) {
  ROSBuffer buffer;
  EXPECT_FALSE(buffer.WriteRaw(nullptr, 1).ok());
  EXPECT_EQ(buffer.Size(), 0u);
  EXPECT_TRUE(buffer.WriteRaw(nullptr, 0).ok());
}

}  // namespace
}  // namespace phaser
