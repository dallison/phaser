#include <string>

#include "gtest/gtest.h"
#include "phaser/testdata/CrossPackageEnumUser.phaser.h"

namespace canvas::phaser {
namespace {

using ::palette::phaser::Color;
using ::palette::phaser::COLOR_BLUE;
using ::palette::phaser::COLOR_GREEN;
using ::palette::phaser::COLOR_RED;
using ::palette::phaser::Shade_Depth;
using ::palette::phaser::Shade_Depth_DEPTH_DARK;

TEST(CrossPackageEnumTest, SingularFieldRoundTrips) {
  Drawing drawing;
  drawing.set_background(COLOR_BLUE);
  EXPECT_EQ(COLOR_BLUE, drawing.background());
}

TEST(CrossPackageEnumTest, NestedEnumFieldRoundTrips) {
  Drawing drawing;
  drawing.set_depth(Shade_Depth_DEPTH_DARK);
  EXPECT_EQ(Shade_Depth_DEPTH_DARK, drawing.depth());
}

TEST(CrossPackageEnumTest, RepeatedFieldRoundTrips) {
  Drawing drawing;
  drawing.add_strokes(COLOR_RED);
  drawing.add_strokes(COLOR_GREEN);
  ASSERT_EQ(2u, drawing.strokes_size());
  EXPECT_EQ(COLOR_RED, drawing.strokes(0));
  EXPECT_EQ(COLOR_GREEN, drawing.strokes(1));
}

// The protobuf frontend ignores (phaser.array_size) and keeps the field a
// vector, so size it before indexing into it.
TEST(CrossPackageEnumTest, SizedArrayFieldRoundTrips) {
  Drawing drawing;
  drawing.resize_corners(4);
  drawing.set_corners(0, COLOR_GREEN);
  drawing.set_corners(3, COLOR_BLUE);
  EXPECT_EQ(COLOR_GREEN, drawing.corners(0));
  EXPECT_EQ(COLOR_BLUE, drawing.corners(3));
}

TEST(CrossPackageEnumTest, UnionFieldRoundTrips) {
  Drawing drawing;
  drawing.set_accent_color(COLOR_BLUE);
  EXPECT_EQ(COLOR_BLUE, drawing.accent_color());
  EXPECT_TRUE(drawing.has_accent_color());
}

TEST(CrossPackageEnumTest, StringizerAndParserResolveAcrossPackages) {
  EXPECT_EQ("COLOR_GREEN", ::palette::phaser::Color_Name(COLOR_GREEN));

  Color parsed;
  ::palette::phaser::Color_Parse("COLOR_BLUE", &parsed);
  EXPECT_EQ(COLOR_BLUE, parsed);
}

TEST(CrossPackageEnumTest, DebugStringNamesTheEnumValue) {
  Drawing drawing;
  drawing.set_background(COLOR_GREEN);
  EXPECT_NE(std::string::npos, drawing.DebugString().find("COLOR_GREEN"));
}

TEST(CrossPackageEnumTest, SerializesAndParses) {
  Drawing drawing;
  drawing.set_background(COLOR_BLUE);
  drawing.add_strokes(COLOR_RED);
  drawing.set_depth(Shade_Depth_DEPTH_DARK);

  const std::string wire = drawing.SerializeAsString();

  Drawing parsed;
  ASSERT_TRUE(parsed.ParseFromString(wire));
  EXPECT_EQ(COLOR_BLUE, parsed.background());
  ASSERT_EQ(1u, parsed.strokes_size());
  EXPECT_EQ(COLOR_RED, parsed.strokes(0));
  EXPECT_EQ(Shade_Depth_DEPTH_DARK, parsed.depth());
}

}  // namespace
}  // namespace canvas::phaser
