// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "phaser/runtime/runtime.h"
#include "phaser/testdata/Foo.phaser.h"

// Verifies the optional `active_message` field that is emitted when a
// phaser_library is built with enable_active_message = True (which passes the
// active_message=true plugin command-line option).
static_assert(std::is_convertible_v<phaser::ActiveMessage&, std::any&>);
static_assert(
    std::is_convertible_v<const phaser::ActiveMessage&, const std::any&>);

TEST(ActiveMessage, DefaultsToEmpty) {
  char* buffer = static_cast<char*>(calloc(4096, 1));
  foo::bar::phaser::Foo msg =
      foo::bar::phaser::Foo::CreateMutable(buffer, 4096);
  EXPECT_FALSE(msg.active_message.has_value());
  EXPECT_EQ(msg.active_message.type(), typeid(void));
  free(buffer);
}

TEST(ActiveMessage, HoldsArbitraryPayload) {
  char* buffer = static_cast<char*>(calloc(4096, 1));
  foo::bar::phaser::Foo msg =
      foo::bar::phaser::Foo::CreateMutable(buffer, 4096);

  // The field is independent of the wire-format fields.
  msg.set_a(42);
  msg.active_message = std::string("attached-payload");

  ASSERT_TRUE(msg.active_message.has_value());
  EXPECT_EQ(msg.active_message.type(), typeid(std::string));
  EXPECT_EQ("attached-payload", std::any_cast<std::string>(msg.active_message));
  EXPECT_EQ(42, msg.a());

  msg.active_message.reset();
  EXPECT_FALSE(msg.active_message.has_value());
  free(buffer);
}

TEST(ActiveMessage, AcceptsAndExposesStdAny) {
  phaser::ActiveMessage active;
  std::any value = std::make_shared<int>(42);
  active = value;
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(*std::any_cast<std::shared_ptr<int>>(active), 42);

  active = std::any(std::string("moved"));
  const phaser::ActiveMessage& view = active;
  EXPECT_EQ(std::any_cast<const std::string&>(view), "moved");
  EXPECT_EQ(std::any_cast<std::string>(&static_cast<const std::any&>(view)),
            &std::any_cast<std::string&>(active));

  std::any& as_any = active;
  as_any = 7;
  EXPECT_EQ(std::any_cast<int>(active), 7);

  // An empty std::any assigned in leaves it without a value.
  active = std::any();
  EXPECT_FALSE(active.has_value());
}

TEST(ActiveMessage, EmptyConstViewHasNoValue) {
  const phaser::ActiveMessage active;
  const std::any& view = active;
  EXPECT_FALSE(view.has_value());
  EXPECT_THROW((void)std::any_cast<int>(active), std::bad_any_cast);
}

TEST(ActiveMessage, Emplace) {
  phaser::ActiveMessage active;
  std::string& value = active.emplace<std::string>(3, 'x');
  EXPECT_EQ(value, "xxx");
  EXPECT_EQ(std::any_cast<std::string&>(active), "xxx");
}

TEST(ActiveMessage, CopyIsDeepAndMoveTransfers) {
  phaser::ActiveMessage original;
  original = std::string("payload");

  phaser::ActiveMessage copy(original);
  std::any_cast<std::string&>(copy) = "changed";
  EXPECT_EQ(std::any_cast<std::string>(original), "payload");
  EXPECT_EQ(std::any_cast<std::string>(copy), "changed");

  phaser::ActiveMessage assigned;
  assigned = original;
  EXPECT_EQ(std::any_cast<std::string>(assigned), "payload");
  assigned = phaser::ActiveMessage();
  EXPECT_FALSE(assigned.has_value());

  phaser::ActiveMessage moved(std::move(original));
  EXPECT_EQ(std::any_cast<std::string>(moved), "payload");
  EXPECT_FALSE(original.has_value());

  assigned = std::move(moved);
  EXPECT_EQ(std::any_cast<std::string>(assigned), "payload");
  EXPECT_FALSE(moved.has_value());

  copy.swap(assigned);
  EXPECT_EQ(std::any_cast<std::string>(copy), "payload");
  EXPECT_EQ(std::any_cast<std::string>(assigned), "changed");
}

TEST(ActiveMessage, CopiesWithTheMessage) {
  char* buffer = static_cast<char*>(calloc(4096, 1));
  foo::bar::phaser::Foo msg =
      foo::bar::phaser::Foo::CreateMutable(buffer, 4096);
  msg.active_message = 5;
  foo::bar::phaser::Foo copy = msg;
  EXPECT_EQ(std::any_cast<int>(copy.active_message), 5);
  free(buffer);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
