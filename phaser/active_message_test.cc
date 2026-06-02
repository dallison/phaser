// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/runtime.h"
#include "phaser/testdata/Foo.phaser.h"
#include <any>
#include <gtest/gtest.h>
#include <string>

// Verifies the optional `std::any active_message` field that is emitted when a
// phaser_library is built with enable_active_message = True (which passes the
// active_message=true plugin command-line option).
TEST(ActiveMessage, DefaultsToEmpty) {
  char *buffer = static_cast<char *>(calloc(4096, 1));
  foo::bar::phaser::Foo msg =
      foo::bar::phaser::Foo::CreateMutable(buffer, 4096);
  EXPECT_FALSE(msg.active_message.has_value());
  free(buffer);
}

TEST(ActiveMessage, HoldsArbitraryPayload) {
  char *buffer = static_cast<char *>(calloc(4096, 1));
  foo::bar::phaser::Foo msg =
      foo::bar::phaser::Foo::CreateMutable(buffer, 4096);

  // The field is a public std::any, independent of the wire-format fields.
  msg.set_a(42);
  msg.active_message = std::string("attached-payload");

  ASSERT_TRUE(msg.active_message.has_value());
  EXPECT_EQ("attached-payload",
            std::any_cast<std::string>(msg.active_message));
  EXPECT_EQ(42, msg.a());

  msg.active_message.reset();
  EXPECT_FALSE(msg.active_message.has_value());
  free(buffer);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
