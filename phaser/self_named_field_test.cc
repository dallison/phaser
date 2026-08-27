#include <string>

#include "gtest/gtest.h"
#include "phaser/testdata/SelfNamedField.phaser.h"
#include "phaser/testdata/self_named_field_ros_phaser/phaser/testdata/SelfNamedField.phaser.h"

namespace selfnamed {
namespace {

// The protobuf frontend normally puts the accessor at the field name and the
// member one underscore past it. A self-named field pushes both along by one so
// the accessor stops being a constructor.
TEST(SelfNamedFieldTest, ProtobufFrontendShiftsTheAccessorPastTheClassName) {
  phaser::Polygon polygon;
  phaser::Point point = polygon.add_Polygon();
  point.set_x(1.5);
  point.set_y(-2.5);

  ASSERT_EQ(1u, polygon.Polygon_size());
  EXPECT_EQ(1.5, polygon.Polygon_(0).x());
  EXPECT_EQ(-2.5, polygon.Polygon_(0).y());
}

TEST(SelfNamedFieldTest, ProtobufFrontendHandlesScalarSelfNamedField) {
  phaser::Label label;
  label.set_Label("stop");
  label.set_index(3);
  EXPECT_EQ("stop", label.Label_());
  EXPECT_EQ(3, label.index());
}

// The ROS frontend exposes the member itself, so only the member has to move.
TEST(SelfNamedFieldTest, RosFrontendSuffixesTheCollidingMember) {
  phaser_ros::Polygon polygon;
  auto point = polygon.Polygon_.Add();
  point.x = 4.0;
  point.y = 5.0;

  ASSERT_EQ(1u, polygon.Polygon_.size());
  EXPECT_EQ(4.0, polygon.Polygon_[0].x);
  EXPECT_EQ(5.0, polygon.Polygon_[0].y);
}

TEST(SelfNamedFieldTest, RosFrontendLeavesOtherMembersAlone) {
  phaser_ros::Label label;
  label.Label_ = "go";
  label.index = 7;
  EXPECT_EQ("go", label.Label_.Get());
  EXPECT_EQ(7, label.index);
}

// Nested messages generate as `Outer_Inner`, so that is the name a member has
// to stay clear of, not the proto-level `Inner`.
TEST(SelfNamedFieldTest, RosFrontendUsesTheFlattenedNestedClassName) {
  phaser_ros::Outer_Inner inner;
  inner.Outer_Inner_ = 11;
  inner.keep = 12;
  EXPECT_EQ(11, inner.Outer_Inner_);
  EXPECT_EQ(12, inner.keep);
}

TEST(SelfNamedFieldTest, SelfNamedFieldSurvivesAWireRoundtrip) {
  phaser_ros::Label label;
  label.Label_ = "yield";
  label.index = 2;

  const std::string wire = label.SerializeAsString();

  phaser::Label parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ("yield", parsed.Label_());
  EXPECT_EQ(2, parsed.index());
}

TEST(SelfNamedFieldTest, FieldNamedTDoesNotCollideWithCloneFromsTemplate) {
  phaser_ros::Horizon source;
  source.T = 20;
  source.steps.push_back(0.5);

  phaser_ros::Horizon copy;
  ASSERT_TRUE(copy.CloneFrom(source).ok());
  EXPECT_EQ(20, copy.T);
  ASSERT_EQ(1u, copy.steps.size());
  EXPECT_EQ(0.5, copy.steps[0]);
}

TEST(SelfNamedFieldTest, CloneFromCopiesASelfNamedField) {
  phaser::Label source;
  source.set_Label("merge");
  source.set_index(5);

  phaser::Label copy;
  ASSERT_TRUE(copy.CloneFrom(source).ok());
  EXPECT_EQ("merge", copy.Label_());
  EXPECT_EQ(5, copy.index());
}

}  // namespace
}  // namespace selfnamed
