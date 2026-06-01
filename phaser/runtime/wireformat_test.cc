// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "absl/strings/str_format.h"
#include "phaser/runtime/runtime.h"
#include "phaser/runtime/wireformat.h"
#include "toolbelt/hexdump.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <sstream>

using ProtoBuffer = phaser::ProtoBuffer;
using WireType = phaser::WireType;

TEST(Wireformat, Sizes) {
  ASSERT_EQ(1, (ProtoBuffer::VarintSize<int32_t, false>(1)));
  ASSERT_EQ(2, (ProtoBuffer::VarintSize<int32_t, false>(0x80)));
  ASSERT_EQ(3, (ProtoBuffer::VarintSize<int32_t, false>(0x8000)));
  // int32 varints use unsigned encoding (10 bytes for negative values).
  ASSERT_EQ(10, (ProtoBuffer::VarintSize<int32_t, false>(-123456)));

  ASSERT_EQ(1, ProtoBuffer::TagSize(1, WireType::kVarint));
  ASSERT_EQ(1, ProtoBuffer::TagSize(0xf, WireType::kVarint));
  ASSERT_EQ(2, ProtoBuffer::TagSize(0x10, WireType::kVarint));

  ASSERT_EQ(2, ProtoBuffer::LengthDelimitedSize(1, 0));
  ASSERT_EQ(3, ProtoBuffer::LengthDelimitedSize(1, 1));

  ASSERT_EQ(7, ProtoBuffer::StringSize(1, "hello"));
}

TEST(Wireformat, ZigZagKnownValues) {
  // Canonical protobuf zigzag mappings. ZigZag() returns the encoded value
  // reinterpreted as the signed type, so compare via the unsigned bit pattern.
  EXPECT_EQ(0u, static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(0)));
  EXPECT_EQ(1u, static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(-1)));
  EXPECT_EQ(2u, static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(1)));
  EXPECT_EQ(3u, static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(-2)));
  EXPECT_EQ(4u, static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(2)));
  EXPECT_EQ(4294967294u,
            static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(INT32_MAX)));
  EXPECT_EQ(4294967295u,
            static_cast<uint32_t>(ProtoBuffer::ZigZag<int32_t>(INT32_MIN)));

  // 64-bit values exercised the bug: the old implementation hard-coded a
  // 31-bit sign shift and produced garbage for anything outside the int32
  // range.
  EXPECT_EQ(1u, static_cast<uint64_t>(ProtoBuffer::ZigZag<int64_t>(-1)));
  EXPECT_EQ(uint64_t(1) << 32,
            static_cast<uint64_t>(ProtoBuffer::ZigZag<int64_t>(int64_t(1)
                                                               << 31)));
  EXPECT_EQ(0xFFFFFFFFFFFFFFFEull,
            static_cast<uint64_t>(ProtoBuffer::ZigZag<int64_t>(INT64_MAX)));
  EXPECT_EQ(0xFFFFFFFFFFFFFFFFull,
            static_cast<uint64_t>(ProtoBuffer::ZigZag<int64_t>(INT64_MIN)));
}

TEST(Wireformat, ZigZagRoundTrip) {
  EXPECT_EQ(0, ProtoBuffer::ZagZig<int32_t>(ProtoBuffer::ZigZag<int32_t>(0)));
  for (int32_t v :
       {INT32_MIN, INT32_MIN + 1, -123456, -2, -1, 0, 1, 2, 123456,
        INT32_MAX - 1, INT32_MAX}) {
    EXPECT_EQ(v, ProtoBuffer::ZagZig<int32_t>(ProtoBuffer::ZigZag<int32_t>(v)))
        << "int32 value " << v;
  }
  for (int64_t v : {INT64_MIN, INT64_MIN + 1, int64_t(INT32_MIN) - 1,
                    int64_t(INT32_MIN), int64_t(-1) << 40, int64_t(-1),
                    int64_t(0), int64_t(1), int64_t(1) << 31, int64_t(1) << 40,
                    int64_t(INT32_MAX) + 1, INT64_MAX - 1, INT64_MAX}) {
    EXPECT_EQ(v, ProtoBuffer::ZagZig<int64_t>(ProtoBuffer::ZigZag<int64_t>(v)))
        << "int64 value " << v;
  }
}

TEST(Wireformat, SintVarintRoundTrip) {
  // Full serialize/deserialize round trip through the varint wire format for
  // the sint32/sint64 (zigzag) encodings.
  auto round_trip64 = [](int64_t v) {
    ProtoBuffer out;
    ASSERT_TRUE((out.SerializeRawVarint<int64_t, true>(v).ok()));
    // ProtoBuffer(std::string_view) does not own its storage, so the encoded
    // string must outlive the reader.
    std::string encoded = out.AsString();
    ProtoBuffer in(encoded);
    absl::StatusOr<int64_t> decoded = in.DeserializeVarint<int64_t, true>();
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(v, *decoded) << "int64 value " << v;
  };
  for (int64_t v : {INT64_MIN, int64_t(INT32_MIN) - 1, int64_t(-1),
                    int64_t(0), int64_t(1), int64_t(1) << 33,
                    int64_t(INT32_MAX) + 1, INT64_MAX}) {
    round_trip64(v);
  }

  auto round_trip32 = [](int32_t v) {
    ProtoBuffer out;
    ASSERT_TRUE((out.SerializeRawVarint<int32_t, true>(v).ok()));
    std::string encoded = out.AsString();
    ProtoBuffer in(encoded);
    absl::StatusOr<int32_t> decoded = in.DeserializeVarint<int32_t, true>();
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(v, *decoded) << "int32 value " << v;
  };
  for (int32_t v : {INT32_MIN, -1, 0, 1, INT32_MAX}) {
    round_trip32(v);
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
