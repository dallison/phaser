#include "absl/strings/str_format.h"
#include "phaser/runtime/runtime.h"
#include "phaser/runtime/wireformat.h"
#include "toolbelt/hexdump.h"
#include <gtest/gtest.h>
#include <sstream>

using ProtoBuffer = phaser::ProtoBuffer;
using WireType = phaser::WireType;

TEST(Wireformat, Sizes) {
  ASSERT_EQ(1, (ProtoBuffer::VarintSize<int32_t, false>(1)));
  ASSERT_EQ(2, (ProtoBuffer::VarintSize<int32_t, false>(0x80)));
  ASSERT_EQ(3, (ProtoBuffer::VarintSize<int32_t, false>(0x8000)));

  ASSERT_EQ(1, ProtoBuffer::TagSize(1, WireType::kVarint));
  ASSERT_EQ(1, ProtoBuffer::TagSize(0xf, WireType::kVarint));
  ASSERT_EQ(2, ProtoBuffer::TagSize(0x10, WireType::kVarint));

  ASSERT_EQ(2, ProtoBuffer::LengthDelimitedSize(1, 0));
  ASSERT_EQ(3, ProtoBuffer::LengthDelimitedSize(1, 1));

  ASSERT_EQ(7, ProtoBuffer::StringSize(1, "hello"));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
