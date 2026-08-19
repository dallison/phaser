// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "phaser/runtime/message.h"
#include "phaser/testdata/TestMessage.phaser.h"
#include "test_helpers.h"

namespace {

using foo::bar::phaser::TestMessage;

// Dynamic buffer: repeated strings until a large count (exercises growth).
TEST(StressTest, DynamicStringPressure) {
  TestMessage msg(512);
  ASSERT_FALSE(msg.allocate_buffer(64 * 1024).empty());
  for (int i = 0; i < 200; i++) {
    msg.add_vstr(::phaser::test::MakePatternString(64, 's'));
  }
  EXPECT_GE(msg.vstr_size(), 150u);
}

TEST(StressTest, FixedBufferTuningSizeMode) {
  constexpr size_t kBufSize = 4096;
  char* buffer = static_cast<char*>(calloc(kBufSize, 1));
  ASSERT_NE(nullptr, buffer);

  TestMessage msg =
      TestMessage::CreateMutable(buffer, kBufSize, ::phaser::Tuning::kSize);
  for (int i = 0; i < 200; i++) {
    msg.add_vi32(i);
  }
  EXPECT_GE(msg.vi32_size(), 100u);
  free(buffer);
}

TEST(StressTest, DynamicBufferGrowsAndPreservesScalars) {
  TestMessage msg(256);
  msg.set_x(1234);
  msg.set_s("seed");
  ASSERT_FALSE(msg.allocate_buffer(64 * 1024).empty());

  for (int i = 0; i < 200; i++) {
    msg.add_vstr(::phaser::test::MakePatternString(48, 'v'));
  }
  EXPECT_EQ(1234, msg.x());
  EXPECT_EQ("seed", msg.s());
  EXPECT_GE(msg.vstr_size(), 150u);
}

TEST(StressTest, DynamicExplicitBufferExpansion) {
  TestMessage msg(512);
  msg.set_s("expand-me");
  auto span = msg.allocate_buffer(32 * 1024);
  ASSERT_FALSE(span.empty());
  EXPECT_GE(span.size(), 32u * 1024u);
  EXPECT_EQ("expand-me", msg.s());
}

TEST(StressTest, FinalizeSetsPayloadSize) {
  TestMessage msg(512);
  msg.set_s("finalize-me");
  auto* payload = reinterpret_cast<::toolbelt::PayloadBuffer*>(msg.Data());
  payload->full_size = 0;

  msg.Finalize();

  EXPECT_EQ(payload->full_size, payload->hwm);
}

TEST(StressTest, AllocFailsAtStart) {
  auto status = ::phaser::NewDynamicBuffer(
      1024, ::phaser::test::AllocUntilLimit(0),
      ::phaser::test::ReallocAlwaysFails(), ::phaser::Tuning::kPerformance);
  EXPECT_FALSE(status.ok());
}

TEST(StressTest, CustomAllocSucceeds) {
  TestMessage msg = TestMessage::CreateDynamicMutable(
      512, ::phaser::test::AllocUntilLimit(64 * 1024), [](void* p) { free(p); },
      [](void* p, size_t, size_t new_size) -> absl::StatusOr<void*> {
        void* r = realloc(p, new_size);
        if (r == nullptr) {
          return absl::ResourceExhaustedError("realloc failed");
        }
        return r;
      });
  msg.set_s("custom-alloc");
  EXPECT_EQ("custom-alloc", msg.s());
}

TEST(StressTest, TryCreateCustomAllocSucceeds) {
  auto result = TestMessage::TryCreateDynamicMutable(
      512, ::phaser::test::AllocUntilLimit(64 * 1024), [](void* p) { free(p); },
      [](void* p, size_t, size_t new_size) -> absl::StatusOr<void*> {
        void* r = realloc(p, new_size);
        if (r == nullptr) {
          return absl::ResourceExhaustedError("realloc failed");
        }
        return r;
      });
  ASSERT_TRUE(result.ok()) << result.status();
  TestMessage msg = std::move(*result);
  msg.set_s("fallible-custom-alloc");
  EXPECT_EQ("fallible-custom-alloc", msg.s());
}

TEST(StressTest, TryCreateReportsInitialAllocationFailure) {
  auto result = TestMessage::TryCreateDynamicMutable(
      512, ::phaser::test::AllocUntilLimit(0), [](void* p) { free(p); },
      ::phaser::test::ReallocAlwaysFails());
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kResourceExhausted);
}

TEST(StressTest, ReallocFailureAborts) {
  EXPECT_DEATH(
      {
        TestMessage msg = TestMessage::CreateDynamicMutable(
            256, ::phaser::test::AllocUntilLimit(1024 * 1024),
            [](void* p) { free(p); }, ::phaser::test::ReallocAlwaysFails());
        for (int i = 0; i < 2000; i++) {
          msg.add_vstr(::phaser::test::MakePatternString(256, 'x'));
        }
      },
      "Failed to resize PayloadBuffer");
}

TEST(StressTest, RepeatedStringResizeAndReplace) {
  TestMessage msg(1024);
  ASSERT_FALSE(msg.allocate_buffer(64 * 1024).empty());
  msg.resize_vstr(200);
  for (size_t i = 0; i < 200; i++) {
    msg.set_vstr(i, ::phaser::test::MakePatternString(
                        16, static_cast<char>('a' + (i % 26))));
  }
  msg.clear_vstr();
  EXPECT_EQ(0u, msg.vstr_size());
  for (int i = 0; i < 200; i++) {
    msg.add_vstr("x");
  }
  EXPECT_EQ(200u, msg.vstr_size());
}

TEST(StressTest, MapManyEntries) {
  TestMessage msg(4096);
  ASSERT_FALSE(msg.allocate_buffer(128 * 1024).empty());
  for (int i = 0; i < 500; i++) {
    auto e = msg.add_values();
    e->set_key(::phaser::test::MakePatternString(
        8, static_cast<char>('k' + (i % 10))));
    e->set_value(i);
  }
  ASSERT_EQ(500u, msg.values_size());
  for (int i = 0; i < 500; i++) {
    EXPECT_EQ(i, msg.values(static_cast<size_t>(i)).value());
  }
}

}  // namespace
