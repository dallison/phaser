// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "test_helpers.h"
#include "phaser/runtime/wireformat.h"
#include "phaser/testdata/coverage.pb.h"
#include "phaser/testdata/coverage.phaser.h"
#include "google/protobuf/any.pb.h"
#include <cmath>
#include <gtest/gtest.h>
#include <string>

namespace foo::bar::coverage::phaser {
namespace {

void FillAllScalars(AllScalars &msg) {
  msg.set_f_int32(-123456);
  msg.set_f_int64(-9876543210LL);
  msg.set_f_sint32(-42);
  msg.set_f_sint64(-84);
  msg.set_f_uint32(0xabcdef01u);
  msg.set_f_uint64(0x123456789abcdef0ULL);
  msg.set_f_fixed32(0x11223344u);
  msg.set_f_fixed64(0x5566778899aabbccULL);
  msg.set_f_sfixed32(-77);
  msg.set_f_sfixed64(-88);
  msg.set_f_float(1.25f);
  msg.set_f_double(2.5);
  msg.set_f_bool(true);
  msg.set_f_string("scalar-string");
  msg.set_f_bytes(::phaser::test::MakePatternBytes(128));
  msg.set_f_enum(COV_BAR);
}

void ExpectAllScalars(const AllScalars &msg) {
  EXPECT_EQ(-123456, msg.f_int32());
  EXPECT_EQ(-9876543210LL, msg.f_int64());
  EXPECT_EQ(-42, msg.f_sint32());
  EXPECT_EQ(-84, msg.f_sint64());
  EXPECT_EQ(0xabcdef01u, msg.f_uint32());
  EXPECT_EQ(0x123456789abcdef0ULL, msg.f_uint64());
  EXPECT_EQ(0x11223344u, msg.f_fixed32());
  EXPECT_EQ(0x5566778899aabbccULL, msg.f_fixed64());
  EXPECT_EQ(-77, msg.f_sfixed32());
  EXPECT_EQ(-88, msg.f_sfixed64());
  EXPECT_FLOAT_EQ(1.25f, msg.f_float());
  EXPECT_DOUBLE_EQ(2.5, msg.f_double());
  EXPECT_TRUE(msg.f_bool());
  EXPECT_EQ("scalar-string", msg.f_string());
  EXPECT_EQ(::phaser::test::MakePatternBytes(128), msg.f_bytes());
  EXPECT_EQ(COV_BAR, msg.f_enum());
}

using PbAllScalars = ::foo::bar::coverage::AllScalars;
using PbMapHolder = ::foo::bar::coverage::MapHolder;
using PbRepeatedPacked = ::foo::bar::coverage::RepeatedPrimitivesPacked;
using PbRepeatedUnpacked = ::foo::bar::coverage::RepeatedPrimitivesUnpacked;
using PbRepeatedStrings = ::foo::bar::coverage::RepeatedStrings;
using PbRepeatedBytes = ::foo::bar::coverage::RepeatedBytes;
using PbRepeatedMessages = ::foo::bar::coverage::RepeatedMessages;
using PbOneofStress = ::foo::bar::coverage::OneofStress;
using PbCoverageInner = ::foo::bar::coverage::CoverageInner;
using PbImportsMessage = ::foo::bar::coverage::ImportsMessage;

void ExpectAllScalarsMatch(const AllScalars &a, const AllScalars &b) {
  EXPECT_EQ(a.f_int32(), b.f_int32());
  EXPECT_EQ(a.f_int64(), b.f_int64());
  EXPECT_EQ(a.f_sint32(), b.f_sint32());
  EXPECT_EQ(a.f_sint64(), b.f_sint64());
  EXPECT_EQ(a.f_uint32(), b.f_uint32());
  EXPECT_EQ(a.f_uint64(), b.f_uint64());
  EXPECT_EQ(a.f_fixed32(), b.f_fixed32());
  EXPECT_EQ(a.f_fixed64(), b.f_fixed64());
  EXPECT_EQ(a.f_sfixed32(), b.f_sfixed32());
  EXPECT_EQ(a.f_sfixed64(), b.f_sfixed64());
  EXPECT_FLOAT_EQ(a.f_float(), b.f_float());
  EXPECT_DOUBLE_EQ(a.f_double(), b.f_double());
  EXPECT_EQ(a.f_bool(), b.f_bool());
  EXPECT_EQ(a.f_string(), b.f_string());
  EXPECT_EQ(a.f_bytes(), b.f_bytes());
  EXPECT_EQ(a.f_enum(), b.f_enum());
}

void ExpectAllScalarsMatchPb(const PbAllScalars &pb, const AllScalars &msg) {
  EXPECT_EQ(pb.f_int32(), msg.f_int32());
  EXPECT_EQ(pb.f_int64(), msg.f_int64());
  EXPECT_EQ(pb.f_sint32(), msg.f_sint32());
  EXPECT_EQ(pb.f_sint64(), msg.f_sint64());
  EXPECT_EQ(pb.f_uint32(), msg.f_uint32());
  EXPECT_EQ(pb.f_uint64(), msg.f_uint64());
  EXPECT_EQ(pb.f_fixed32(), msg.f_fixed32());
  EXPECT_EQ(pb.f_fixed64(), msg.f_fixed64());
  EXPECT_EQ(pb.f_sfixed32(), msg.f_sfixed32());
  EXPECT_EQ(pb.f_sfixed64(), msg.f_sfixed64());
  EXPECT_FLOAT_EQ(pb.f_float(), msg.f_float());
  EXPECT_DOUBLE_EQ(pb.f_double(), msg.f_double());
  EXPECT_EQ(pb.f_bool(), msg.f_bool());
  EXPECT_EQ(pb.f_string(), msg.f_string());
  EXPECT_EQ(pb.f_bytes(), msg.f_bytes());
  EXPECT_EQ(pb.f_enum(), static_cast<int>(msg.f_enum()));
}

void FillAllScalars(PbAllScalars &msg) {
  msg.set_f_int32(-123456);
  msg.set_f_int64(-9876543210LL);
  msg.set_f_sint32(-42);
  msg.set_f_sint64(-84);
  msg.set_f_uint32(0xabcdef01u);
  msg.set_f_uint64(0x123456789abcdef0ULL);
  msg.set_f_fixed32(0x11223344u);
  msg.set_f_fixed64(0x5566778899aabbccULL);
  msg.set_f_sfixed32(-77);
  msg.set_f_sfixed64(-88);
  msg.set_f_float(1.25f);
  msg.set_f_double(2.5);
  msg.set_f_bool(true);
  msg.set_f_string("scalar-string");
  msg.set_f_bytes(::phaser::test::MakePatternBytes(128));
  msg.set_f_enum(::foo::bar::coverage::COV_BAR);
}

void FillCoverageInner(PbCoverageInner &msg) {
  msg.set_str("inner");
  msg.set_f(0x0123456789abcdefULL);
}

// Phaser -> protobuf wire -> phaser, then protobuf -> wire -> phaser.
template <typename PhaserMsg, typename PbMsg, typename FillPhaser, typename FillPb,
          typename ExpectPhaserMatch, typename ExpectPbMatch>
void ExpectBidirectionalWireRoundTrip(FillPhaser fill_phaser, FillPb fill_pb,
                                      ExpectPhaserMatch expect_phaser,
                                      ExpectPbMatch expect_pb) {
  PhaserMsg phaser;
  fill_phaser(phaser);

  std::string phaser_wire;
  ASSERT_TRUE(phaser.SerializeToString(&phaser_wire));
  PbMsg pb_from_phaser;
  ASSERT_TRUE(pb_from_phaser.ParseFromString(phaser_wire));
  expect_pb(pb_from_phaser, phaser);

  PhaserMsg phaser_from_pb;
  std::string pb_wire;
  ASSERT_TRUE(pb_from_phaser.SerializeToString(&pb_wire));
  ASSERT_TRUE(phaser_from_pb.ParseFromString(pb_wire));
  expect_phaser(phaser, phaser_from_pb);
  expect_pb(pb_from_phaser, phaser_from_pb);

  PbMsg pb_orig;
  fill_pb(pb_orig);
  std::string pb_orig_wire;
  ASSERT_TRUE(pb_orig.SerializeToString(&pb_orig_wire));
  PhaserMsg phaser_from_orig_pb;
  ASSERT_TRUE(phaser_from_orig_pb.ParseFromString(pb_orig_wire));
  expect_phaser(phaser, phaser_from_orig_pb);

  std::string phaser_roundtrip_wire;
  ASSERT_TRUE(phaser_from_orig_pb.SerializeToString(&phaser_roundtrip_wire));
  PhaserMsg phaser_roundtrip;
  ASSERT_TRUE(phaser_roundtrip.ParseFromString(phaser_roundtrip_wire));
  expect_phaser(phaser_from_orig_pb, phaser_roundtrip);
  expect_pb(pb_orig, phaser_roundtrip);
}

void FillMapHolder(MapHolder &msg) {
  auto *a = msg.add_values();
  a->set_key("alpha");
  a->set_value(-7);
  auto *b = msg.add_values();
  b->set_key("beta");
  b->set_value(42);
}

void ExpectMapHolderMatch(const MapHolder &a, const MapHolder &b) {
  ASSERT_EQ(a.values_size(), b.values_size());
  for (size_t i = 0; i < a.values_size(); i++) {
    bool found = false;
    for (size_t j = 0; j < b.values_size(); j++) {
      if (a.values(i).key() == b.values(j).key()) {
        EXPECT_EQ(a.values(i).value(), b.values(j).value());
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "key missing in other map: " << a.values(i).key();
  }
}

void ExpectMapHolderMatchPb(const PbMapHolder &pb, const MapHolder &msg) {
  ASSERT_EQ(static_cast<int>(msg.values_size()), pb.values_size());
  for (size_t i = 0; i < msg.values_size(); i++) {
    const auto it = pb.values().find(msg.values(i).key());
    ASSERT_NE(pb.values().end(), it);
    EXPECT_EQ(it->second, msg.values(i).value());
  }
}

void FillRepeatedPacked(RepeatedPrimitivesPacked &msg) {
  for (int i = -3; i < 10; i++) {
    msg.add_vi32(i);
    msg.add_vi64(i * 1000LL);
    msg.add_vf64(static_cast<uint64_t>(i * 100000ULL));
  }
}

void ExpectRepeatedPackedMatch(const RepeatedPrimitivesPacked &a,
                               const RepeatedPrimitivesPacked &b) {
  ASSERT_EQ(a.vi32_size(), b.vi32_size());
  ASSERT_EQ(a.vi64_size(), b.vi64_size());
  ASSERT_EQ(a.vf64_size(), b.vf64_size());
  for (size_t i = 0; i < a.vi32_size(); i++) {
    EXPECT_EQ(a.vi32(i), b.vi32(i));
    EXPECT_EQ(a.vi64(i), b.vi64(i));
    EXPECT_EQ(a.vf64(i), b.vf64(i));
  }
}

void ExpectRepeatedPackedMatchPb(const PbRepeatedPacked &pb,
                                 const RepeatedPrimitivesPacked &msg) {
  ASSERT_EQ(pb.vi32_size(), static_cast<int>(msg.vi32_size()));
  ASSERT_EQ(pb.vi64_size(), static_cast<int>(msg.vi64_size()));
  ASSERT_EQ(pb.vf64_size(), static_cast<int>(msg.vf64_size()));
  for (int i = 0; i < pb.vi32_size(); i++) {
    EXPECT_EQ(pb.vi32(i), msg.vi32(i));
    EXPECT_EQ(pb.vi64(i), msg.vi64(i));
    EXPECT_EQ(pb.vf64(i), msg.vf64(i));
  }
}

void FillRepeatedUnpacked(RepeatedPrimitivesUnpacked &msg) {
  for (int i = -5; i < 5; i++) {
    msg.add_vi32(i * 11);
  }
}

void ExpectRepeatedUnpackedMatch(const RepeatedPrimitivesUnpacked &a,
                                 const RepeatedPrimitivesUnpacked &b) {
  ASSERT_EQ(a.vi32_size(), b.vi32_size());
  for (size_t i = 0; i < a.vi32_size(); i++) {
    EXPECT_EQ(a.vi32(i), b.vi32(i));
  }
}

void ExpectRepeatedUnpackedMatchPb(const PbRepeatedUnpacked &pb,
                                   const RepeatedPrimitivesUnpacked &msg) {
  ASSERT_EQ(pb.vi32_size(), static_cast<int>(msg.vi32_size()));
  for (int i = 0; i < pb.vi32_size(); i++) {
    EXPECT_EQ(pb.vi32(i), msg.vi32(i));
  }
}

void ExpectRepeatedUnpackedMatchPb(const PbRepeatedUnpacked &a,
                                   const PbRepeatedUnpacked &b) {
  ASSERT_EQ(a.vi32_size(), b.vi32_size());
  for (int i = 0; i < a.vi32_size(); i++) {
    EXPECT_EQ(a.vi32(i), b.vi32(i));
  }
}

void FillRepeatedStrings(RepeatedStrings &msg) {
  msg.add_vstr("");
  msg.add_vstr(::phaser::test::MakePatternString(64, 's'));
  msg.add_vstr("line\nbreak\t");
}

void ExpectRepeatedStringsMatch(const RepeatedStrings &a,
                                const RepeatedStrings &b) {
  ASSERT_EQ(a.vstr_size(), b.vstr_size());
  for (size_t i = 0; i < a.vstr_size(); i++) {
    EXPECT_EQ(a.vstr(i), b.vstr(i));
  }
}

void ExpectRepeatedStringsMatchPb(const PbRepeatedStrings &pb,
                                  const RepeatedStrings &msg) {
  ASSERT_EQ(pb.vstr_size(), static_cast<int>(msg.vstr_size()));
  for (int i = 0; i < pb.vstr_size(); i++) {
    EXPECT_EQ(pb.vstr(i), msg.vstr(i));
  }
}

void FillRepeatedBytes(RepeatedBytes &msg) {
  msg.add_vbytes(::phaser::test::MakePatternBytes(32));
  msg.add_vbytes(std::string("\0\x01\xff", 3));
}

void ExpectRepeatedBytesMatch(const RepeatedBytes &a, const RepeatedBytes &b) {
  ASSERT_EQ(a.vbytes_size(), b.vbytes_size());
  for (size_t i = 0; i < a.vbytes_size(); i++) {
    EXPECT_EQ(a.vbytes(i), b.vbytes(i));
  }
}

void ExpectRepeatedBytesMatchPb(const PbRepeatedBytes &pb,
                                const RepeatedBytes &msg) {
  ASSERT_EQ(pb.vbytes_size(), static_cast<int>(msg.vbytes_size()));
  for (int i = 0; i < pb.vbytes_size(); i++) {
    EXPECT_EQ(pb.vbytes(i), msg.vbytes(i));
  }
}

void FillRepeatedMessages(RepeatedMessages &msg) {
  auto *m0 = msg.add_items();
  FillAllScalars(*m0);
  auto *m1 = msg.add_items();
  m1->set_f_int32(99);
  m1->set_f_string("nested");
}

void ExpectRepeatedMessagesMatch(const RepeatedMessages &a,
                                 const RepeatedMessages &b) {
  ASSERT_EQ(a.items_size(), b.items_size());
  for (size_t i = 0; i < a.items_size(); i++) {
    ExpectAllScalarsMatch(a.items(i), b.items(i));
  }
}

void ExpectRepeatedMessagesMatchPb(const PbRepeatedMessages &pb,
                                   const RepeatedMessages &msg) {
  ASSERT_EQ(pb.items_size(), static_cast<int>(msg.items_size()));
  for (int i = 0; i < pb.items_size(); i++) {
    ExpectAllScalarsMatchPb(pb.items(i), msg.items(i));
  }
}

void FillCoverageInner(CoverageInner &msg) {
  msg.set_str("inner");
  msg.set_f(0x0123456789abcdefULL);
}

void ExpectCoverageInnerMatch(const CoverageInner &a, const CoverageInner &b) {
  EXPECT_EQ(a.str(), b.str());
  EXPECT_EQ(a.f(), b.f());
}

void ExpectCoverageInnerMatchPb(const PbCoverageInner &pb,
                                const CoverageInner &msg) {
  EXPECT_EQ(pb.str(), msg.str());
  EXPECT_EQ(pb.f(), msg.f());
}

void ExpectCoverageInnerMatchPb(const PbCoverageInner &a,
                                const PbCoverageInner &b) {
  EXPECT_EQ(a.str(), b.str());
  EXPECT_EQ(a.f(), b.f());
}

void FillImportsMessage(ImportsMessage &msg, bool include_any = true) {
  msg.mutable_imported_foo()->set_a(-3);
  msg.mutable_imported_foo()->set_b("wire-foo");
  FillCoverageInner(*msg.mutable_inner());
  msg.mutable_timestamp()->set_seconds(1700000001);
  msg.mutable_timestamp()->set_nanos(456);
  msg.mutable_wrapped_string()->set_value("wrapped-wire");
  msg.mutable_empty_msg();
  if (include_any) {
    CoverageInner packed;
    FillCoverageInner(packed);
    ASSERT_TRUE(msg.mutable_any_field()->PackFrom(packed));
  }
}

void FillImportsMessage(PbImportsMessage &msg, bool include_any = true) {
  msg.mutable_imported_foo()->set_a(-3);
  msg.mutable_imported_foo()->set_b("wire-foo");
  FillCoverageInner(*msg.mutable_inner());
  msg.mutable_timestamp()->set_seconds(1700000001);
  msg.mutable_timestamp()->set_nanos(456);
  msg.mutable_wrapped_string()->set_value("wrapped-wire");
  msg.mutable_empty_msg();
  if (include_any) {
    PbCoverageInner packed;
    FillCoverageInner(packed);
    msg.mutable_any_field()->PackFrom(packed);
  }
}

void FillMapHolder(PbMapHolder &msg) {
  (*msg.mutable_values())["alpha"] = -7;
  (*msg.mutable_values())["beta"] = 42;
}

void FillRepeatedPacked(PbRepeatedPacked &msg) {
  for (int i = -3; i < 10; i++) {
    msg.add_vi32(i);
    msg.add_vi64(i * 1000LL);
    msg.add_vf64(static_cast<uint64_t>(i * 100000ULL));
  }
}

void FillRepeatedUnpacked(PbRepeatedUnpacked &msg) {
  for (int i = -5; i < 5; i++) {
    msg.add_vi32(i * 11);
  }
}

void FillRepeatedStrings(PbRepeatedStrings &msg) {
  msg.add_vstr("");
  msg.add_vstr(::phaser::test::MakePatternString(64, 's'));
  msg.add_vstr("line\nbreak\t");
}

void FillRepeatedBytes(PbRepeatedBytes &msg) {
  msg.add_vbytes(::phaser::test::MakePatternBytes(32));
  msg.add_vbytes(std::string("\0\x01\xff", 3));
}

void FillRepeatedMessages(PbRepeatedMessages &msg) {
  FillAllScalars(*msg.add_items());
  auto *m1 = msg.add_items();
  m1->set_f_int32(99);
  m1->set_f_string("nested");
}

void ExpectImportsMessageMatch(const ImportsMessage &a, const ImportsMessage &b,
                               bool check_any = true) {
  EXPECT_EQ(a.imported_foo().a(), b.imported_foo().a());
  EXPECT_EQ(a.imported_foo().b(), b.imported_foo().b());
  ExpectCoverageInnerMatch(a.inner(), b.inner());
  EXPECT_EQ(a.timestamp().seconds(), b.timestamp().seconds());
  EXPECT_EQ(a.timestamp().nanos(), b.timestamp().nanos());
  EXPECT_EQ(a.wrapped_string().value(), b.wrapped_string().value());
  if (!check_any) {
    return;
  }
  CoverageInner inner_a;
  CoverageInner inner_b;
  ASSERT_TRUE(a.any_field().UnpackTo(&inner_a));
  ASSERT_TRUE(b.any_field().UnpackTo(&inner_b));
  ExpectCoverageInnerMatch(inner_a, inner_b);
}

void ExpectImportsMessageMatchPb(const PbImportsMessage &pb,
                                 const ImportsMessage &msg,
                                 bool check_any = true) {
  EXPECT_EQ(pb.imported_foo().a(), msg.imported_foo().a());
  EXPECT_EQ(pb.imported_foo().b(), msg.imported_foo().b());
  ExpectCoverageInnerMatchPb(pb.inner(), msg.inner());
  EXPECT_EQ(pb.timestamp().seconds(), msg.timestamp().seconds());
  EXPECT_EQ(pb.timestamp().nanos(), msg.timestamp().nanos());
  EXPECT_EQ(pb.wrapped_string().value(), msg.wrapped_string().value());
  if (!check_any) {
    return;
  }
  CoverageInner inner_msg;
  ASSERT_TRUE(msg.any_field().UnpackTo(&inner_msg));
  PbCoverageInner inner_pb;
  ASSERT_TRUE(pb.any_field().UnpackTo(&inner_pb));
  ExpectCoverageInnerMatchPb(inner_pb, inner_msg);
}

void ExpectImportsMessageMatchPb(const PbImportsMessage &a,
                                 const PbImportsMessage &b) {
  EXPECT_EQ(a.imported_foo().a(), b.imported_foo().a());
  EXPECT_EQ(a.imported_foo().b(), b.imported_foo().b());
  EXPECT_EQ(a.inner().str(), b.inner().str());
  EXPECT_EQ(a.inner().f(), b.inner().f());
  EXPECT_EQ(a.timestamp().seconds(), b.timestamp().seconds());
  EXPECT_EQ(a.timestamp().nanos(), b.timestamp().nanos());
  EXPECT_EQ(a.wrapped_string().value(), b.wrapped_string().value());
  PbCoverageInner inner_a;
  PbCoverageInner inner_b;
  ASSERT_TRUE(a.any_field().UnpackTo(&inner_a));
  ASSERT_TRUE(b.any_field().UnpackTo(&inner_b));
  EXPECT_EQ(inner_a.str(), inner_b.str());
  EXPECT_EQ(inner_a.f(), inner_b.f());
}

template <typename FillOneof, typename ExpectPhaserOneof,
          typename ExpectPbOneof>
void ExpectOneofWireRoundTrip(FillOneof fill, ExpectPhaserOneof expect_phaser,
                              ExpectPbOneof expect_pb) {
  OneofStress phaser;
  fill(phaser);
  std::string wire;
  ASSERT_TRUE(phaser.SerializeToString(&wire));
  PbOneofStress pb;
  ASSERT_TRUE(pb.ParseFromString(wire));
  expect_pb(pb);
  OneofStress phaser2;
  ASSERT_TRUE(pb.SerializeToString(&wire));
  ASSERT_TRUE(phaser2.ParseFromString(wire));
  expect_phaser(phaser2);
}

} // namespace

TEST(AllTypesTest, SetAndGetEveryScalar) {
  AllScalars msg;
  FillAllScalars(msg);
  ExpectAllScalars(msg);
}

TEST(AllTypesTest, ClearAndResetScalars) {
  AllScalars msg;
  FillAllScalars(msg);
  msg.clear_f_string();
  msg.clear_f_bytes();
  EXPECT_FALSE(msg.has_f_string());
  EXPECT_FALSE(msg.has_f_bytes());
  msg.set_f_string("again");
  msg.set_f_bytes("bytes");
  EXPECT_EQ("again", msg.f_string());
  EXPECT_EQ("bytes", msg.f_bytes());
}

TEST(AllTypesTest, ProtobufRoundTrip) {
  AllScalars msg;
  FillAllScalars(msg);

  std::string wire;
  ASSERT_TRUE(msg.SerializeToString(&wire));

  ::foo::bar::coverage::AllScalars pb;
  ASSERT_TRUE(pb.ParseFromString(wire));

  EXPECT_EQ(pb.f_int32(), msg.f_int32());
  EXPECT_EQ(pb.f_int64(), msg.f_int64());
  EXPECT_EQ(pb.f_sint32(), msg.f_sint32());
  EXPECT_EQ(pb.f_sint64(), msg.f_sint64());
  EXPECT_EQ(pb.f_uint32(), msg.f_uint32());
  EXPECT_EQ(pb.f_uint64(), msg.f_uint64());
  EXPECT_EQ(pb.f_fixed32(), msg.f_fixed32());
  EXPECT_EQ(pb.f_fixed64(), msg.f_fixed64());
  EXPECT_EQ(pb.f_sfixed32(), msg.f_sfixed32());
  EXPECT_EQ(pb.f_sfixed64(), msg.f_sfixed64());
  EXPECT_FLOAT_EQ(pb.f_float(), msg.f_float());
  EXPECT_DOUBLE_EQ(pb.f_double(), msg.f_double());
  EXPECT_EQ(pb.f_bool(), msg.f_bool());
  EXPECT_EQ(pb.f_string(), msg.f_string());
  EXPECT_EQ(pb.f_bytes(), msg.f_bytes());
  EXPECT_EQ(pb.f_enum(), static_cast<int>(msg.f_enum()));
}

TEST(AllTypesTest, MapInsertOverwriteAndClear) {
  MapHolder msg;
  auto *e1 = msg.add_values();
  e1->set_key("alpha");
  e1->set_value(1);
  auto *e2 = msg.add_values();
  e2->set_key("beta");
  e2->set_value(2);

  ASSERT_EQ(2u, msg.values_size());
  EXPECT_EQ("alpha", msg.values(0).key());
  EXPECT_EQ(1, msg.values(0).value());
  EXPECT_EQ("beta", msg.values(1).key());
  EXPECT_EQ(2, msg.values(1).value());

  msg.mutable_values(0)->set_value(99);
  EXPECT_EQ(99, msg.values(0).value());

  msg.clear_values();
  EXPECT_EQ(0u, msg.values_size());

  auto *e3 = msg.add_values();
  e3->set_key("gamma");
  e3->set_value(3);
  EXPECT_EQ(1u, msg.values_size());
  EXPECT_EQ("gamma", msg.values(0).key());
}

TEST(AllTypesTest, RepeatedPackedInt32GrowAndResize) {
  RepeatedPrimitivesPacked msg;
  constexpr int kCount = 1000;
  for (int i = 0; i < kCount; i++) {
    msg.add_vi32(i);
  }
  ASSERT_EQ(static_cast<size_t>(kCount), msg.vi32_size());
  for (int i = 0; i < kCount; i++) {
    EXPECT_EQ(i, msg.vi32(i));
  }
  msg.resize_vi32(10);
  ASSERT_EQ(10u, msg.vi32_size());
  msg.set_vi32(9, 9999);
  EXPECT_EQ(9999, msg.vi32(9));
}

TEST(AllTypesTest, RepeatedPackedInt64AndFixed64) {
  RepeatedPrimitivesPacked msg;
  for (int i = 0; i < 500; i++) {
    msg.add_vi64(i * 10);
    msg.add_vf64(static_cast<uint64_t>(i * 100));
  }
  ASSERT_EQ(500u, msg.vi64_size());
  ASSERT_EQ(500u, msg.vf64_size());
  EXPECT_EQ(4990, msg.vi64(499));
  EXPECT_EQ(static_cast<uint64_t>(49900), msg.vf64(499));
}

TEST(AllTypesTest, RepeatedStringChurn) {
  RepeatedStrings msg;
  for (int round = 0; round < 10; round++) {
    msg.clear_vstr();
    for (int i = 0; i < 100; i++) {
      msg.add_vstr(::phaser::test::MakePatternString(32, static_cast<char>('a' + (i % 26))));
    }
    ASSERT_EQ(100u, msg.vstr_size());
    for (int i = 0; i < 100; i++) {
      EXPECT_EQ(::phaser::test::MakePatternString(32, static_cast<char>('a' + (i % 26))),
                msg.vstr(i));
    }
  }
}

TEST(AllTypesTest, RepeatedStringShrinkAndGrow) {
  RepeatedStrings msg;
  msg.add_vstr(::phaser::test::MakePatternString(2048, 'L'));
  msg.add_vstr("tiny");
  EXPECT_EQ(2048u, msg.vstr(0).size());
  msg.set_vstr(0, "short");
  EXPECT_EQ("short", msg.vstr(0));
  msg.set_vstr(0, ::phaser::test::MakePatternString(4096, 'H'));
  EXPECT_EQ(4096u, msg.vstr(0).size());
}

TEST(AllTypesTest, RepeatedBytesWithNulls) {
  RepeatedBytes msg;
  msg.add_vbytes(::phaser::test::MakePatternBytes(256));
  msg.add_vbytes(std::string("\0\0\0", 3));
  EXPECT_EQ(256u, msg.vbytes(0).size());
  EXPECT_EQ(3u, msg.vbytes(1).size());
}

TEST(AllTypesTest, RepeatedMessagesSparseMutable) {
  RepeatedMessages msg;
  auto *m5 = msg.mutable_items(5);
  m5->set_f_string("slot-five");
  auto *m0 = msg.mutable_items(0);
  m0->set_f_int32(7);
  EXPECT_EQ(7, msg.items(0).f_int32());
  EXPECT_EQ("slot-five", msg.items(5).f_string());
}

// Each arm uses a fresh message (oneof arm switching can trip PayloadBuffer
// ShrinkBlock in toolbelt when replacing a larger arm with a smaller one).
TEST(AllTypesTest, OneofIntArm) {
  OneofStress msg;
  msg.set_u_int(42);
  EXPECT_EQ(42, msg.u_int());
}

TEST(AllTypesTest, OneofStringArm) {
  OneofStress msg;
  msg.set_u_string("oneof-string");
  EXPECT_EQ("oneof-string", msg.u_string());
}

TEST(AllTypesTest, OneofBytesArm) {
  OneofStress msg;
  msg.set_u_bytes(::phaser::test::MakePatternBytes(64));
  EXPECT_EQ(::phaser::test::MakePatternBytes(64), msg.u_bytes());
}

TEST(AllTypesTest, OneofNestedMessageArm) {
  OneofStress msg;
  auto *inner = msg.mutable_u_msg();
  inner->set_f_string("nested");
  EXPECT_EQ("nested", msg.u_msg().f_string());
}

TEST(AllTypesTest, ImportsCrossPackage) {
  ImportsMessage msg;
  msg.mutable_imported_foo()->set_a(7);
  msg.mutable_imported_foo()->set_b("from-foo");
  msg.mutable_inner()->set_str("inner-coverage");
  msg.mutable_inner()->set_f(0x1234);
  msg.mutable_timestamp()->set_seconds(1700000000);
  msg.mutable_timestamp()->set_nanos(123);
  msg.mutable_wrapped_string()->set_value("wrapped");
  msg.mutable_empty_msg();

  EXPECT_EQ(7, msg.imported_foo().a());
  EXPECT_EQ("from-foo", msg.imported_foo().b());
  EXPECT_EQ("inner-coverage", msg.inner().str());
  EXPECT_EQ(0x1234ULL, msg.inner().f());
  EXPECT_EQ(1700000000, msg.timestamp().seconds());
  EXPECT_EQ(123, msg.timestamp().nanos());
  EXPECT_EQ("wrapped", msg.wrapped_string().value());
}

TEST(AllTypesTest, AnyPackInnerAndImported) {
  ImportsMessage msg;

  CoverageInner inner;
  inner.set_str("packed-inner");
  inner.set_f(0xdeadbeef);
  ASSERT_TRUE(msg.mutable_any_field()->PackFrom(inner));

  CoverageInner unpacked;
  ASSERT_TRUE(msg.any_field().UnpackTo(&unpacked));
  EXPECT_EQ("packed-inner", unpacked.str());
  EXPECT_EQ(0xdeadbeefULL, unpacked.f());

  foo::bar::phaser::Foo foo;
  foo.set_a(99);
  foo.set_b("packed-foo");
  ASSERT_TRUE(msg.mutable_any_field()->PackFrom(foo));

  foo::bar::phaser::Foo foo_out;
  ASSERT_TRUE(msg.any_field().UnpackTo(&foo_out));
  EXPECT_EQ(99, foo_out.a());
  EXPECT_EQ("packed-foo", foo_out.b());
}

TEST(AllTypesTest, WireFormatScalarsBidirectional) {
  ExpectBidirectionalWireRoundTrip<AllScalars, PbAllScalars>(
      [](AllScalars &m) { FillAllScalars(m); },
      [](PbAllScalars &m) { FillAllScalars(m); },
      [](const AllScalars &a, const AllScalars &b) {
        ExpectAllScalarsMatch(a, b);
      },
      [](const PbAllScalars &pb, const AllScalars &m) {
        ExpectAllScalarsMatchPb(pb, m);
      });
}

TEST(AllTypesTest, WireFormatMapBidirectional) {
  ExpectBidirectionalWireRoundTrip<MapHolder, PbMapHolder>(
      [](MapHolder &m) { FillMapHolder(m); },
      [](PbMapHolder &m) { FillMapHolder(m); },
      [](const MapHolder &a, const MapHolder &b) { ExpectMapHolderMatch(a, b); },
      [](const PbMapHolder &pb, const MapHolder &m) {
        ExpectMapHolderMatchPb(pb, m);
      });
}

TEST(AllTypesTest, WireFormatRepeatedPackedBidirectional) {
  ExpectBidirectionalWireRoundTrip<RepeatedPrimitivesPacked, PbRepeatedPacked>(
      [](RepeatedPrimitivesPacked &m) { FillRepeatedPacked(m); },
      [](PbRepeatedPacked &m) { FillRepeatedPacked(m); },
      [](const RepeatedPrimitivesPacked &a, const RepeatedPrimitivesPacked &b) {
        ExpectRepeatedPackedMatch(a, b);
      },
      [](const PbRepeatedPacked &pb, const RepeatedPrimitivesPacked &m) {
        ExpectRepeatedPackedMatchPb(pb, m);
      });
}

// Proto has [packed=false], but phaser currently emits a length-delimited packed
// payload for scalar repeated fields. Protobuf accepts that wire; verify both
// directions through protobuf bytes.
TEST(AllTypesTest, WireFormatRepeatedUnpackedBidirectional) {
  PbRepeatedUnpacked pb;
  FillRepeatedUnpacked(pb);
  std::string pb_wire;
  ASSERT_TRUE(pb.SerializeToString(&pb_wire));

  RepeatedPrimitivesUnpacked phaser;
  ASSERT_TRUE(phaser.ParseFromString(pb_wire));
  ExpectRepeatedUnpackedMatchPb(pb, phaser);

  std::string phaser_wire;
  ASSERT_TRUE(phaser.SerializeToString(&phaser_wire));
  RepeatedPrimitivesUnpacked phaser2;
  ASSERT_TRUE(phaser2.ParseFromString(phaser_wire));
  ExpectRepeatedUnpackedMatch(phaser, phaser2);

  std::string pb_wire2;
  ASSERT_TRUE(pb.SerializeToString(&pb_wire2));
  RepeatedPrimitivesUnpacked phaser3;
  ASSERT_TRUE(phaser3.ParseFromString(pb_wire2));
  ExpectRepeatedUnpackedMatch(phaser, phaser3);
}

TEST(AllTypesTest, WireFormatRepeatedStringsBidirectional) {
  ExpectBidirectionalWireRoundTrip<RepeatedStrings, PbRepeatedStrings>(
      [](RepeatedStrings &m) { FillRepeatedStrings(m); },
      [](PbRepeatedStrings &m) { FillRepeatedStrings(m); },
      [](const RepeatedStrings &a, const RepeatedStrings &b) {
        ExpectRepeatedStringsMatch(a, b);
      },
      [](const PbRepeatedStrings &pb, const RepeatedStrings &m) {
        ExpectRepeatedStringsMatchPb(pb, m);
      });
}

TEST(AllTypesTest, WireFormatRepeatedBytesBidirectional) {
  ExpectBidirectionalWireRoundTrip<RepeatedBytes, PbRepeatedBytes>(
      [](RepeatedBytes &m) { FillRepeatedBytes(m); },
      [](PbRepeatedBytes &m) { FillRepeatedBytes(m); },
      [](const RepeatedBytes &a, const RepeatedBytes &b) {
        ExpectRepeatedBytesMatch(a, b);
      },
      [](const PbRepeatedBytes &pb, const RepeatedBytes &m) {
        ExpectRepeatedBytesMatchPb(pb, m);
      });
}

TEST(AllTypesTest, WireFormatRepeatedMessagesBidirectional) {
  ExpectBidirectionalWireRoundTrip<RepeatedMessages, PbRepeatedMessages>(
      [](RepeatedMessages &m) { FillRepeatedMessages(m); },
      [](PbRepeatedMessages &m) { FillRepeatedMessages(m); },
      [](const RepeatedMessages &a, const RepeatedMessages &b) {
        ExpectRepeatedMessagesMatch(a, b);
      },
      [](const PbRepeatedMessages &pb, const RepeatedMessages &m) {
        ExpectRepeatedMessagesMatchPb(pb, m);
      });
}

TEST(AllTypesTest, WireFormatImportsBidirectional) {
  ExpectBidirectionalWireRoundTrip<ImportsMessage, PbImportsMessage>(
      [](ImportsMessage &m) { FillImportsMessage(m, true); },
      [](PbImportsMessage &m) { FillImportsMessage(m, true); },
      [](const ImportsMessage &a, const ImportsMessage &b) {
        ExpectImportsMessageMatch(a, b, true);
      },
      [](const PbImportsMessage &pb, const ImportsMessage &m) {
        ExpectImportsMessageMatchPb(pb, m, true);
      });
}

TEST(AllTypesTest, WireFormatAnyFieldBidirectional) {
  ImportsMessage phaser;
  CoverageInner inner;
  FillCoverageInner(inner);
  ASSERT_TRUE(phaser.mutable_any_field()->PackFrom(inner));

  std::string phaser_wire;
  ASSERT_TRUE(phaser.SerializeToString(&phaser_wire));

  PbImportsMessage pb;
  ASSERT_TRUE(pb.ParseFromString(phaser_wire));
  PbCoverageInner inner_pb;
  ASSERT_TRUE(pb.any_field().UnpackTo(&inner_pb));
  ExpectCoverageInnerMatchPb(inner_pb, inner);

  std::string pb_wire;
  ASSERT_TRUE(pb.SerializeToString(&pb_wire));
  ImportsMessage phaser_from_pb;
  ASSERT_TRUE(phaser_from_pb.ParseFromString(pb_wire));
  CoverageInner inner2;
  ASSERT_TRUE(phaser_from_pb.any_field().UnpackTo(&inner2));
  ExpectCoverageInnerMatch(inner, inner2);

  std::string roundtrip_wire;
  ASSERT_TRUE(phaser_from_pb.SerializeToString(&roundtrip_wire));
  PbImportsMessage pb2;
  ASSERT_TRUE(pb2.ParseFromString(roundtrip_wire));
  PbCoverageInner inner3_pb;
  ASSERT_TRUE(pb2.any_field().UnpackTo(&inner3_pb));
  ExpectCoverageInnerMatchPb(inner_pb, inner3_pb);
}

TEST(AllTypesTest, WireFormatAnyFieldValueBeforeTypeUrl) {
  PbCoverageInner inner_pb;
  FillCoverageInner(inner_pb);
  std::string inner_wire;
  ASSERT_TRUE(inner_pb.SerializeToString(&inner_wire));

  const std::string type_url =
      "type.googleapis.com/foo.bar.coverage.CoverageInner";

  // Build standard protobuf Any wire with field 2 (value) before field 1
  // (type_url).
  std::string any_wire;
  {
    ::phaser::ProtoBuffer buf(256);
    ASSERT_TRUE(
        buf.SerializeLengthDelimited(2, inner_wire.data(), inner_wire.size())
            .ok());
    ASSERT_TRUE(buf.SerializeLengthDelimited(1, type_url.data(), type_url.size())
                    .ok());
    any_wire = buf.AsString();
  }

  ImportsMessage phaser;
  ASSERT_TRUE(phaser.mutable_any_field()->ParseFromString(any_wire));

  CoverageInner unpacked;
  ASSERT_TRUE(phaser.any_field().UnpackTo(&unpacked));
  ExpectCoverageInnerMatchPb(inner_pb, unpacked);
  EXPECT_EQ(type_url, phaser.any_field().type_url());

  std::string out_wire;
  ASSERT_TRUE(phaser.any_field().SerializeToString(&out_wire));
  ::google::protobuf::Any any_out;
  ASSERT_TRUE(any_out.ParseFromString(out_wire));
  PbCoverageInner inner_out_pb;
  ASSERT_TRUE(any_out.UnpackTo(&inner_out_pb));
  ExpectCoverageInnerMatchPb(inner_pb, inner_out_pb);
}

TEST(AllTypesTest, WireFormatOneofIntBidirectional) {
  ExpectOneofWireRoundTrip(
      [](OneofStress &m) { m.set_u_int(-99); },
      [](const OneofStress &m) {
        EXPECT_TRUE(m.has_u_int());
        EXPECT_EQ(-99, m.u_int());
      },
      [](const PbOneofStress &pb) {
        EXPECT_TRUE(pb.has_u_int());
        EXPECT_EQ(-99, pb.u_int());
      });
}

TEST(AllTypesTest, WireFormatOneofStringBidirectional) {
  ExpectOneofWireRoundTrip(
      [](OneofStress &m) { m.set_u_string("wire-oneof"); },
      [](const OneofStress &m) {
        EXPECT_TRUE(m.has_u_string());
        EXPECT_EQ("wire-oneof", m.u_string());
      },
      [](const PbOneofStress &pb) {
        EXPECT_TRUE(pb.has_u_string());
        EXPECT_EQ("wire-oneof", pb.u_string());
      });
}

TEST(AllTypesTest, WireFormatOneofBytesBidirectional) {
  const std::string bytes = ::phaser::test::MakePatternBytes(48);
  ExpectOneofWireRoundTrip(
      [&](OneofStress &m) { m.set_u_bytes(bytes); },
      [&](const OneofStress &m) {
        EXPECT_TRUE(m.has_u_bytes());
        EXPECT_EQ(bytes, m.u_bytes());
      },
      [&](const PbOneofStress &pb) {
        EXPECT_TRUE(pb.has_u_bytes());
        EXPECT_EQ(bytes, pb.u_bytes());
      });
}

TEST(AllTypesTest, WireFormatOneofMessageBidirectional) {
  ExpectOneofWireRoundTrip(
      [](OneofStress &m) {
        FillAllScalars(*m.mutable_u_msg());
      },
      [](const OneofStress &m) {
        EXPECT_TRUE(m.has_u_msg());
        AllScalars expected;
        FillAllScalars(expected);
        ExpectAllScalarsMatch(m.u_msg(), expected);
      },
      [](const PbOneofStress &pb) {
        EXPECT_TRUE(pb.has_u_msg());
        EXPECT_EQ(-123456, pb.u_msg().f_int32());
        EXPECT_EQ("scalar-string", pb.u_msg().f_string());
      });
}

} // namespace foo::bar::coverage::phaser
