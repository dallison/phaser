// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/md5.h"

#include <gtest/gtest.h>

#include <string>

namespace phaser {
namespace {

TEST(Md5Test, MatchesPublishedTestVectors) {
  EXPECT_EQ(Md5(""), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(Md5("a"), "0cc175b9c0f1b6a831c399e269772661");
  EXPECT_EQ(Md5("abc"), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(Md5("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
  EXPECT_EQ(Md5("abcdefghijklmnopqrstuvwxyz"),
            "c3fcd3d76192e4007dfb496cca67e13b");
  EXPECT_EQ(
      Md5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),
      "d174ab98d277d9f5a5611c2c9f419d9f");
  EXPECT_EQ(
      Md5("123456789012345678901234567890123456789012345678901234567890123"
          "45678901234567890"),
      "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(Md5Test, HandlesMultipleCompleteBlocks) {
  EXPECT_EQ(Md5(std::string(1'000'000, 'a')),
            "7707d6ae4e027c70eea2a935c2296f21");
}

}  // namespace
}  // namespace phaser
