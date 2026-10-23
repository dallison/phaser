// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "phaser/runtime/arrow_abi.h"
#include "phaser/testdata/TestMessage.phaser.h"

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

TEST(PhaserArrowTest, RoundTrip) {
  using Message = foo::bar::phaser::TestMessage;
  Message unset;
  Message set;
  set.set_x(0);
  set.set_y(7);
  set.set_s("hello");
  set.set_buffer(std::string("a\0b", 3));
  set.set_e(foo::bar::phaser::FOO);
  set.set_fl(1.5f);
  set.set_db(2.5);
  set.mutable_m()->set_str("inner");
  set.add_vi32(1);
  set.add_vi32(2);
  set.add_vstr("one");
  set.set_u1a(9);
  set.set_u2b("arm");
  set.mutable_u3b()->set_str("u3");
  auto entry = set.add_values();
  entry->set_key("k");
  entry->set_value(3);

  foo::bar::phaser::InnerMessage packed;
  packed.set_str("any-inner");
  packed.set_f(42);
  ASSERT_TRUE(set.mutable_any()->PackFrom(packed));

  Message other;
  other.set_x(1);
  foo::bar::phaser::HybridLookupMessage hybrid;
  hybrid.set_dense_10(10);
  hybrid.set_sparse_1000(1000);
  ASSERT_TRUE(other.mutable_any()->PackFrom(hybrid));

  const std::vector<Message> batch = {unset, set, other};
  ArrowSchema schema{};
  ArrowArray array{};
  ArrowRelease release{&schema, &array};
  const absl::Status written = Message::ToArrow(batch, &schema, &array);
  ASSERT_TRUE(written.ok()) << written;

  std::vector<Message> out;
  const absl::Status read = Message::FromArrow(&schema, &array, &out);
  ASSERT_TRUE(read.ok()) << read;
  ASSERT_EQ(3u, out.size());

  EXPECT_FALSE(out[0].has_x());
  EXPECT_FALSE(out[0].has_y());
  EXPECT_FALSE(out[0].has_s());
  EXPECT_FALSE(out[0].has_m());
  EXPECT_FALSE(out[0].has_any());
  EXPECT_FALSE(out[0].has_buffer());
  EXPECT_FALSE(out[0].has_e());
  EXPECT_EQ(0u, out[0].vi32_size());
  EXPECT_FALSE(out[0].has_u1a());
  EXPECT_FALSE(out[0].has_u2b());
  EXPECT_EQ(0u, out[0].values_size());

  EXPECT_TRUE(out[1].has_x());
  EXPECT_EQ(0, out[1].x());
  EXPECT_EQ(7, out[1].y());
  EXPECT_EQ("hello", out[1].s());
  EXPECT_EQ(std::string("a\0b", 3), std::string(out[1].buffer()));
  EXPECT_EQ(foo::bar::phaser::FOO, out[1].e());
  EXPECT_FLOAT_EQ(1.5f, out[1].fl());
  EXPECT_DOUBLE_EQ(2.5, out[1].db());
  EXPECT_EQ("inner", out[1].m().str());
  ASSERT_EQ(2u, out[1].vi32_size());
  EXPECT_EQ(1, out[1].vi32(0));
  EXPECT_EQ(2, out[1].vi32(1));
  ASSERT_EQ(1u, out[1].vstr_size());
  EXPECT_EQ("one", out[1].vstr(0));
  EXPECT_EQ(9u, out[1].u1a());
  EXPECT_EQ("arm", out[1].u2b());
  EXPECT_EQ("u3", out[1].u3b().str());
  ASSERT_EQ(1u, out[1].values_size());
  EXPECT_EQ("k", out[1].values(0).key());
  EXPECT_EQ(3, out[1].values(0).value());
  foo::bar::phaser::InnerMessage unpacked;
  ASSERT_TRUE(out[1].any().UnpackTo(&unpacked));
  EXPECT_EQ("any-inner", unpacked.str());
  EXPECT_EQ(42, unpacked.f());

  EXPECT_EQ(1, out[2].x());
  EXPECT_FALSE(out[2].has_s());
  foo::bar::phaser::HybridLookupMessage unpacked_hybrid;
  ASSERT_TRUE(out[2].any().UnpackTo(&unpacked_hybrid));
  EXPECT_EQ(10, unpacked_hybrid.dense_10());
  EXPECT_EQ(1000, unpacked_hybrid.sparse_1000());
}

}  // namespace
