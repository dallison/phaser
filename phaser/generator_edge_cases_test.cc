#include <string>

#include "gtest/gtest.h"
#include "phaser/testdata/GeneratorEdgeCases.phaser.h"

namespace {

using foo::bar::phaser::Holder;
using foo::bar::phaser::Limits;

TEST(GeneratorEdgeCasesTest, MessagesDefinedLaterRoundTrip) {
  Holder holder;
  holder.mutable_later()->set_value(1);
  holder.mutable_later()->set_name("later");
  holder.add_laters().set_value(2);
  holder.add_laters().set_value(3);
  holder.mutable_choice_later()->set_value(4);
  holder.mutable_nested()->mutable_later()->set_value(5);
  holder.mutable_other_inner()->mutable_later()->set_name("inner");

  const std::string wire = holder.SerializeAsString();
  ASSERT_FALSE(wire.empty());

  Holder parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ(parsed.later().value(), 1);
  EXPECT_EQ(parsed.later().name(), "later");
  ASSERT_EQ(parsed.laters_size(), 2u);
  EXPECT_EQ(parsed.laters(0).value(), 2);
  EXPECT_EQ(parsed.laters(1).value(), 3);
  ASSERT_TRUE(parsed.has_choice_later());
  EXPECT_EQ(parsed.choice_later().value(), 4);
  EXPECT_EQ(parsed.nested().later().value(), 5);
  EXPECT_EQ(parsed.other_inner().later().name(), "inner");
  EXPECT_EQ(parsed.SerializeAsString(), wire);
}

TEST(GeneratorEdgeCasesTest, FieldNamedLikeAPresenceAccessor) {
  Limits limits;
  EXPECT_FALSE(limits._phaser_has_limit());
  EXPECT_FALSE(limits._phaser_has_later());

  limits.set_has_limit(true);
  limits.set_limit(3);
  limits.mutable_has_later()->set_value(6);
  limits.mutable_later()->set_value(7);

  EXPECT_TRUE(limits.has_limit());
  EXPECT_EQ(limits.limit(), 3);
  EXPECT_TRUE(limits._phaser_has_limit());
  EXPECT_TRUE(limits._phaser_has_later());

  Limits parsed;
  ASSERT_TRUE(parsed.ParseFromString(limits.SerializeAsString()));
  EXPECT_TRUE(parsed.has_limit());
  EXPECT_EQ(parsed.limit(), 3);
  EXPECT_EQ(parsed.has_later().value(), 6);
  EXPECT_EQ(parsed.later().value(), 7);
}

}  // namespace
