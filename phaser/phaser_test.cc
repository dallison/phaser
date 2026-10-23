
// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <type_traits>
#include <vector>

#include "absl/strings/str_format.h"
#include "phaser/runtime/payload_buffer.h"
#include "phaser/runtime/runtime.h"
#include "phaser/testdata/TestMessage.pb.h"
#include "phaser/testdata/TestMessage.phaser.h"
#include "test_helpers.h"
#include "toolbelt/hexdump.h"

TEST(PhaserTest, ProtobufCompat) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto m = msg.mutable_m();
  m->set_str("Inner message");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  auto inner = msg.mutable_m();
  inner->set_str("Inner message");

  msg.set_u1b(4321);
  msg.set_u2b("u2b");

  auto u3 = msg.mutable_u3b();
  u3->set_str("u3b");

  std::string buffer_data;
  for (int i = 0; i < 100; i++) {
    buffer_data.push_back(static_cast<char>(rand() & 0xff));
  }
  buffer_data.push_back('\n');
  buffer_data.push_back('\r');

  buffer_data.push_back('\v');
  buffer_data.push_back('\t');

  msg.set_buffer(buffer_data);

  msg.set_e(foo::bar::phaser::FOO);

  msg.set_fl(1.3f);
  msg.set_db(2.4);

  std::cout << msg;

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());
  ASSERT_EQ("Inner message", msg.m().str());

  // Check enum printing and parsing functions.
  ASSERT_EQ(foo::bar::phaser::FOO, msg.e());
  ASSERT_EQ("FOO", foo::bar::phaser::EnumTest_Name(msg.e()));

  foo::bar::phaser::EnumTest ee;
  foo::bar::phaser::EnumTest_Parse("BAR", &ee);
  ASSERT_EQ(foo::bar::phaser::BAR, ee);

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Serialize to a string.
  std::string serialized;
  msg.SerializeToString(&serialized);

  // Deserialize from a string into a protobuf message.
  foo::bar::TestMessage msg2;
  ASSERT_TRUE(msg2.ParseFromString(serialized));
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());
  ASSERT_EQ(msg2.m().str(), msg.m().str());

  std::string pb_debug_string = msg2.DebugString();
  std::string phaser_debug_string = msg.DebugString();
  phaser::test::StripProtobufDebugRedaction(pb_debug_string);
  phaser::test::StripProtobufDebugRedaction(phaser_debug_string);
  ASSERT_EQ(pb_debug_string, phaser_debug_string);
}

TEST(PhaserTest, Expansion) {
  foo::bar::phaser::TestMessage msg;  // 1K buffer by default.
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto buffer = msg.allocate_buffer(16000);
  ASSERT_NE(nullptr, buffer.data());
  ASSERT_EQ(16000, buffer.size());

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());
}

TEST(PhaserTest, NewFieldsBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Create a TestMessageNewFields using the binary from TestMessage.
  auto msg2 = foo::bar::phaser::TestMessageNewFields::CreateReadonly(
      msg.Data(), msg.Size());
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());

  // Check that the new fields are not present.
  ASSERT_FALSE(msg2.has_new1());
  ASSERT_FALSE(msg2.has_new2());
  ASSERT_EQ(0, msg2.new3_size());
}

TEST(PhaserTest, NewFieldsRepeatedBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Create a TestMessageNewFields using the binary from TestMessage.
  auto msg2 = foo::bar::phaser::TestMessageNewFields::CreateReadonly(
      msg.Data(), msg.Size());
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());

  ASSERT_EQ(3, msg2.vi32_size());
  ASSERT_EQ(3, msg2.vstr_size());
  ASSERT_EQ(msg2.vi32(0), msg.vi32(0));
  ASSERT_EQ(msg2.vi32(1), msg.vi32(1));
  ASSERT_EQ(msg2.vi32(2), msg.vi32(2));
  ASSERT_EQ(msg2.vstr(0), msg.vstr(0));
  ASSERT_EQ(msg2.vstr(1), msg.vstr(1));
  ASSERT_EQ(msg2.vstr(2), msg.vstr(2));
}

namespace {
void SetInnerMessage(foo::bar::phaser::InnerMessage inner, int i) {
  inner.set_str(absl::StrFormat("element %d", i));
  inner.set_f(i);
  for (int j = 0; j < i % 4; j++) {
    inner.add_ev(foo::bar::phaser::BAR);
  }
}

template <typename Inner>
void ExpectInnerMessage(const Inner& inner, int i) {
  EXPECT_EQ(absl::StrFormat("element %d", i), inner.str());
  EXPECT_EQ(i, inner.f());
  EXPECT_EQ(i % 4, inner.ev_size());
}

template <typename Message>
void ExpectInnerMessages(const Message& msg, int count) {
  ASSERT_EQ(count, msg.vm_size());
  for (int i = 0; i < count; i++) {
    ExpectInnerMessage(msg.vm(i), i);
  }
}
}  // namespace

TEST(PhaserTest, RepeatedMessagesGrowAcrossChunks) {
  constexpr int kCount = 100;
  foo::bar::phaser::TestMessage msg;
  std::vector<foo::bar::phaser::InnerMessage> handles;
  for (int i = 0; i < kCount; i++) {
    handles.push_back(msg.add_vm());
  }
  // Every handle is still valid after the later elements were added.
  for (int i = 0; i < kCount; i++) {
    SetInnerMessage(handles[i], i);
  }
  ExpectInnerMessages(msg, kCount);
  EXPECT_GE(msg.vm().capacity(), size_t(kCount));

  const char* base = static_cast<const char*>(msg.Data());
  std::vector<char> recv(base, base + msg.Size());
  ExpectInnerMessages(
      foo::bar::phaser::TestMessage::CreateReadonly(recv.data(), recv.size()),
      kCount);
  ExpectInnerMessages(foo::bar::phaser::TestMessageNewFields::CreateReadonly(
                          recv.data(), recv.size()),
                      kCount);

  std::string serialized;
  ASSERT_TRUE(msg.SerializeToString(&serialized));
  foo::bar::TestMessage pb;
  ASSERT_TRUE(pb.ParseFromString(serialized));
  ASSERT_EQ(kCount, pb.vm_size());
  for (int i = 0; i < kCount; i++) {
    EXPECT_EQ(absl::StrFormat("element %d", i), pb.vm(i).str());
    EXPECT_EQ(i, pb.vm(i).f());
  }
  foo::bar::phaser::TestMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ExpectInnerMessages(parsed, kCount);

  foo::bar::phaser::TestMessage copy;
  ASSERT_TRUE(copy.CloneFrom(msg).ok());
  ExpectInnerMessages(copy, kCount);
}

// A buffer is sent finalized, without the index of its metadata, so a reader
// with a newer schema finds each field through the message's own metadata.
TEST(PhaserTest, NewFieldsInFinalizedBuffer) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");
  msg.mutable_m()->set_str("nested");
  SetInnerMessage(msg.add_vm(), 7);
  msg.Finalize();
  EXPECT_EQ(0u,
            static_cast<const ::phaser::PayloadBuffer*>(msg.Data())->metadata);

  const char* base = static_cast<const char*>(msg.Data());
  std::vector<char> recv(base, base + msg.Size());
  auto same =
      foo::bar::phaser::TestMessage::CreateReadonly(recv.data(), recv.size());
  EXPECT_TRUE(same.FieldLocationsKnown());
  EXPECT_TRUE(same.m().FieldLocationsKnown());

  auto evolved = foo::bar::phaser::TestMessageNewFields::CreateReadonly(
      recv.data(), recv.size());
  EXPECT_FALSE(evolved.FieldLocationsKnown());
  EXPECT_EQ(1234, evolved.x());
  EXPECT_EQ(5678, evolved.y());
  EXPECT_EQ("hello world", evolved.s());
  EXPECT_FALSE(evolved.has_new1());
  EXPECT_FALSE(evolved.has_new2());
  EXPECT_EQ(0, evolved.new3_size());
  EXPECT_EQ("nested", evolved.m().str());
  ASSERT_EQ(1, evolved.vm_size());
  ExpectInnerMessage(evolved.vm(0), 7);
}

TEST(PhaserTest, PartHandlesBorrowTheMessage) {
  foo::bar::phaser::TestMessage msg;
  EXPECT_TRUE(msg.FieldLocationsKnown());
  const auto& owners =
      static_cast<::phaser::DynamicMutableMessageRuntime*>(msg.runtime)
          ->owners_;
  EXPECT_TRUE(msg.OwnsRuntime());
  EXPECT_EQ(1u, owners.load());
  foo::bar::phaser::InnerMessage element = msg.add_vm();
  foo::bar::phaser::InnerMessage* nested = msg.mutable_m();
  foo::bar::phaser::InnerMessage* member = msg.mutable_u3b();
  foo::bar::phaser::InnerMessage copy = element;
  EXPECT_EQ(1u, owners.load());
  EXPECT_FALSE(element.OwnsRuntime());
  EXPECT_FALSE(copy.OwnsRuntime());

  // The handles see the payload buffer after it grows.
  ASSERT_NE(nullptr, msg.allocate_buffer(1 << 20).data());
  element.set_f(1);
  copy.set_str("copy");
  nested->set_str("nested");
  member->set_str("member");
  EXPECT_EQ(1, msg.vm(0).f());
  EXPECT_EQ("copy", msg.vm(0).str());
  EXPECT_EQ("nested", msg.m().str());
  EXPECT_EQ("member", msg.u3b().str());

  // A received message's nested handles find their fields where they were
  // compiled when the nested type's layout matches.
  const char* base = static_cast<const char*>(msg.Data());
  std::vector<char> recv(base, base + msg.Size());
  auto readonly =
      foo::bar::phaser::TestMessage::CreateReadonly(recv.data(), recv.size());
  EXPECT_TRUE(readonly.m().FieldLocationsKnown());
  EXPECT_EQ("nested", readonly.m().str());
  EXPECT_TRUE(readonly.u3b().FieldLocationsKnown());
  EXPECT_EQ("member", readonly.u3b().str());
  auto evolved = foo::bar::phaser::TestMessageNewFields::CreateReadonly(
      recv.data(), recv.size());
  EXPECT_FALSE(evolved.FieldLocationsKnown());
  EXPECT_TRUE(evolved.m().FieldLocationsKnown());
  EXPECT_EQ("nested", evolved.m().str());
}

TEST(PhaserTest, CopiesOfDynamicMessageShareIt) {
  auto msg = std::make_unique<foo::bar::phaser::TestMessage>();
  msg->set_x(1);
  const auto& owners =
      static_cast<::phaser::DynamicMutableMessageRuntime*>(msg->runtime)
          ->owners_;
  foo::bar::phaser::TestMessage copy = *msg;
  EXPECT_TRUE(copy.OwnsRuntime());
  EXPECT_EQ(msg->runtime, copy.runtime);
  EXPECT_EQ(2u, owners.load());

  foo::bar::phaser::TestMessage moved = std::move(copy);
  EXPECT_FALSE(copy.OwnsRuntime());
  EXPECT_EQ(nullptr, copy.runtime);
  EXPECT_EQ(2u, owners.load());

  msg.reset();
  EXPECT_EQ(1u, owners.load());
  EXPECT_EQ(1, moved.x());
  ASSERT_NE(nullptr, moved.allocate_buffer(1 << 20).data());
  moved.set_y(2);

  // Assigning releases the runtime the handle owned.
  foo::bar::phaser::TestMessage other;
  other = moved;
  EXPECT_EQ(moved.runtime, other.runtime);
  EXPECT_EQ(2u, owners.load());
  EXPECT_EQ(2, other.y());
  other = foo::bar::phaser::TestMessage();
  EXPECT_NE(moved.runtime, other.runtime);
  EXPECT_EQ(1u, owners.load());
}

// Assigning a protobuf-frontend message makes it refer to the other message,
// as copying one does.
TEST(PhaserTest, AssignedMessageRefersToTheOther) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1);
  msg.mutable_u3b()->set_str("member");
  msg.mutable_m()->set_str("nested");

  foo::bar::phaser::TestMessage other;
  other.set_x(2);
  other.mutable_u3b()->set_str("other");
  other = msg;
  EXPECT_EQ(msg.runtime, other.runtime);
  EXPECT_EQ(1, other.x());
  EXPECT_EQ("member", other.u3b().str());
  EXPECT_EQ("nested", other.m().str());
  other.mutable_u3b()->set_str("changed");
  EXPECT_EQ("changed", msg.u3b().str());

  foo::bar::phaser::TestMessage empty;
  other = empty;
  EXPECT_EQ(empty.runtime, other.runtime);
  EXPECT_FALSE(other.has_u3b());
  EXPECT_EQ("changed", msg.u3b().str());
}

TEST(PhaserTest, RepeatedMessagesResizeAndReserve) {
  foo::bar::phaser::TestMessage msg;
  for (int i = 0; i < 40; i++) {
    SetInnerMessage(msg.add_vm(), i);
  }
  msg.resize_vm(10);
  ExpectInnerMessages(msg, 10);

  // An element reused after shrinking starts out empty.
  auto reused = msg.add_vm();
  EXPECT_EQ("", reused.str());
  EXPECT_EQ(0, reused.f());
  EXPECT_EQ(0, reused.ev_size());
  SetInnerMessage(reused, 10);
  msg.resize_vm(30);
  ASSERT_EQ(30, msg.vm_size());
  for (int i = 0; i < 11; i++) {
    ExpectInnerMessage(msg.vm(i), i);
  }
  EXPECT_EQ("", msg.vm(29).str());
  for (int i = 11; i < 30; i++) {
    SetInnerMessage(msg.mutable_vm(i), i);
  }
  ExpectInnerMessages(msg, 30);

  msg.clear_vm();
  EXPECT_EQ(0, msg.vm_size());
  EXPECT_EQ(0, msg.vm().capacity());

  // Reserving empty storage sizes it exactly; growing past it doubles it.
  msg.reserve_vm(40);
  EXPECT_EQ(40, msg.vm().capacity());
  for (int i = 0; i < 41; i++) {
    SetInnerMessage(msg.add_vm(), i);
  }
  EXPECT_EQ(80, msg.vm().capacity());
  ExpectInnerMessages(msg, 41);
}

TEST(PhaserTest, ClearedRepeatedMessagesReuseTheBuffer) {
  foo::bar::phaser::TestMessage msg;
  for (int i = 0; i < 50; i++) {
    SetInnerMessage(msg.add_vm(), i);
  }
  msg.clear_vm();
  const size_t size = msg.Size();
  for (int round = 0; round < 20; round++) {
    for (int i = 0; i < 50; i++) {
      SetInnerMessage(msg.add_vm(), i);
    }
    ExpectInnerMessages(msg, 50);
    msg.clear_vm();
  }
  EXPECT_EQ(size, msg.Size());
}

// A hostile sender can corrupt the block that holds a vector of messages.
// Reading it must stay inside the received bytes; run under AddressSanitizer
// to check.
TEST(PhaserTest, HostileRepeatedMessageBlockIsBounded) {
  constexpr int kCount = 20;
  foo::bar::phaser::TestMessage src;
  for (int i = 0; i < kCount; i++) {
    SetInnerMessage(src.add_vm(), i);
  }
  const char* base = static_cast<const char*>(src.Data());
  const std::vector<char> original(base, base + src.Size());

  // Find the VectorHeader {kCount, block} whose block starts with the stride.
  const uint32_t stride =
      (foo::bar::phaser::InnerMessage::BinarySize() + 7) & ~size_t(7);
  size_t header = 0;
  uint32_t block = 0;
  for (size_t i = 0; i + 2 * sizeof(uint32_t) <= original.size(); i += 4) {
    uint32_t num, data, word;
    std::memcpy(&num, original.data() + i, sizeof(num));
    std::memcpy(&data, original.data() + i + 4, sizeof(data));
    if (num == kCount && data + sizeof(word) <= original.size()) {
      std::memcpy(&word, original.data() + data, sizeof(word));
      if (word == stride) {
        header = i;
        block = data;
        break;
      }
    }
  }
  ASSERT_NE(0, block);
  uint32_t directory;
  std::memcpy(&directory, original.data() + block + 12, sizeof(directory));
  ASSERT_NE(0, directory);

  const struct {
    size_t offset;
    uint32_t value;
  } corruptions[] = {
      {header, 0xffffffffu},         // num_elements
      {header + 4, 0xfffffff0u},     // data
      {block, 0},                    // stride
      {block, 0xfffffff8u},          // stride
      {block, 8},                    // stride
      {block + 4, 0},                // first_capacity
      {block + 4, 0xffffffffu},      // first_capacity
      {block + 4, 1},                // first_capacity
      {block + 8, 0xfffffff0u},      // metadata
      {block + 12, 0xfffffff0u},     // directory
      {block + 12, block},           // directory
      {directory, 0xffffffffu},      // chunk count
      {directory, 31},               // chunk count
      {directory + 4, 0xfffffff0u},  // first chunk
      {directory + 4, 8},            // first chunk
  };
  for (const auto& corruption : corruptions) {
    std::vector<char> recv = original;
    std::memcpy(recv.data() + corruption.offset, &corruption.value,
                sizeof(corruption.value));
    auto msg =
        foo::bar::phaser::TestMessage::CreateReadonly(recv.data(), recv.size());
    const size_t count = msg.vm_size();
    ASSERT_LE(count, recv.size() / sizeof(uint64_t));
    for (size_t i = 0; i < count + 2; i++) {
      const auto inner = msg.vm(i);
      ASSERT_LE(inner.str().size(), recv.size());
      (void)inner.f();
      (void)inner.ev_size();
    }
    (void)msg.vm().capacity();
    (void)msg.DebugString();
  }
}

// A malicious sender can build a structurally valid payload but corrupt the
// in-buffer offsets/lengths/counts it contains. Attaching to such a buffer with
// CreateReadonly and reading fields must never read outside the received bytes.
// Run under AddressSanitizer (--config=asan) to catch any out-of-bounds access.
TEST(PhaserTest, HostilePayloadIsBounded) {
  foo::bar::phaser::TestMessage src;
  src.set_x(1234);
  src.set_s("hello world");
  src.add_vi32(0x11111111);
  src.add_vi32(0x22222222);
  src.add_vi32(0x33333333);
  src.mutable_m()->set_str("inner");

  // Copy exactly the shipped bytes so we can corrupt them like a hostile peer.
  const size_t n = src.Size();
  const char* base = static_cast<const char*>(src.Data());
  std::vector<char> recv(base, base + n);

  auto find_u32 = [](const std::vector<char>& b, uint32_t v,
                     size_t start) -> long {
    for (size_t i = start; i + sizeof(uint32_t) <= b.size(); ++i) {
      uint32_t w;
      std::memcpy(&w, b.data() + i, sizeof(w));
      if (w == v) {
        return static_cast<long>(i);
      }
    }
    return -1;
  };

  // Baseline: the copied buffer parses and reads back correctly.
  {
    auto msg = foo::bar::phaser::TestMessage::CreateReadonly(recv.data(),
                                                             recv.size());
    ASSERT_EQ(1234, msg.x());
    ASSERT_EQ("hello world", msg.s());
    ASSERT_EQ(3, msg.vi32_size());
    ASSERT_EQ("inner", msg.m().str());
  }

  // 1) Inflating full_size (header offset 12) must not let accessors read past
  //    the received size. Keep it inflated for the remaining corruptions too.
  {
    uint32_t huge = 0xffffffffu;
    std::memcpy(recv.data() + 12, &huge, sizeof(huge));
  }
  {
    auto msg = foo::bar::phaser::TestMessage::CreateReadonly(recv.data(),
                                                             recv.size());
    ASSERT_EQ(1234, msg.x());
    ASSERT_EQ("hello world", msg.s());
    ASSERT_EQ(3, msg.vi32_size());
  }

  // 2) A hostile string length must be clamped to the buffer, not trusted.
  long s_pos = -1;
  for (size_t i = 0; i + 11 <= recv.size(); ++i) {
    if (std::memcmp(recv.data() + i, "hello world", 11) == 0) {
      s_pos = static_cast<long>(i);
      break;
    }
  }
  ASSERT_GE(s_pos, 4);
  {
    uint32_t huge = 0xffffffffu;
    std::memcpy(recv.data() + s_pos - 4, &huge, sizeof(huge));
  }
  {
    auto msg = foo::bar::phaser::TestMessage::CreateReadonly(recv.data(),
                                                             recv.size());
    std::string_view s = msg.s();
    ASSERT_LE(s.size(), recv.size());
    ASSERT_EQ(0, s.compare(0, 11, "hello world"));
  }

  // 3) A hostile repeated-field element count must be clamped. Locate the vi32
  //    data, then the VectorHeader { num_elements=3, data_offset } pointing at
  //    it, and blow up the count.
  const long data_pos = find_u32(recv, 0x11111111u, 0);
  ASSERT_GE(data_pos, 0);
  const uint32_t data_off = static_cast<uint32_t>(data_pos);
  long hdr_pos = -1;
  for (size_t i = 0; i + 2 * sizeof(uint32_t) <= recv.size(); ++i) {
    uint32_t num, off;
    std::memcpy(&num, recv.data() + i, sizeof(num));
    std::memcpy(&off, recv.data() + i + sizeof(uint32_t), sizeof(off));
    if (num == 3 && off == data_off) {
      hdr_pos = static_cast<long>(i);
      break;
    }
  }
  ASSERT_GE(hdr_pos, 0);
  {
    uint32_t huge = 0xffffffffu;
    std::memcpy(recv.data() + hdr_pos, &huge, sizeof(huge));
  }
  {
    auto msg = foo::bar::phaser::TestMessage::CreateReadonly(recv.data(),
                                                             recv.size());
    const int count = msg.vi32_size();
    ASSERT_LE(static_cast<size_t>(count),
              (recv.size() - data_off) / sizeof(int32_t));
    // Iterating the clamped range must stay in-bounds (ASan verifies this).
    long long sum = 0;
    for (int i = 0; i < count; ++i) {
      sum += msg.vi32(i);
    }
    (void)sum;
    // capacity() must not underflow-read before a hostile data offset.
    ASSERT_GE(msg.vi32().capacity(), 0u);
  }

  // 4) Presence bits / has_* and nested-message access must stay in-bounds
  //    even with inflated full_size (already set above).
  {
    auto msg = foo::bar::phaser::TestMessage::CreateReadonly(recv.data(),
                                                             recv.size());
    (void)msg.has_x();
    (void)msg.has_s();
    (void)msg.has_m();
    (void)msg.m().str();
    // String size()/data() used by Serialize must be size-aware.
    ASSERT_LE(msg.s().size(), recv.size());
    const char* sdata = msg.s().data();
    if (sdata != nullptr) {
      ASSERT_GE(static_cast<const void*>(sdata),
                static_cast<const void*>(recv.data()));
      ASSERT_LT(static_cast<const void*>(sdata),
                static_cast<const void*>(recv.data() + recv.size()));
    }
  }
}

TEST(PhaserTest, DeletedFieldsBasic) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  ASSERT_EQ(1234, msg.x());
  ASSERT_EQ(5678, msg.y());
  ASSERT_EQ("hello world", msg.s());

  toolbelt::Hexdump(msg.Data(), msg.Size());

  // Create a TestMessageDeletedFields using the binary from TestMessage.
  auto msg2 = foo::bar::phaser::TestMessageDeletedFields::CreateReadonly(
      msg.Data(), msg.Size());
  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.s(), msg.s());
}

TEST(PhaserTest, HybridFieldMetadataLayout) {
  using Hybrid = foo::bar::phaser::HybridLookupMessage;
  EXPECT_EQ(Hybrid::field_data.header.magic, ::phaser::kHybridFieldDataMagic);
  EXPECT_EQ(Hybrid::field_data.header.dense_base, 10u);
  EXPECT_EQ(Hybrid::field_data.header.dense_span, 6u);
  EXPECT_EQ(Hybrid::field_data.header.sparse_count, 1u);
  EXPECT_EQ(Hybrid::field_data.dense_fields[2].offset, 0u);
  EXPECT_EQ(Hybrid::field_data.sparse_fields[0].number, 1000u);

  Hybrid message;
  message.set_dense_10(10);
  message.set_dense_11(11);
  message.set_dense_13(13);
  message.set_dense_14(14);
  message.set_dense_15(15);
  message.set_sparse_1000(1000);
  EXPECT_EQ(message.dense_10(), 10);
  EXPECT_EQ(message.dense_15(), 15);
  EXPECT_EQ(message.sparse_1000(), 1000);
  EXPECT_EQ(message.FindField(12).offset, -1);
  EXPECT_EQ(message.FindField(1000).offset,
            static_cast<int32_t>(Hybrid::field_data.sparse_fields[0].offset));
}

TEST(PhaserTest, SparseFieldMetadataLayout) {
  using Sparse = foo::bar::phaser::SparseLookupMessage;
  EXPECT_EQ(Sparse::field_data.header.magic, ::phaser::kHybridFieldDataMagic);
  EXPECT_EQ(Sparse::field_data.header.dense_base, 1u);
  EXPECT_EQ(Sparse::field_data.header.dense_span, 1u);
  EXPECT_EQ(Sparse::field_data.header.sparse_count, 2u);
  EXPECT_NE(Sparse::field_data.dense_fields[0].offset, 0u);
  EXPECT_EQ(Sparse::field_data.sparse_fields[0].number, 100u);
  EXPECT_EQ(Sparse::field_data.sparse_fields[1].number, 10000u);

  Sparse message;
  message.set_field_1(1);
  message.set_field_100(100);
  message.set_field_10000(10000);
  EXPECT_EQ(message.field_1(), 1);
  EXPECT_EQ(message.field_100(), 100);
  EXPECT_EQ(message.field_10000(), 10000);
  EXPECT_EQ(message.FindField(99).offset, -1);
}

TEST(PhaserTest, DenseMetadataHandlesOneofAndOutlierFields) {
  using Message = foo::bar::phaser::TestMessage;
  ASSERT_LE(Message::field_data.header.dense_base, 107u);
  ASSERT_GT(Message::field_data.header.dense_base +
                Message::field_data.header.dense_span,
            108u);
  const size_t first_index = 107u - Message::field_data.header.dense_base;
  const size_t second_index = 108u - Message::field_data.header.dense_base;
  EXPECT_EQ(Message::field_data.dense_fields[first_index].offset,
            Message::field_data.dense_fields[second_index].offset);
  ASSERT_EQ(Message::field_data.header.sparse_count, 1u);
  EXPECT_EQ(Message::field_data.sparse_fields[0].number, 200u);
}

TEST(PhaserTest, CopySimple) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto m = msg.mutable_m();
  m->set_str("Inner message");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  msg.set_u1a(4321);
  msg.set_u2a(8765);

  auto u3 = msg.mutable_u3b();
  u3->set_str("u3b");
  msg.runtime->pb->Dump(std::cout);
  toolbelt::Hexdump(msg.runtime->pb, msg.runtime->pb->Size());

  // Copy to phaser.
  foo::bar::phaser::TestMessage msg2;
  ASSERT_TRUE(msg2.CloneFrom(msg).ok());

  ASSERT_EQ(msg2.x(), msg.x());
  ASSERT_EQ(msg2.y(), msg.y());
  ASSERT_EQ(msg2.s(), msg.s());
  ASSERT_EQ(msg2.m().str(), msg.m().str());
  ASSERT_EQ(msg2.vi32_size(), msg.vi32_size());
  ASSERT_EQ(msg2.vstr_size(), msg.vstr_size());
  ASSERT_EQ(msg2.vi32(0), msg.vi32(0));
  ASSERT_EQ(msg2.vi32(1), msg.vi32(1));
  ASSERT_EQ(msg2.vi32(2), msg.vi32(2));
  ASSERT_EQ(msg2.vstr(0), msg.vstr(0));
  ASSERT_EQ(msg2.vstr(1), msg.vstr(1));
  ASSERT_EQ(msg2.vstr(2), msg.vstr(2));

  ASSERT_EQ(msg2.u1a(), msg.u1a());
  ASSERT_EQ(msg2.u2a(), msg.u2a());
  ASSERT_EQ(msg2.u3b().str(), msg.u3b().str());
}

TEST(PhaserTest, Any) {
  foo::bar::phaser::TestMessage msg;
  foo::bar::phaser::InnerMessage inner(4096);
  inner.set_str("Any message is inner");

  auto any = msg.mutable_any();
  ASSERT_TRUE(any->PackFrom(inner));

  foo::bar::phaser::TestMessage msg2;
  ASSERT_TRUE(msg2.CloneFrom(msg).ok());
  foo::bar::phaser::InnerMessage inner2;
  ASSERT_TRUE(msg2.any().Is<foo::bar::phaser::InnerMessage>());
  ASSERT_TRUE(msg2.any().UnpackTo(&inner2));
  ASSERT_EQ(inner.str(), inner2.str());

  foo::bar::phaser::TestMessage msg3;
  foo::bar::phaser::InnerMessage inner3 =
      msg3.mutable_any()->MutableAny<foo::bar::phaser::InnerMessage>();
  inner3.set_str("Any message is inner3");
  ASSERT_EQ(inner3.str(),
            msg3.any().As<foo::bar::phaser::InnerMessage>().str());

  std::cout << msg3;

  foo::bar::TestMessage msg4;
  foo::bar::InnerMessage inner4;
  inner4.set_str("Any message is inner");

  auto any4 = msg4.mutable_any();
  ASSERT_TRUE(any4->PackFrom(inner4));

  std::string pb_debug_string = msg4.DebugString();
  std::string phaser_debug_string = msg.DebugString();
  phaser::test::StripProtobufDebugRedaction(pb_debug_string);
  phaser::test::StripProtobufDebugRedaction(phaser_debug_string);
  ASSERT_EQ(pb_debug_string, phaser_debug_string);
}

// The value's type has no size to serialize with, even given all the room
// SerializedSize() asks for.
TEST(PhaserTest, AnyOfAnUnknownTypeDoesNotSerialize) {
  foo::bar::phaser::TestMessage msg;
  auto any = msg.mutable_any();
  any->set_type_url("type.googleapis.com/no.such.Message");
  any->set_value("value");

  std::vector<char> wire(msg.SerializedSize());
  EXPECT_FALSE(msg.SerializeToArray(wire.data(), wire.size()));
}

TEST(PhaserTest, Garbage) {
  char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = static_cast<char>(rand() & 0xff);
  }
  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, GarbageSmall) {
  char buffer[10];
  for (int i = 0; i < 10; i++) {
    buffer[i] = static_cast<char>(rand() & 0xff);
  }
  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, GarbageLoop) {
  char buffer[256];
  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 256; j++) {
      buffer[j] = static_cast<char>(rand() & 0xff);
    }
    auto msg =
        foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
    ASSERT_FALSE(msg.has_x());
    ASSERT_EQ(0, msg.x());
    ASSERT_FALSE(msg.has_y());
    ASSERT_EQ(0, msg.y());

    ASSERT_FALSE(msg.has_s());
    ASSERT_EQ("", msg.s());

    ASSERT_FALSE(msg.has_m());
    ASSERT_EQ("", msg.m().str());

    ASSERT_EQ(0, msg.vi32_size());
    ASSERT_EQ(0, msg.vstr_size());

    ASSERT_FALSE(msg.has_u1a());
    ASSERT_EQ(0, msg.u1a());
    ASSERT_FALSE(msg.has_u2a());
    ASSERT_EQ(0, msg.u2a());
    ASSERT_FALSE(msg.has_u3b());
    ASSERT_EQ("", msg.u3b().str());

    ASSERT_FALSE(msg.has_any());
    ASSERT_FALSE(msg.any().has_type_url());
    ASSERT_FALSE(msg.any().has_value());
    ASSERT_EQ(0, msg.any().value().size());

    std::cout << msg;
  }
}

TEST(PhaserTest, GarbageWithValidMagic) {
  char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = static_cast<char>(rand() & 0xff);
  }
  // Set the magic.
  *reinterpret_cast<uint32_t*>(buffer) = ::phaser::kFixedBufferMagic;

  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, GarbageWithTailoring) {
  char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = static_cast<char>(rand() & 0xff);
  }
  // Set the magic and the header fields.
  *reinterpret_cast<uint32_t*>(buffer) = ::phaser::kFixedBufferMagic;
  *reinterpret_cast<uint32_t*>(buffer + 4) = 0x1c;   // message location.
  *reinterpret_cast<uint32_t*>(buffer + 8) = 1024;   // hwm.
  *reinterpret_cast<uint32_t*>(buffer + 12) = 1000;  // full_size
  *reinterpret_cast<uint32_t*>(buffer + 16) = 50;    // free list

  auto msg =
      foo::bar::phaser::TestMessage::CreateReadonly(buffer, sizeof(buffer));
  ASSERT_FALSE(msg.has_x());
  ASSERT_EQ(0, msg.x());
  ASSERT_FALSE(msg.has_y());
  ASSERT_EQ(0, msg.y());

  ASSERT_FALSE(msg.has_s());
  ASSERT_EQ("", msg.s());

  ASSERT_FALSE(msg.has_m());
  ASSERT_EQ("", msg.m().str());

  ASSERT_EQ(0, msg.vi32_size());
  ASSERT_EQ(0, msg.vstr_size());

  ASSERT_FALSE(msg.has_u1a());
  ASSERT_EQ(0, msg.u1a());
  ASSERT_FALSE(msg.has_u2a());
  ASSERT_EQ(0, msg.u2a());
  ASSERT_FALSE(msg.has_u3b());
  ASSERT_EQ("", msg.u3b().str());

  ASSERT_FALSE(msg.has_any());
  ASSERT_FALSE(msg.any().has_type_url());
  ASSERT_FALSE(msg.any().has_value());
  ASSERT_EQ(0, msg.any().value().size());

  std::cout << msg;
}

TEST(PhaserTest, Reflection) {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(1234);
  msg.set_y(5678);
  msg.set_s("hello world");

  auto m = msg.mutable_m();
  m->set_str("Inner message");

  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vi32(3);

  msg.add_vstr("one");
  msg.add_vstr("two");
  msg.add_vstr("three");

  msg.set_u1a(4321);
  msg.set_u2a(8765);

  absl::StatusOr<const ::phaser::MessageInfo*> info =
      ::phaser::PhaserBankMessageInfo("foo.bar.TestMessage");
  ASSERT_TRUE(info.ok());
  ASSERT_NE(nullptr, *info);
  ASSERT_EQ(16, (*info)->fields_in_order.size());

  std::shared_ptr<::phaser::FieldInfo> field_x = (*info)->fields_in_order[0];
  ASSERT_EQ("x", field_x->name);
  ASSERT_EQ(100, field_x->number);
  ASSERT_EQ(::phaser::FieldType::kFieldInt32, field_x->type);

  ::phaser::PrimitiveFieldInfo* pf_x =
      static_cast<::phaser::PrimitiveFieldInfo*>(field_x.get());
  ASSERT_NE(nullptr, pf_x);
  ASSERT_FALSE(pf_x->is_repeated);
  ASSERT_FALSE(pf_x->is_packed);
  ASSERT_FALSE(pf_x->is_signed);
  ASSERT_FALSE(pf_x->is_fixed);

  auto it = (*info)->fields_by_name.find("x");
  ASSERT_NE((*info)->fields_by_name.end(), it);
  ASSERT_EQ(field_x, it->second);
  ASSERT_EQ(100, it->second->number);

  int x_number = it->second->number;

  // Let's print all the fields.
  for (const auto& field : (*info)->fields_in_order) {
    std::cout << field->name << " " << field->number << " "
              << static_cast<int>(field->type) << " " << field->offset
              << std::endl;
  }
  absl::StatusOr<bool> has_x =
      ::phaser::PhaserBankHasField("foo.bar.TestMessage", msg, x_number);
  ASSERT_TRUE(has_x.ok());

  // The field type is the one declared in the generated message, which has
  // the field's offset, presence id and number in it.
  using XField =
      ::phaser::Int32Field<false, false, ::phaser::FieldSpec<8, 0, 100>>;
  absl::StatusOr<XField*> x = ::phaser::PhaserBankGetFieldByNumber<XField>(
      "foo.bar.TestMessage", msg, 100);
  ASSERT_TRUE(x.ok());
  ASSERT_NE(nullptr, *x);
  ASSERT_EQ(1234, (*x)->Get());

  // Repeated field
  absl::StatusOr<bool> has_vi32 =
      ::phaser::PhaserBankHasField("foo.bar.TestMessage", msg, 104);
  ASSERT_TRUE(has_vi32.ok());

  using Vi32Field =
      std::remove_cv_t<std::remove_reference_t<decltype(msg.vi32())>>;
  auto vi32 = ::phaser::PhaserBankGetFieldByNumber<Vi32Field>(
      "foo.bar.TestMessage", msg, 104);
  ASSERT_TRUE(vi32.ok());
  ASSERT_NE(nullptr, *vi32);

  Vi32Field& vi32r = **vi32;
  ASSERT_EQ(3, vi32r.Size());
  ASSERT_EQ(1, vi32r[0]);

  for (auto& v : vi32r) {
    std::cout << v << std::endl;
  }
}

TEST(DebugRedaction, StripsMarkerLine) {
  std::string s = "goo.gle/debugstr-abc123\nx: 1\ny: 2\n";
  phaser::test::StripProtobufDebugRedaction(s);
  ASSERT_EQ("x: 1\ny: 2\n", s);
}

TEST(DebugRedaction, StripsMarkerWithLeadingWhitespace) {
  // Protobuf may emit a random amount of leading whitespace before the marker.
  std::string s = "    goo.gle/debugproto\nfield: value\n";
  phaser::test::StripProtobufDebugRedaction(s);
  ASSERT_EQ("field: value\n", s);
}

TEST(DebugRedaction, LeavesUnmarkedStringUntouched) {
  std::string s = "x: 1\ny: 2\n";
  phaser::test::StripProtobufDebugRedaction(s);
  ASSERT_EQ("x: 1\ny: 2\n", s);
}

TEST(DebugRedaction, DoesNotStripMidContentMarker) {
  // A marker that is not the leading prefix (real content precedes it) must be
  // left alone rather than truncating legitimate output.
  std::string s = "name: \"goo.gle/debugstr\"\nx: 1\n";
  std::string original = s;
  phaser::test::StripProtobufDebugRedaction(s);
  ASSERT_EQ(original, s);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
