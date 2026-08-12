// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// It would be in your interests to build this optimized or you will be
// waiting a while...

#include <gtest/gtest.h>

#include <sstream>

#include "absl/strings/str_format.h"
#include "phaser/runtime/runtime.h"
#include "phaser/testdata/TestMessage.phaser.h"
#include "phaser/testdata/vision.pb.h"
#include "phaser/testdata/vision.phaser.h"
#include "toolbelt/clock.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"

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
      robot::LidarScan* scan = lidars.add_scans();
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
      const robot::LidarScan& scan = lidars2.scans(j);
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
      auto scan = lidars.add_scans();
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
      const robot::phaser::LidarScan& scan = lidars2.scans(j);
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
    std::vector<robot::phaser::LidarScan> lidar_scans =
        lidars.allocate_scans(kNumLidars);
    for (auto& scan : lidar_scans) {
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
    auto& scans = lidars2.scans();
    for (int j = 0; j < kNumLidars; ++j) {
      const robot::phaser::LidarScan scan = scans[j];
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

TEST(PerfTest, HybridFieldLookup) {
  foo::bar::phaser::HybridLookupMessage hybrid;
  foo::bar::phaser::SparseLookupMessage sparse;
  constexpr int kIterations = 10000000;
  volatile int64_t result = 0;

  uint64_t start = toolbelt::Now();
  for (int i = 0; i < kIterations; ++i) {
    result += hybrid.FindField(10).offset;
  }
  uint64_t dense_end = toolbelt::Now();

  for (int i = 0; i < kIterations; ++i) {
    result += sparse.FindField(10000).offset;
  }
  uint64_t sparse_end = toolbelt::Now();

  std::cout << absl::StrFormat("Dense direct lookup: %d ns\n",
                               dense_end - start);
  std::cout << absl::StrFormat("Sparse binary lookup: %d ns\n",
                               sparse_end - dense_end);
  EXPECT_NE(result, 0);
}

TEST(PerfTest, DenseVsSparsePhaserMessageReadsAndWrites) {
  using Dense = foo::bar::phaser::DenseLookupBenchmarkMessage;
  using Sparse = foo::bar::phaser::SparseLookupBenchmarkMessage;

  std::vector<char> dense_storage(64 * 1024);
  std::vector<char> sparse_storage(64 * 1024);
  Dense dense_seed =
      Dense::CreateMutable(dense_storage.data(), dense_storage.size());
  Sparse sparse_seed =
      Sparse::CreateMutable(sparse_storage.data(), sparse_storage.size());

  const auto write_fields = [](auto& message, int32_t base) {
    message.set_value_01(base + 1);
    message.set_value_02(base + 2);
    message.set_value_03(base + 3);
    message.set_value_04(base + 4);
    message.set_value_05(base + 5);
    message.set_value_06(base + 6);
    message.set_value_07(base + 7);
    message.set_value_08(base + 8);
    message.set_value_09(base + 9);
    message.set_value_10(base + 10);
    message.set_value_11(base + 11);
    message.set_value_12(base + 12);
    message.set_value_13(base + 13);
    message.set_value_14(base + 14);
    message.set_value_15(base + 15);
    message.set_value_16(base + 16);
    message.set_value_17(base + 17);
    message.set_value_18(base + 18);
    message.set_value_19(base + 19);
    message.set_value_20(base + 20);
    message.set_value_21(base + 21);
    message.set_value_22(base + 22);
    message.set_value_23(base + 23);
    message.set_value_24(base + 24);
    message.set_value_25(base + 25);
    message.set_value_26(base + 26);
    message.set_value_27(base + 27);
    message.set_value_28(base + 28);
    message.set_value_29(base + 29);
    message.set_value_30(base + 30);
    message.set_value_31(base + 31);
    message.set_value_32(base + 32);
    message.set_value_33(base + 33);
    message.set_value_34(base + 34);
    message.set_value_35(base + 35);
    message.set_value_36(base + 36);
    message.set_value_37(base + 37);
    message.set_value_38(base + 38);
    message.set_value_39(base + 39);
    message.set_value_40(base + 40);
    message.set_value_41(base + 41);
    message.set_value_42(base + 42);
    message.set_value_43(base + 43);
    message.set_value_44(base + 44);
    message.set_value_45(base + 45);
    message.set_value_46(base + 46);
    message.set_value_47(base + 47);
    message.set_value_48(base + 48);
    message.set_value_49(base + 49);
    message.set_value_50(base + 50);
    message.set_value_51(base + 51);
    message.set_value_52(base + 52);
    message.set_value_53(base + 53);
    message.set_value_54(base + 54);
    message.set_value_55(base + 55);
    message.set_value_56(base + 56);
    message.set_value_57(base + 57);
    message.set_value_58(base + 58);
    message.set_value_59(base + 59);
    message.set_value_60(base + 60);
    message.set_value_61(base + 61);
    message.set_value_62(base + 62);
    message.set_value_63(base + 63);
    message.set_value_64(base + 64);
  };
  const auto read_fields = [](const auto& message) -> int64_t {
    return static_cast<int64_t>(message.value_01()) + message.value_02() +
           message.value_03() + message.value_04() + message.value_05() +
           message.value_06() + message.value_07() + message.value_08() +
           message.value_09() + message.value_10() + message.value_11() +
           message.value_12() + message.value_13() + message.value_14() +
           message.value_15() + message.value_16() + message.value_17() +
           message.value_18() + message.value_19() + message.value_20() +
           message.value_21() + message.value_22() + message.value_23() +
           message.value_24() + message.value_25() + message.value_26() +
           message.value_27() + message.value_28() + message.value_29() +
           message.value_30() + message.value_31() + message.value_32() +
           message.value_33() + message.value_34() + message.value_35() +
           message.value_36() + message.value_37() + message.value_38() +
           message.value_39() + message.value_40() + message.value_41() +
           message.value_42() + message.value_43() + message.value_44() +
           message.value_45() + message.value_46() + message.value_47() +
           message.value_48() + message.value_49() + message.value_50() +
           message.value_51() + message.value_52() + message.value_53() +
           message.value_54() + message.value_55() + message.value_56() +
           message.value_57() + message.value_58() + message.value_59() +
           message.value_60() + message.value_61() + message.value_62() +
           message.value_63() + message.value_64();
  };

  write_fields(dense_seed, 0);
  write_fields(sparse_seed, 0);
  const size_t dense_size = dense_seed.Size();
  const size_t sparse_size = sparse_seed.Size();

  constexpr int kFirstTouchIterations = 1000000;
  constexpr int kIterations = 5000000;
  constexpr int kFieldsPerIteration = 64;
  volatile int64_t checksum = 0;

  uint64_t first_touch_start = toolbelt::Now();
  for (int i = 0; i < kFirstTouchIterations; ++i) {
    const Dense message =
        Dense::CreateReadonly(dense_storage.data(), dense_size);
    asm volatile("" ::: "memory");
    checksum += read_fields(message);
  }
  const uint64_t dense_first_touch_end = toolbelt::Now();

  for (int i = 0; i < kFirstTouchIterations; ++i) {
    const Sparse message =
        Sparse::CreateReadonly(sparse_storage.data(), sparse_size);
    asm volatile("" ::: "memory");
    checksum += read_fields(message);
  }
  const uint64_t sparse_first_touch_end = toolbelt::Now();

  const Dense dense_message =
      Dense::CreateReadonly(dense_storage.data(), dense_size);
  const Sparse sparse_message =
      Sparse::CreateReadonly(sparse_storage.data(), sparse_size);

  // Resolve each proxy once before timing. The benchmark below measures field
  // reads and writes on already-constructed Phaser handles, not construction or
  // first-access metadata lookup.
  checksum += read_fields(dense_message);
  checksum += read_fields(sparse_message);

  const uint64_t cached_start = toolbelt::Now();
  for (int i = 0; i < kIterations; ++i) {
    asm volatile("" ::: "memory");
    checksum += read_fields(dense_message);
  }
  const uint64_t dense_read_end = toolbelt::Now();

  for (int i = 0; i < kIterations; ++i) {
    asm volatile("" ::: "memory");
    checksum += read_fields(sparse_message);
  }
  const uint64_t sparse_read_end = toolbelt::Now();

  for (int i = 0; i < kIterations; ++i) {
    write_fields(dense_seed, i);
    asm volatile("" ::: "memory");
  }
  const uint64_t dense_write_end = toolbelt::Now();

  for (int i = 0; i < kIterations; ++i) {
    write_fields(sparse_seed, i);
    asm volatile("" ::: "memory");
  }
  const uint64_t sparse_write_end = toolbelt::Now();

  const auto report = [&](const char* label, uint64_t elapsed, int iterations) {
    const double ns_per_field =
        static_cast<double>(elapsed) /
        static_cast<double>(iterations * kFieldsPerIteration);
    std::cout << absl::StrFormat("%s: %d ns total, %.3f ns/field\n", label,
                                 elapsed, ns_per_field);
  };
  report("Dense first-touch Phaser reads",
         dense_first_touch_end - first_touch_start, kFirstTouchIterations);
  report("Sparse first-touch Phaser reads",
         sparse_first_touch_end - dense_first_touch_end, kFirstTouchIterations);
  report("Dense cached Phaser reads", dense_read_end - cached_start,
         kIterations);
  report("Sparse cached Phaser reads", sparse_read_end - dense_read_end,
         kIterations);
  report("Dense Phaser writes", dense_write_end - sparse_read_end, kIterations);
  report("Sparse Phaser writes", sparse_write_end - dense_write_end,
         kIterations);

  checksum += dense_seed.value_64();
  checksum += sparse_seed.value_64();
  EXPECT_GT(checksum, 0);
}

TEST(PerfTest, AllocationFreeReceiveTree) {
  std::vector<char> buffer(1024 * 1024);
  auto source =
      robot::phaser::AllLidars::CreateMutable(buffer.data(), buffer.size());
  constexpr int kNumLidars = 64;
  constexpr int kNumBeams = 128;
  for (int i = 0; i < kNumLidars; ++i) {
    auto scan = source.add_scans();
    scan->resize_beams(kNumBeams);
    absl::Span<double> beams = scan->beams_as_mutable_span();
    for (int j = 0; j < kNumBeams; ++j) {
      beams[j] = i + j;
    }
  }
  const size_t payload_size = source.Size();

  constexpr int kIterations = 10000;
  volatile double checksum = 0;
  const uint64_t start = toolbelt::Now();
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    const auto received =
        robot::phaser::AllLidars::CreateReadonly(buffer.data(), payload_size);
    for (auto scan : received.scans()) {
      const absl::Span<const double> beams = scan.beams_as_span();
      checksum += beams.front();
      checksum += beams.back();
    }
  }
  const uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Allocation-free receive tree: %d ns\n",
                               end - start);
  EXPECT_GT(checksum, 0);
}

TEST(PerfTest, AllocationFreeFixedBufferConstruction) {
  std::vector<char> buffer(1024 * 1024);
  constexpr int kIterations = 10000;
  volatile size_t checksum = 0;
  const uint64_t start = toolbelt::Now();
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    auto message = foo::bar::phaser::TestMessage::CreateMutable(buffer.data(),
                                                                buffer.size());
    message.set_x(iteration);
    message.set_s("fixed-buffer");
    message.add_vi32(iteration);
    message.add_vstr("value");
    message.add_vm()->set_str("nested");
    auto embedded =
        message.mutable_any()->MutableAny<foo::bar::phaser::InnerMessage>();
    embedded.set_str("any");
    checksum += message.Size();
  }
  const uint64_t end = toolbelt::Now();
  std::cout << absl::StrFormat("Allocation-free fixed output: %d ns\n",
                               end - start);
  EXPECT_GT(checksum, 0u);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
