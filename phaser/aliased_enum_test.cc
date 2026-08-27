#include <string>

#include "gtest/gtest.h"
#include "phaser/testdata/AliasedEnum.phaser.h"

namespace alias::phaser {
namespace {

TEST(AliasedEnumTest, AliasesCompareEqualToTheirPrimaryName) {
  EXPECT_EQ(SIGNAL_UNSPECIFIED, SIGNAL_IDLE);
  EXPECT_EQ(SIGNAL_BUSY, SIGNAL_WORKING);
}

// Protobuf's own _Name reports the first name declared for a number, so the
// stringizer keeps that case and drops the aliases.
TEST(AliasedEnumTest, StringizerReportsTheFirstNameForANumber) {
  EXPECT_EQ("SIGNAL_UNSPECIFIED", Signal_Name(SIGNAL_IDLE));
  EXPECT_EQ("SIGNAL_BUSY", Signal_Name(SIGNAL_WORKING));
  EXPECT_EQ("SIGNAL_DONE", Signal_Name(SIGNAL_DONE));
}

TEST(AliasedEnumTest, ParserAcceptsEveryAlias) {
  Signal parsed;
  Signal_Parse("SIGNAL_IDLE", &parsed);
  EXPECT_EQ(SIGNAL_IDLE, parsed);

  Signal_Parse("SIGNAL_WORKING", &parsed);
  EXPECT_EQ(SIGNAL_BUSY, parsed);
}

TEST(AliasedEnumTest, FieldsRoundTrip) {
  Job job;
  job.set_signal(SIGNAL_WORKING);
  job.add_history(SIGNAL_IDLE);
  job.add_history(SIGNAL_DONE);

  const std::string wire = job.SerializeAsString();

  Job parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ(SIGNAL_BUSY, parsed.signal());
  ASSERT_EQ(2u, parsed.history_size());
  EXPECT_EQ(SIGNAL_UNSPECIFIED, parsed.history(0));
  EXPECT_EQ(SIGNAL_DONE, parsed.history(1));
}

}  // namespace
}  // namespace alias::phaser
