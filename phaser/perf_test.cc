// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.


// It would be in your interests to build this optimized or you will be
// waiting a while...

#include "absl/strings/str_format.h"
#include "phaser/runtime/runtime.h"
#include "phaser/testdata/vision.pb.h"
#include "phaser/testdata/vision.phaser.h"
#include "toolbelt/clock.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"
#include <gtest/gtest.h>
#include <sstream>

// This test builds a camera image in a fixed size buffer.  The protobuf version
// has to serialize it into the buffer, but with phaser we build it directly in
// the buffer memory.

TEST(PerfTest, ProtobufOne) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 1000; i++) {
    robot::CameraImage image;
    image.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumRows = 4096;
    constexpr int kNumCols = 4096;

    image.set_rows(kNumRows);
    image.set_cols(kNumCols);

    std::string image_data;
    image_data.resize(kNumRows * kNumCols);
    for (int i = 0; i < kNumRows; ++i) {
      for (int j = 0; j < kNumCols; ++j) {
        image_data[i * kNumCols + j] = i * kNumCols + j;
      }
    }
    image.set_image(image_data);

    ASSERT_TRUE(image.SerializeToArray(buffer.data(), buffer.size()));
  }
  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Protobuf serialization: %d ns\n", end - start);
}

TEST(PerfTest, PhaserOne) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 1000; i++) {
    robot::phaser::CameraImage image =
        robot::phaser::CameraImage::CreateMutable(buffer.data(), buffer.size());
    image.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumRows = 4096;
    constexpr int kNumCols = 4096;

    image.set_rows(kNumRows);
    image.set_cols(kNumCols);

    absl::Span<char> image_data = image.allocate_image(kNumRows * kNumCols);
    for (int i = 0; i < kNumRows; ++i) {
      for (int j = 0; j < kNumCols; ++j) {
        image_data[i * kNumCols + j] = i * kNumCols + j;
      }
    }
  }
  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Phaser zero-copy: %d ns\n", end - start);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
