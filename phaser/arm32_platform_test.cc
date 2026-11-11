// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <cstddef>

#include "gtest/gtest.h"
#include "phaser/runtime/md5.h"

namespace phaser {
namespace {

bool IsArm32() {
#if defined(__arm__) && !defined(__aarch64__)
  return true;
#else
  return false;
#endif
}

TEST(Arm32PlatformTest, ThirtyTwoBitPointers) {
  if (!IsArm32()) {
    GTEST_SKIP() << "armhf-only";
  }
  EXPECT_EQ(sizeof(void*), 4u);
}

TEST(Arm32PlatformTest, Md5Digest) {
  if (!IsArm32()) {
    GTEST_SKIP() << "armhf-only";
  }
  const std::string digest = Md5("phaser-arm32");
  EXPECT_EQ(digest.size(), 32u);
  EXPECT_EQ(digest, "6c87cfacf0231472239d82855e2a0f08");
}

}  // namespace
}  // namespace phaser
