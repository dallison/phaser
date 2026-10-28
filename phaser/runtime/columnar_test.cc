// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/arrow/arrow.h"
#include "phaser/plugins/avro/avro.h"
#include "phaser/plugins/parquet/parquet.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

namespace {

struct ArrowRelease {
  ArrowSchema* schema = nullptr;
  ArrowArray* array = nullptr;
  ~ArrowRelease() {
    if (schema != nullptr && schema->release != nullptr) schema->release(schema);
    if (array != nullptr && array->release != nullptr) array->release(array);
  }
};

class Cursor {
 public:
  explicit Cursor(std::string_view in) : in_(in) {}

  bool ok() const { return ok_; }
  size_t remaining() const { return in_.size() - at_; }

  int64_t Long() {
    uint64_t raw = 0;
    int shift = 0;
    while (at_ < in_.size() && shift <= 63) {
      const auto byte = static_cast<unsigned char>(in_[at_++]);
      raw |= static_cast<uint64_t>(byte & 0x7fU) << shift;
      if ((byte & 0x80U) == 0) {
        return static_cast<int64_t>((raw >> 1) ^ (~(raw & 1) + 1));
      }
      shift += 7;
    }
    ok_ = false;
    return 0;
  }

  std::string_view Bytes() {
    const int64_t size = Long();
    if (!ok_ || size < 0 || static_cast<size_t>(size) > remaining()) {
      ok_ = false;
      return {};
    }
    const std::string_view out = in_.substr(at_, static_cast<size_t>(size));
    at_ += static_cast<size_t>(size);
    return out;
  }

  void Skip(size_t n) {
    if (n > remaining()) {
      ok_ = false;
      return;
    }
    at_ += n;
  }

  std::string_view rest() const { return in_.substr(at_); }

 private:
  std::string_view in_;
  size_t at_ = 0;
  bool ok_ = true;
};

struct Row {
  bool has_x = false;
  int32_t x = 0;
  bool has_s = false;
  std::string s;
  std::vector<int32_t> vi32;
  bool has_m = false;
  bool has_f = false;
  int32_t f = 0;
  std::vector<std::pair<std::string, int32_t>> values;
  int choice = 0;
  int32_t choice_int = 0;
  std::string choice_string;
};

Row ReadRow(Cursor* in) {
  Row row;
  if (in->Long() == 1) {
    row.has_x = true;
    row.x = static_cast<int32_t>(in->Long());
  }
  if (in->Long() == 1) {
    row.has_s = true;
    row.s = std::string(in->Bytes());
  }
  const int64_t list = in->Long();
  for (int64_t i = 0; i < list; ++i) row.vi32.push_back(static_cast<int32_t>(in->Long()));
  if (list > 0) EXPECT_EQ(0, in->Long());
  if (in->Long() == 1) {
    row.has_m = true;
    if (in->Long() == 1) {
      row.has_f = true;
      row.f = static_cast<int32_t>(in->Long());
    }
  }
  const int64_t map = in->Long();
  for (int64_t i = 0; i < map; ++i) {
    const std::string key(in->Bytes());
    const bool present = in->Long() == 1;
    row.values.emplace_back(key, present ? static_cast<int32_t>(in->Long()) : 0);
  }
  if (map > 0) EXPECT_EQ(0, in->Long());
  row.choice = static_cast<int>(in->Long());
  if (row.choice == 1) row.choice_int = static_cast<int32_t>(in->Long());
  if (row.choice == 2) row.choice_string = std::string(in->Bytes());
  return row;
}

absl::StatusOr<std::vector<Row>> ReadAvro(std::string_view file) {
  if (file.size() < 4 || file.substr(0, 4) != "Obj\x01") {
    return absl::InvalidArgumentError("missing Avro magic");
  }
  Cursor in(file.substr(4));
  EXPECT_EQ(2, in.Long());
  EXPECT_EQ("avro.schema", in.Bytes());
  const std::string schema(in.Bytes());
  EXPECT_EQ("avro.codec", in.Bytes());
  EXPECT_EQ("null", in.Bytes());
  EXPECT_EQ(0, in.Long());
  EXPECT_NE(schema.find("\"vi32\""), std::string::npos) << schema;
  in.Skip(16);
  if (!in.ok()) return absl::InvalidArgumentError("truncated Avro header");
  if (in.remaining() == 0) return std::vector<Row>{};
  const int64_t count = in.Long();
  const std::string_view body = in.Bytes();
  in.Skip(16);
  Cursor rows(body);
  std::vector<Row> out;
  for (int64_t i = 0; i < count; ++i) out.push_back(ReadRow(&rows));
  if (!rows.ok() || rows.remaining() != 0 || !in.ok()) {
    return absl::InvalidArgumentError("Avro body did not decode");
  }
  return out;
}

}  // namespace

TEST(ColumnarTest, AvroAndParquetFollowTheArrowBatch) {
  phaser::arrow::SchemaBuilder builder;
  builder.BeginStruct("Row", false);
  builder.AddInt32("x", true);
  builder.AddString("s", true);
  builder.BeginList("vi32", false);
  builder.AddInt32("item", false);
  builder.EndList();
  builder.BeginStruct("m", true);
  builder.AddInt32("f", true);
  builder.EndStruct();
  builder.BeginMap("values", false);
  builder.BeginStruct("entries", false);
  builder.AddString("key", false);
  builder.AddInt32("value", true);
  builder.EndStruct();
  builder.EndMap();
  builder.BeginUnion("choice", false);
  builder.AddInt32("n", false);
  builder.AddString("text", false);
  builder.EndUnion();
  builder.EndStruct();

  ArrowSchema schema{};
  ArrowArray array{};
  ArrowRelease release{&schema, &array};
  ASSERT_TRUE(builder.Finish(&schema).ok());
  absl::StatusOr<phaser::arrow::BatchWriter> writer =
      phaser::arrow::BatchWriter::Open(&schema);
  ASSERT_TRUE(writer.ok()) << writer.status();

  {
    phaser::arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).Null().ok());
    ASSERT_TRUE(row.Child(1).Null().ok());
    ASSERT_TRUE(row.Child(2).FinishList(0).ok());
    ASSERT_TRUE(row.Child(3).NullStruct().ok());
    ASSERT_TRUE(row.Child(4).FinishList(0).ok());
    ASSERT_TRUE(row.Child(5).NullUnion().ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  {
    phaser::arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).Int32(7).ok());
    ASSERT_TRUE(row.Child(1).String("hello").ok());
    phaser::arrow::FieldWriter items = row.Child(2).Values();
    ASSERT_TRUE(items.Int32(1).ok());
    ASSERT_TRUE(items.Int32(2).ok());
    ASSERT_TRUE(row.Child(2).FinishList(2).ok());
    phaser::arrow::FieldWriter nested = row.Child(3);
    ASSERT_TRUE(nested.Child(0).Int32(3).ok());
    ASSERT_TRUE(nested.FinishStruct().ok());
    phaser::arrow::FieldWriter entry = row.Child(4).Values();
    ASSERT_TRUE(entry.Child(0).String("k").ok());
    ASSERT_TRUE(entry.Child(1).Int32(9).ok());
    ASSERT_TRUE(entry.FinishStruct().ok());
    ASSERT_TRUE(row.Child(4).FinishList(1).ok());
    ASSERT_TRUE(row.Child(5).UnionChild(1).Int32(4).ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  {
    phaser::arrow::FieldWriter row = writer->Root();
    ASSERT_TRUE(row.Child(0).Int32(0).ok());
    ASSERT_TRUE(row.Child(1).Null().ok());
    ASSERT_TRUE(row.Child(2).FinishList(0).ok());
    ASSERT_TRUE(row.Child(3).NullStruct().ok());
    phaser::arrow::FieldWriter entry = row.Child(4).Values();
    ASSERT_TRUE(entry.Child(0).String("z").ok());
    ASSERT_TRUE(entry.Child(1).Null().ok());
    ASSERT_TRUE(entry.FinishStruct().ok());
    ASSERT_TRUE(row.Child(4).FinishList(1).ok());
    ASSERT_TRUE(row.Child(5).UnionChild(2).String("arm").ok());
    ASSERT_TRUE(row.FinishStruct().ok());
  }
  ASSERT_TRUE(writer->Finish(&array).ok());

  std::string avro;
  ASSERT_TRUE(phaser::avro::WriteFile(&schema, &array, &avro).ok());
  absl::StatusOr<std::vector<Row>> rows = ReadAvro(avro);
  ASSERT_TRUE(rows.ok()) << rows.status();
  ASSERT_EQ(3u, rows->size());
  EXPECT_FALSE(rows->at(0).has_x);
  EXPECT_FALSE(rows->at(0).has_s);
  EXPECT_TRUE(rows->at(0).vi32.empty());
  EXPECT_FALSE(rows->at(0).has_m);
  EXPECT_TRUE(rows->at(0).values.empty());
  EXPECT_EQ(0, rows->at(0).choice);

  EXPECT_TRUE(rows->at(1).has_x);
  EXPECT_EQ(7, rows->at(1).x);
  EXPECT_EQ("hello", rows->at(1).s);
  ASSERT_EQ(2u, rows->at(1).vi32.size());
  EXPECT_EQ(1, rows->at(1).vi32[0]);
  EXPECT_EQ(2, rows->at(1).vi32[1]);
  EXPECT_TRUE(rows->at(1).has_m);
  EXPECT_TRUE(rows->at(1).has_f);
  EXPECT_EQ(3, rows->at(1).f);
  ASSERT_EQ(1u, rows->at(1).values.size());
  EXPECT_EQ("k", rows->at(1).values[0].first);
  EXPECT_EQ(9, rows->at(1).values[0].second);
  EXPECT_EQ(1, rows->at(1).choice);
  EXPECT_EQ(4, rows->at(1).choice_int);

  EXPECT_TRUE(rows->at(2).has_x);
  EXPECT_EQ(0, rows->at(2).x);
  EXPECT_FALSE(rows->at(2).has_s);
  ASSERT_EQ(1u, rows->at(2).values.size());
  EXPECT_EQ("z", rows->at(2).values[0].first);
  EXPECT_EQ(0, rows->at(2).values[0].second);
  EXPECT_EQ(2, rows->at(2).choice);
  EXPECT_EQ("arm", rows->at(2).choice_string);

  std::string parquet;
  ASSERT_TRUE(phaser::parquet::WriteFile(&schema, &array, &parquet).ok());
  ASSERT_GE(parquet.size(), 8u);
  EXPECT_EQ("PAR1", parquet.substr(0, 4));
  EXPECT_EQ("PAR1", parquet.substr(parquet.size() - 4));
  const std::string hello("\x05\x00\x00\x00hello", 9);
  EXPECT_NE(parquet.find(hello), std::string::npos);
  const std::string seven("\x07\x00\x00\x00", 4);
  EXPECT_NE(parquet.find(seven), std::string::npos);
  EXPECT_NE(parquet.find("vi32"), std::string::npos);
  EXPECT_NE(parquet.find("key_value"), std::string::npos);
  EXPECT_NE(parquet.find("type_id"), std::string::npos);
}

TEST(ColumnarTest, EmptyBatchStillWritesASchema) {
  phaser::arrow::SchemaBuilder builder;
  builder.BeginStruct("Empty", false);
  builder.AddInt32("x", true);
  builder.EndStruct();
  ArrowSchema schema{};
  ArrowArray array{};
  ArrowRelease release{&schema, &array};
  ASSERT_TRUE(builder.Finish(&schema).ok());
  absl::StatusOr<phaser::arrow::BatchWriter> writer =
      phaser::arrow::BatchWriter::Open(&schema);
  ASSERT_TRUE(writer.ok());
  ASSERT_TRUE(writer->Finish(&array).ok());
  std::string avro;
  std::string parquet;
  ASSERT_TRUE(phaser::avro::WriteFile(&schema, &array, &avro).ok());
  ASSERT_TRUE(phaser::parquet::WriteFile(&schema, &array, &parquet).ok());
  EXPECT_EQ("Obj\x01", avro.substr(0, 4));
  EXPECT_EQ("PAR1", parquet.substr(0, 4));
  EXPECT_EQ("PAR1", parquet.substr(parquet.size() - 4));
}
