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

enum TestEnum : int {
  kFirst = 1,
  kSecond = -2,
};

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

TEST(ROSWireformatTest, ReadsCanonicalLittleEndianBytes) {
  ROSBuffer buffer;
  ASSERT_TRUE(buffer.Write(static_cast<int16_t>(-2)).ok());
  ASSERT_TRUE(buffer.Write(static_cast<uint32_t>(0xf2345678)).ok());
  ASSERT_TRUE(buffer.Write(true).ok());
  ASSERT_TRUE(buffer.Write(1.5F).ok());
  ASSERT_TRUE(buffer.Write(-2.25).ok());
  ASSERT_TRUE(buffer.WriteString("hello").ok());

  ROSReader reader(buffer.AsSpan());
  absl::StatusOr<int16_t> int16_value = reader.Read<int16_t>();
  ASSERT_TRUE(int16_value.ok());
  EXPECT_EQ(*int16_value, -2);
  absl::StatusOr<uint32_t> uint32_value = reader.Read<uint32_t>();
  ASSERT_TRUE(uint32_value.ok());
  EXPECT_EQ(*uint32_value, 0xf2345678u);
  absl::StatusOr<bool> bool_value = reader.Read<bool>();
  ASSERT_TRUE(bool_value.ok());
  EXPECT_TRUE(*bool_value);
  absl::StatusOr<float> float_value = reader.Read<float>();
  ASSERT_TRUE(float_value.ok());
  EXPECT_FLOAT_EQ(*float_value, 1.5F);
  absl::StatusOr<double> double_value = reader.Read<double>();
  ASSERT_TRUE(double_value.ok());
  EXPECT_DOUBLE_EQ(*double_value, -2.25);
  absl::StatusOr<std::string_view> string_value = reader.ReadString();
  ASSERT_TRUE(string_value.ok());
  EXPECT_EQ(*string_value, "hello");
  EXPECT_TRUE(reader.Eof());
  EXPECT_EQ(reader.Remaining(), 0u);
}

TEST(ROSWireformatTest, ReaderRejectsTruncatedAndInvalidValues) {
  const std::array<char, 3> truncated_integer = {1, 2, 3};
  ROSReader integer_reader(absl::MakeConstSpan(truncated_integer));
  EXPECT_FALSE(integer_reader.Read<uint32_t>().ok());
  EXPECT_EQ(integer_reader.Position(), 0u);

  const std::array<char, 1> invalid_bool = {2};
  ROSReader bool_reader(absl::MakeConstSpan(invalid_bool));
  EXPECT_FALSE(bool_reader.Read<bool>().ok());

  const std::array<char, 6> truncated_string = {5, 0, 0, 0, 'a', 'b'};
  ROSReader string_reader(absl::MakeConstSpan(truncated_string));
  EXPECT_FALSE(string_reader.ReadString().ok());
  EXPECT_EQ(string_reader.Position(), sizeof(uint32_t));
}

TEST(ROSWireformatTest, ReadsPrimitiveArraysInBulk) {
  ROSBuffer buffer;
  for (int32_t value : {1, -2, 3, -4}) {
    ASSERT_TRUE(buffer.Write(value).ok());
  }

  std::array<int32_t, 4> values = {};
  ROSReader reader(buffer.AsSpan());
  ASSERT_TRUE(reader.ReadArray(absl::MakeSpan(values)).ok());
  EXPECT_EQ(values, (std::array<int32_t, 4>{1, -2, 3, -4}));
  EXPECT_TRUE(reader.Eof());
}

TEST(ROSWireformatTest, WritesPrimitiveArraysInBulk) {
  const std::array<int32_t, 4> integers = {1, -2, 3, -4};
  const std::array<bool, 3> bools = {true, false, true};
  const std::array<TestEnum, 2> enums = {kFirst, kSecond};

  ROSBuffer buffer;
  ASSERT_TRUE(buffer.WriteArray(absl::MakeConstSpan(integers)).ok());
  ASSERT_TRUE(buffer.WriteArray(absl::MakeConstSpan(bools)).ok());
  ASSERT_TRUE(buffer.WriteArray(absl::MakeConstSpan(enums)).ok());
  ASSERT_TRUE(buffer.WriteZeros(3).ok());

  ROSReader reader(buffer.AsSpan());
  std::array<int32_t, 4> decoded_integers = {};
  std::array<bool, 3> decoded_bools = {};
  std::array<TestEnum, 2> decoded_enums = {};
  ASSERT_TRUE(
      reader.ReadArray(absl::MakeSpan(decoded_integers)).ok());
  ASSERT_TRUE(reader.ReadArray(absl::MakeSpan(decoded_bools)).ok());
  ASSERT_TRUE(reader.ReadArray(absl::MakeSpan(decoded_enums)).ok());
  EXPECT_EQ(decoded_integers, integers);
  EXPECT_EQ(decoded_bools, bools);
  EXPECT_EQ(decoded_enums, enums);
  EXPECT_EQ(reader.Remaining(), 3u);
  for (size_t i = 0; i < 3; ++i) {
    absl::StatusOr<uint8_t> zero = reader.Read<uint8_t>();
    ASSERT_TRUE(zero.ok());
    EXPECT_EQ(*zero, 0);
  }
}

TEST(ROSWireformatTest, BulkWriteFailureDoesNotAdvanceCursor) {
  std::array<char, 7> storage = {};
  const std::array<int32_t, 2> values = {1, 2};
  ROSBuffer buffer(storage.data(), storage.size());
  EXPECT_FALSE(buffer.WriteArray(absl::MakeConstSpan(values)).ok());
  EXPECT_EQ(buffer.Size(), 0u);
}

TEST(ROSWireformatTest, BulkReadValidatesBeforeAdvancing) {
  const std::array<char, 7> truncated = {};
  std::array<int32_t, 2> integers = {};
  ROSReader integer_reader(absl::MakeConstSpan(truncated));
  EXPECT_FALSE(integer_reader.ReadArray(absl::MakeSpan(integers)).ok());
  EXPECT_EQ(integer_reader.Position(), 0u);

  const std::array<char, 3> invalid_bool = {0, 2, 1};
  std::array<bool, 3> bools = {};
  ROSReader bool_reader(absl::MakeConstSpan(invalid_bool));
  EXPECT_FALSE(bool_reader.ReadArray(absl::MakeSpan(bools)).ok());
  EXPECT_EQ(bool_reader.Position(), 0u);
}

}  // namespace
}  // namespace phaser
