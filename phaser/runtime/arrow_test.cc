// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/arrow.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace phaser {
namespace {

struct ArrowRelease {
  ArrowSchema* schema = nullptr;
  ArrowArray* array = nullptr;
  ~ArrowRelease() {
    if (schema != nullptr && schema->release != nullptr) {
      schema->release(schema);
    }
    if (array != nullptr && array->release != nullptr) {
      array->release(array);
    }
  }
};

TEST(ArrowTest, StructListAndNullsRoundTrip) {
  arrow::SchemaBuilder builder;
  builder.BeginStruct("row", false);
  builder.AddInt32("i", true);
  builder.AddString("s", true);
  builder.BeginList("fs", false);
  builder.AddFloat("item", false);
  builder.EndList();
  builder.EndStruct();

  ArrowSchema schema{};
  ArrowArray array{};
  ArrowRelease release{&schema, &array};
  ASSERT_TRUE(builder.Finish(&schema).ok());

  absl::StatusOr<arrow::BatchWriter> writer = arrow::BatchWriter::Open(&schema);
  ASSERT_TRUE(writer.ok()) << writer.status();

  {
    arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).Int32(7).ok());
    ASSERT_TRUE(row.Child(1).String("ab").ok());
    arrow::FieldWriter values = row.Child(2).Values();
    ASSERT_TRUE(values.Float(1.5f).ok());
    ASSERT_TRUE(values.Float(-2.5f).ok());
    ASSERT_TRUE(row.Child(2).FinishList(2).ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  {
    arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).Null().ok());
    ASSERT_TRUE(row.Child(1).Null().ok());
    ASSERT_TRUE(row.Child(2).FinishList(0).ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  ASSERT_TRUE(writer->Finish(&array).ok());
  EXPECT_EQ(2, array.length);

  absl::StatusOr<arrow::BatchReader> reader =
      arrow::BatchReader::Open(&schema, &array);
  ASSERT_TRUE(reader.ok()) << reader.status();
  EXPECT_EQ(2, reader->length());

  const arrow::FieldReader first = reader->Row(0);
  absl::StatusOr<int32_t> i = first.Child(0).Int32();
  ASSERT_TRUE(i.ok()) << i.status();
  EXPECT_EQ(7, *i);
  absl::StatusOr<std::string_view> text = first.Child(1).String();
  ASSERT_TRUE(text.ok()) << text.status();
  EXPECT_EQ("ab", *text);
  EXPECT_EQ(2, first.Child(2).length());
  absl::StatusOr<float> f0 = first.Child(2).Element(0).Float();
  absl::StatusOr<float> f1 = first.Child(2).Element(1).Float();
  ASSERT_TRUE(f0.ok()) << f0.status();
  ASSERT_TRUE(f1.ok()) << f1.status();
  EXPECT_EQ(1.5f, *f0);
  EXPECT_EQ(-2.5f, *f1);

  const arrow::FieldReader second = reader->Row(1);
  EXPECT_TRUE(second.Child(0).IsNull());
  EXPECT_TRUE(second.Child(1).IsNull());
  EXPECT_EQ(0, second.Child(2).length());
}

TEST(ArrowTest, DenseUnionNoneArm) {
  arrow::SchemaBuilder builder;
  builder.BeginStruct("row", false);
  builder.BeginUnion("value", false);
  builder.AddInt32("demo.Int", false);
  builder.EndUnion();
  builder.EndStruct();

  ArrowSchema schema{};
  ArrowArray array{};
  ArrowRelease release{&schema, &array};
  ASSERT_TRUE(builder.Finish(&schema).ok());
  EXPECT_EQ(std::string("+ud:0,1"), schema.children[0]->format);
  EXPECT_STREQ("none", schema.children[0]->children[0]->name);

  absl::StatusOr<arrow::BatchWriter> writer = arrow::BatchWriter::Open(&schema);
  ASSERT_TRUE(writer.ok()) << writer.status();
  {
    arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).NullUnion().ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  {
    arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).UnionChild(1).Int32(11).ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  ASSERT_TRUE(writer->Finish(&array).ok());

  absl::StatusOr<arrow::BatchReader> reader =
      arrow::BatchReader::Open(&schema, &array);
  ASSERT_TRUE(reader.ok()) << reader.status();
  EXPECT_EQ(0, reader->Row(0).Child(0).UnionType());
  EXPECT_EQ(1, reader->Row(1).Child(0).UnionType());
  absl::StatusOr<int32_t> value = reader->Row(1).Child(0).UnionValue().Int32();
  ASSERT_TRUE(value.ok()) << value.status();
  EXPECT_EQ(11, *value);
  absl::StatusOr<std::string_view> name =
      reader->Row(1).Child(0).UnionChildName();
  ASSERT_TRUE(name.ok()) << name.status();
  EXPECT_EQ("demo.Int", *name);
}

}  // namespace
}  // namespace phaser
