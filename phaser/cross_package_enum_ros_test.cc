#include <string>

#include "gtest/gtest.h"
#include "phaser/testdata/cross_package_enum_ros_phaser/phaser/testdata/CrossPackageEnumUser.phaser.h"

namespace canvas::phaser_ros {
namespace {

using ::palette::phaser_ros::Color;
using ::palette::phaser_ros::COLOR_BLUE;
using ::palette::phaser_ros::COLOR_GREEN;
using ::palette::phaser_ros::COLOR_RED;
using ::palette::phaser_ros::Shade_Depth_DEPTH_DARK;

TEST(CrossPackageEnumRosTest, SingularFieldRoundTrips) {
  Drawing drawing;
  drawing.background = COLOR_BLUE;
  EXPECT_EQ(COLOR_BLUE, drawing.background);
}

TEST(CrossPackageEnumRosTest, NestedEnumFieldRoundTrips) {
  Drawing drawing;
  drawing.depth = Shade_Depth_DEPTH_DARK;
  EXPECT_EQ(Shade_Depth_DEPTH_DARK, drawing.depth);
}

TEST(CrossPackageEnumRosTest, RepeatedFieldRoundTrips) {
  Drawing drawing;
  drawing.strokes.push_back(COLOR_RED);
  drawing.strokes.push_back(COLOR_GREEN);
  ASSERT_EQ(2u, drawing.strokes.size());
  EXPECT_EQ(COLOR_RED, drawing.strokes[0]);
  EXPECT_EQ(COLOR_GREEN, drawing.strokes[1]);
}

TEST(CrossPackageEnumRosTest, FixedArrayFieldRoundTrips) {
  Drawing drawing;
  drawing.corners[0] = COLOR_GREEN;
  drawing.corners[3] = COLOR_BLUE;
  EXPECT_EQ(COLOR_GREEN, drawing.corners[0]);
  EXPECT_EQ(COLOR_BLUE, drawing.corners[3]);
}

TEST(CrossPackageEnumRosTest, StringizerAndParserResolveAcrossPackages) {
  EXPECT_EQ("COLOR_GREEN", ::palette::phaser_ros::Color_Name(COLOR_GREEN));

  Color parsed;
  ::palette::phaser_ros::Color_Parse("COLOR_BLUE", &parsed);
  EXPECT_EQ(COLOR_BLUE, parsed);
}

TEST(CrossPackageEnumRosTest, SerializesAndParses) {
  Drawing drawing;
  drawing.background = COLOR_BLUE;
  drawing.strokes.push_back(COLOR_RED);

  const std::string wire = drawing.SerializeAsString();

  Drawing parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ(COLOR_BLUE, parsed.background);
  ASSERT_EQ(1u, parsed.strokes.size());
  EXPECT_EQ(COLOR_RED, parsed.strokes[0]);
}

}  // namespace
}  // namespace canvas::phaser_ros
