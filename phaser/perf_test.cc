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
//
// All the tests produce a serialized message in a fixed size buffer, suitable
// for transmission over IPC or a network.
//
// They also read the message back from the buffer and check that the data is
// correct.
//
// The tests show that if you just copy the algorithms you use to create
// messages in protobuf, you will not get the full benefit of phaser.  You need
// to use the fact that phaser writes directly to the output buffer (and reads
// from it too) in order to gain the full performance benefits.
//
// This can make a huge difference to the performance of your system, especially
// when combined with a shared memory IPC system like Subspace.
// Please see https://github.com/dallison/subspace for more information.

TEST(PerfTest, ProtobufCameraImage) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
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
    size_t size = image.ByteSizeLong();
    ASSERT_TRUE(image.SerializeToArray(buffer.data(), buffer.size()));

    // Deserialize again and read the image data.
    robot::CameraImage image2;
    ASSERT_TRUE(image2.ParseFromArray(buffer.data(), size));
    ASSERT_EQ(image2.header().timestamp(), 1234567890);
    ASSERT_EQ(image2.rows(), kNumRows);
    ASSERT_EQ(image2.cols(), kNumCols);
    ASSERT_EQ(image2.image(), image_data);
  }
  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Protobuf serialization: %d ns\n", end - start);
}

// This uses a less optimal way to build the image in the buffer, similar to the
// protobuf version.  It's about the same speed as the protobuf version because
// we are copying the image data into the buffer rather than building it
// directly.
TEST(PerfTest, PhaserCameraImageCopy) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::phaser::CameraImage image =
        robot::phaser::CameraImage::CreateMutable(buffer.data(), buffer.size());
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
    // This will copy the image into the buffer.
    image.set_image(image_data);
    size_t size = image.Size();
    robot::phaser::CameraImage image2 =
        robot::phaser::CameraImage::CreateReadonly(buffer.data(), size);
    ASSERT_EQ(image2.header().timestamp(), 1234567890);
    ASSERT_EQ(image2.rows(), kNumRows);
    ASSERT_EQ(image2.cols(), kNumCols);
    ASSERT_EQ(image2.image(), image_data);
  }
  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Phaser string copy: %d ns\n", end - start);
}

// Highest performance version.  This builds the image directly in the buffer.
TEST(PerfTest, PhaserCameraImageZeroCopy) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
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
    size_t size = image.Size();

    robot::phaser::CameraImage image2 =
        robot::phaser::CameraImage::CreateReadonly(buffer.data(), size);
    ASSERT_EQ(image2.header().timestamp(), 1234567890);
    ASSERT_EQ(image2.rows(), kNumRows);
    ASSERT_EQ(image2.cols(), kNumCols);

    // Comparing against absl::Span<char> seems to be much slower than comparing
    // a std::string_view, so we convert the image data to a string_view.
    ASSERT_EQ(image2.image(),
              std::string_view(image_data.data(), image_data.size()));
  }
  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Phaser zero-copy: %d ns\n", end - start);
}

// Standard protobuf algorithm to create a message with a repeated field.
TEST(PerfTest, ProtobufLidarScan) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::LidarScan scan;
    scan.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumBeams = 1000000;
    for (int i = 0; i < kNumBeams; ++i) {
      scan.add_beams(i);
    }
    size_t size = scan.ByteSizeLong();

    ASSERT_TRUE(scan.SerializeToArray(buffer.data(), buffer.size()));

    // Deserialize and compare.
    robot::LidarScan scan2;
    ASSERT_TRUE(scan2.ParseFromArray(buffer.data(), size));
    ASSERT_EQ(scan2.header().timestamp(), 1234567890);
    ASSERT_EQ(scan2.beams_size(), kNumBeams);
    for (int i = 0; i < kNumBeams; ++i) {
      ASSERT_EQ(scan2.beams(i), i);
    }
  }

  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Protobuf serialization: %d ns\n", end - start);
}

// Phaser version of the protobuf algorithm, showing compatility with the
// protobuf API.
TEST(PerfTest, PhaserLidarScanPush) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::phaser::LidarScan scan =
        robot::phaser::LidarScan::CreateMutable(buffer.data(), buffer.size());
    scan.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumBeams = 1000000;
    scan.reserve_beams(kNumBeams);
    for (int i = 0; i < kNumBeams; ++i) {
      scan.add_beams(i);
    }
    // No serialization step, the message is built directly in the buffer.

    robot::phaser::LidarScan scan2 =
        robot::phaser::LidarScan::CreateReadonly(buffer.data(), scan.Size());
    ASSERT_EQ(scan2.header().timestamp(), 1234567890);
    ASSERT_EQ(scan2.beams_size(), kNumBeams);
    for (int i = 0; i < kNumBeams; ++i) {
      ASSERT_EQ(scan2.beams(i), i);
    }
  }

  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Phaser zero-copy: %d ns\n", end - start);
}

// This is a much faster version of PhaserLidarScanPush.
TEST(PerfTest, PhaserLidarScanZeroCopy) {
  std::vector<char> buffer(1024 * 1024 * 30);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::phaser::LidarScan scan =
        robot::phaser::LidarScan::CreateMutable(buffer.data(), buffer.size());
    scan.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumBeams = 1000000;
    scan.resize_beams(kNumBeams);
    // Get access to the actual memory in the buffer.
    absl::Span<double> beams = scan.beams_as_mutable_span();
    for (int i = 0; i < kNumBeams; ++i) {
      beams[i] = i;
    }

    robot::phaser::LidarScan scan2 =
        robot::phaser::LidarScan::CreateReadonly(buffer.data(), scan.Size());
    ASSERT_EQ(scan2.header().timestamp(), 1234567890);
    ASSERT_EQ(scan2.beams_size(), kNumBeams);
    absl::Span<const double> beams2 = scan2.beams_as_span();
    for (int i = 0; i < kNumBeams; ++i) {
      ASSERT_EQ(beams2[i], i);
    }
  }

  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Phaser zero-copy: %d ns\n", end - start);
}

// Standard protobuf algorithm to create a message with a repeated field of
// messages.
TEST(PerfTest, ProtobufAllLidars) {
  std::vector<char> buffer(1024 * 1024 * 100);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::AllLidars lidars;
    lidars.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumLidars = 100;
    constexpr int kNumBeams = 100000;
    for (int j = 0; j < kNumLidars; ++j) {
      robot::LidarScan *scan = lidars.add_scans();
      scan->mutable_header()->set_timestamp(1234567890);

      for (int k = 0; k < kNumBeams; ++k) {
        scan->add_beams(k);
      }
    }

    ASSERT_TRUE(lidars.SerializeToArray(buffer.data(), buffer.size()));

    // Deserialize and compare.
    robot::AllLidars lidars2;
    ASSERT_TRUE(lidars2.ParseFromArray(buffer.data(), lidars.ByteSizeLong()));
    ASSERT_EQ(lidars2.header().timestamp(), 1234567890);
    ASSERT_EQ(lidars2.scans_size(), kNumLidars);
    for (int j = 0; j < kNumLidars; ++j) {
      const robot::LidarScan &scan = lidars2.scans(j);
      ASSERT_EQ(scan.header().timestamp(), 1234567890);
      ASSERT_EQ(scan.beams_size(), kNumBeams);
      for (int k = 0; k < kNumBeams; ++k) {
        ASSERT_EQ(scan.beams(k), k);
      }
    }
  }

  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Protobuf serialization: %d ns\n", end - start);
}

// Same as protobuf algorithm, slower than protobuf because the allocator in the
// payload buffer is not as fast as regular malloc.  Not much point in doing
// this really.
TEST(PerfTest, PhaserAllLidarsPush) {
  std::vector<char> buffer(1024 * 1024 * 100);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::phaser::AllLidars lidars =
        robot::phaser::AllLidars::CreateMutable(buffer.data(), buffer.size());
    lidars.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumLidars = 100;
    constexpr int kNumBeams = 100000;
    lidars.reserve_scans(kNumLidars);
    for (int j = 0; j < kNumLidars; ++j) {
      robot::phaser::LidarScan *scan = lidars.add_scans();
      scan->mutable_header()->set_timestamp(1234567890);

      scan->reserve_beams(kNumBeams);
      for (int k = 0; k < kNumBeams; ++k) {
        scan->add_beams(k);
      }
    }

    // No serialization step, the message is built directly in the buffer.
    robot::phaser::AllLidars lidars2 =
        robot::phaser::AllLidars::CreateReadonly(buffer.data(), lidars.Size());
    ASSERT_EQ(lidars2.header().timestamp(), 1234567890);
    ASSERT_EQ(lidars2.scans_size(), kNumLidars);
    for (int j = 0; j < kNumLidars; ++j) {
      const robot::phaser::LidarScan &scan = lidars2.scans(j);
      ASSERT_EQ(scan.header().timestamp(), 1234567890);
      ASSERT_EQ(scan.beams_size(), kNumBeams);
      for (int k = 0; k < kNumBeams; ++k) {
        ASSERT_EQ(scan.beams(k), k);
      }
    }
  }

  uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Phaser push: %d ns\n", end - start);
}

// Optimized zero-copy version using absl::Span.  Runs much faster than
// the protobuf algorithm as it uses phaser-specific features to access the
// buffer memory directly.
TEST(PerfTest, PhaserAllLidarsZeroCopy) {
  std::vector<char> buffer(1024 * 1024 * 100);

  uint64_t start = toolbelt::Now();

  for (int i = 0; i < 100; i++) {
    robot::phaser::AllLidars lidars =
        robot::phaser::AllLidars::CreateMutable(buffer.data(), buffer.size());
    lidars.mutable_header()->set_timestamp(1234567890);

    constexpr int kNumLidars = 100;
    constexpr int kNumBeams = 100000;

    // Allocate all the scans at once.
    std::vector<robot::phaser::LidarScan *> lidar_scans =
        lidars.allocate_scans(kNumLidars);
    for (auto scan : lidar_scans) {
      scan->mutable_header()->set_timestamp(1234567890);

      scan->resize_beams(kNumBeams);
      absl::Span<double> beams = scan->beams_as_mutable_span();
      for (int i = 0; i < kNumBeams; ++i) {
        beams[i] = i;
      }
    }

    robot::phaser::AllLidars lidars2 =
        robot::phaser::AllLidars::CreateReadonly(buffer.data(), lidars.Size());
    ASSERT_EQ(lidars2.header().timestamp(), 1234567890);
    ASSERT_EQ(lidars2.scans_size(), kNumLidars);
    auto &scans = lidars2.scans();
    for (int j = 0; j < kNumLidars; ++j) {
      const robot::phaser::LidarScan &scan = *(scans[j]);
      ASSERT_EQ(scan.header().timestamp(), 1234567890);
      ASSERT_EQ(scan.beams_size(), kNumBeams);
      absl::Span<const double> beams = scan.beams_as_span();
      for (int k = 0; k < kNumBeams; ++k) {
        ASSERT_EQ(beams[k], k);
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
