// Fills fixed size buffers until they run out of space.  Every operation must
// stay inside the buffer, report the failure and leave earlier contents intact.
// This test and the generated code it uses are built without exceptions or
// RTTI.

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "phaser/testdata/FixedBuffer.phaser.h"
#include "phaser/testdata/fixed_buffer_ros_phaser/phaser/testdata/FixedBuffer.phaser.h"

namespace {

using Message = foo::bar::phaser::FixedBufferMessage;
using RosMessage = foo::bar::phaser_ros::FixedBufferMessage;

constexpr std::array<phaser::Tuning, 2> kTunings = {
    phaser::Tuning::kPerformance, phaser::Tuning::kSize};

constexpr size_t kLargeBuffer = 65536;

// Bytes after the end of the buffer under test that must never change.
constexpr size_t kCanarySize = 64;
constexpr std::byte kCanary{0xa5};

void SetCanary(std::vector<std::byte>& storage, size_t size) {
  std::fill(storage.begin() + static_cast<ptrdiff_t>(size),
            storage.begin() + static_cast<ptrdiff_t>(size + kCanarySize),
            kCanary);
}

bool CanaryIntact(const std::vector<std::byte>& storage, size_t size) {
  for (size_t i = size; i < size + kCanarySize; i++) {
    if (storage[i] != kCanary) {
      return false;
    }
  }
  return true;
}

template <typename T>
std::string Serialized(const T& message) {
  std::string out;
  out.resize(message.SerializedSize());
  if (!out.empty() && !message.SerializeToArray(out.data(), out.size())) {
    return "<serialize failed>";
  }
  return out;
}

void Populate(Message& msg) {
  msg.set_x(42);
  msg.set_name("a name that needs some space in the buffer");
  msg.mutable_inner()->set_id(7);
  msg.mutable_inner()->set_label("inner label");
  for (int i = 0; i < 20; i++) {
    msg.add_values(i * 3);
  }
  for (int i = 0; i < 6; i++) {
    msg.add_names("name number " + std::to_string(i));
  }
  for (int i = 0; i < 4; i++) {
    auto inner = msg.add_inners();
    inner.set_id(100 + i);
    inner.set_label("repeated inner " + std::to_string(i));
  }
  msg.mutable_choice_inner()->set_id(9);
  msg.mutable_choice_inner()->set_label("choice");
  msg.set_payload(std::string(64, '\x5a'));
}

void Populate(RosMessage& msg) {
  msg.x = 42;
  msg.name = "a name that needs some space in the buffer";
  msg.inner->id = 7;
  msg.inner->label = "inner label";
  for (int i = 0; i < 20; i++) {
    msg.values.push_back(i * 3);
  }
  for (int i = 0; i < 6; i++) {
    msg.names.push_back("name number " + std::to_string(i));
  }
  for (int i = 0; i < 4; i++) {
    auto inner = msg.inners.Add();
    inner->id = 100 + i;
    inner->label = "repeated inner " + std::to_string(i);
  }
  for (size_t i = 0; i < 4; i++) {
    msg.fixed_values[i] = static_cast<int32_t>(i + 1);
  }
  int32_t extra = 10;
  for (int32_t& v : msg.fixed_values) {
    v += extra++;
  }
  msg.fixed_names[0] = "first fixed name";
  msg.fixed_names[1] = "second fixed name";
  msg.fixed_inners[0]->id = 21;
  msg.fixed_inners[1]->label = "fixed inner";
  msg.choice.emplace<RosMessage::ChoiceInnerAlternative>().id = 9;
  msg.payload = std::string(64, '\x5a');
}

template <typename T>
std::string ReferenceWire() {
  static std::vector<std::byte> storage(kLargeBuffer);
  T msg = T::CreateMutable(storage.data(), storage.size());
  Populate(msg);
  EXPECT_FALSE(msg.AllocationFailed());
  return Serialized(msg);
}

TEST(FixedBufferTest, TryCreateMutableChecksTheBufferSize) {
  alignas(8) std::array<std::byte, 8> tiny{};
  absl::StatusOr<Message> too_small =
      Message::TryCreateMutable(tiny.data(), tiny.size());
  EXPECT_EQ(too_small.status().code(), absl::StatusCode::kInvalidArgument);

  alignas(8) std::array<std::byte, sizeof(phaser::PayloadBuffer) + 32>
      no_room_for_message{};
  absl::StatusOr<Message> no_room = Message::TryCreateMutable(
      no_room_for_message.data(), no_room_for_message.size());
  EXPECT_EQ(no_room.status().code(), absl::StatusCode::kResourceExhausted);

  alignas(8) std::array<std::byte, 4096> enough{};
  absl::StatusOr<Message> ok = Message::TryCreateMutable(enough.data(),
                                                         enough.size());
  ASSERT_TRUE(ok.ok()) << ok.status();
  EXPECT_FALSE(ok->AllocationFailed());
  ok->set_x(1);
  EXPECT_EQ(ok->x(), 1);
}

TEST(FixedBufferTest, FailedStringSetKeepsTheOldValue) {
  for (phaser::Tuning tuning : kTunings) {
    alignas(8) std::array<std::byte, 1024> storage{};
    Message msg = Message::CreateMutable(storage.data(), storage.size(), tuning);
    msg.set_x(5);
    msg.set_name("hello");
    ASSERT_FALSE(msg.AllocationFailed());

    msg.set_name(std::string(4096, 'x'));
    EXPECT_TRUE(msg.AllocationFailed());
    EXPECT_EQ(msg.name(), "hello");
    EXPECT_EQ(msg.x(), 5);

    std::array<char, 4096> wire{};
    phaser::ProtoBuffer out(wire.data(), wire.size());
    EXPECT_EQ(msg.Serialize(out).code(), absl::StatusCode::kResourceExhausted);
    EXPECT_FALSE(msg.SerializeToArray(wire.data(), wire.size()));

    // A receiver sees the incomplete buffer as invalid.
    Message received = Message::CreateReadonly(msg.Data(), msg.Size());
    EXPECT_EQ(received.x(), 0);
    EXPECT_EQ(received.name(), "");
  }
}

TEST(FixedBufferTest, RepeatedFieldsStopGrowingAndKeepTheirValues) {
  for (phaser::Tuning tuning : kTunings) {
    alignas(8) std::array<std::byte, 2048> storage{};
    Message msg = Message::CreateMutable(storage.data(), storage.size(), tuning);

    size_t values = 0;
    while (!msg.AllocationFailed()) {
      msg.add_values(static_cast<int32_t>(values));
      if (msg.values_size() == values) {
        break;
      }
      values++;
      ASSERT_LT(values, 10000u);
    }
    ASSERT_TRUE(msg.AllocationFailed());
    ASSERT_EQ(msg.values_size(), values);
    for (size_t i = 0; i < values; i++) {
      EXPECT_EQ(msg.values(i), static_cast<int32_t>(i));
    }

    size_t names = 0;
    for (;;) {
      msg.add_names("name " + std::to_string(names));
      if (msg.names_size() == names) {
        break;
      }
      names++;
      ASSERT_LT(names, 10000u);
    }
    for (size_t i = 0; i < names; i++) {
      EXPECT_EQ(msg.names(i), "name " + std::to_string(i));
    }

    size_t inners = 0;
    for (;;) {
      auto inner = msg.add_inners();
      inner.set_id(static_cast<int32_t>(inners));
      if (msg.inners_size() == inners) {
        break;
      }
      inners++;
      ASSERT_LT(inners, 10000u);
    }
    for (size_t i = 0; i < inners; i++) {
      EXPECT_EQ(msg.inners(i).id(), static_cast<int32_t>(i));
    }

    const std::array<std::byte, 2048> before = storage;
    auto detached = msg.add_inners();
    detached.set_id(3);
    detached.set_label("goes nowhere");
    EXPECT_EQ(storage, before);
    EXPECT_EQ(msg.inners_size(), inners);
    EXPECT_EQ(msg.names_size(), names);
    EXPECT_EQ(msg.values_size(), values);
  }
}

TEST(FixedBufferTest, SubMessageWritesAfterFailureStayInBounds) {
  for (phaser::Tuning tuning : kTunings) {
    alignas(8) std::array<std::byte, 1024> storage{};
    Message msg = Message::CreateMutable(storage.data(), storage.size(), tuning);
    msg.set_x(11);
    while (!msg.AllocationFailed()) {
      msg.add_names("filler string");
    }
    const std::array<std::byte, 1024> before = storage;

    msg.mutable_inner()->set_id(7);
    msg.mutable_inner()->set_label("label");
    msg.mutable_choice_inner()->set_id(8);
    auto element = msg.add_inners();
    element.set_id(9);

    EXPECT_FALSE(msg.has_inner());
    EXPECT_EQ(msg.inner().id(), 0);
    EXPECT_EQ(msg.x(), 11);
    // Only the choice discriminator may change.
    size_t changed = 0;
    for (size_t i = 0; i < storage.size(); i++) {
      changed += storage[i] != before[i] ? 1 : 0;
    }
    EXPECT_LE(changed, sizeof(int32_t));
  }
}

void AddFiller(Message& msg) { msg.add_names("filler string"); }
void AddFiller(RosMessage& msg) { msg.names.push_back("filler string"); }

template <typename T>
void ExpectClearUndoesAFailure() {
  const std::string wire = ReferenceWire<T>();
  ASSERT_NE(wire, "<serialize failed>");
  constexpr size_t kSize = 4096;
  for (phaser::Tuning tuning : kTunings) {
    std::vector<std::byte> storage(kSize + kCanarySize);
    SetCanary(storage, kSize);
    T msg = T::CreateMutable(storage.data(), kSize, tuning);
    Populate(msg);
    ASSERT_FALSE(msg.AllocationFailed());
    for (int i = 0; i < 1000 && !msg.AllocationFailed(); i++) {
      AddFiller(msg);
    }
    ASSERT_TRUE(msg.AllocationFailed());

    msg.Clear();
    EXPECT_FALSE(msg.AllocationFailed());
    Populate(msg);
    EXPECT_FALSE(msg.AllocationFailed());
    EXPECT_EQ(Serialized(msg), wire);
    EXPECT_TRUE(CanaryIntact(storage, kSize));
  }
}

TEST(FixedBufferTest, ClearAfterFailureLetsTheMessageBeFilledAgain) {
  ExpectClearUndoesAFailure<Message>();
  ExpectClearUndoesAFailure<RosMessage>();
}

TEST(FixedBufferTest, ClearAfterFailureOfAMessageOwningOneBlock) {
  using Inner = foo::bar::phaser::FixedBufferInner;
  for (phaser::Tuning tuning : kTunings) {
    alignas(8) std::array<std::byte, 1024> fresh_storage{};
    Inner fresh = Inner::CreateMutable(fresh_storage.data(),
                                       fresh_storage.size(), tuning);
    fresh.set_id(3);
    fresh.set_label("label");
    const std::string wire = Serialized(fresh);
    ASSERT_NE(wire, "<serialize failed>");

    alignas(8) std::array<std::byte, 1024> storage{};
    Inner msg = Inner::CreateMutable(storage.data(), storage.size(), tuning);
    msg.set_label(std::string(4096, 'x'));
    ASSERT_TRUE(msg.AllocationFailed());

    msg.Clear();
    EXPECT_FALSE(msg.AllocationFailed());
    msg.set_id(3);
    msg.set_label("label");
    EXPECT_FALSE(msg.AllocationFailed());
    EXPECT_EQ(Serialized(msg), wire);
  }
}

TEST(FixedBufferTest, ParseIntoEveryBufferSizeSucceedsOrReportsExhaustion) {
  const std::string wire = ReferenceWire<Message>();
  ASSERT_NE(wire, "<serialize failed>");
  for (phaser::Tuning tuning : kTunings) {
    std::vector<std::byte> storage(4096 + kCanarySize);
    size_t successes = 0;
    for (size_t size = sizeof(phaser::PayloadBuffer) + 16;
         size + kCanarySize <= storage.size(); size += 4) {
      SetCanary(storage, size);
      absl::StatusOr<Message> msg =
          Message::TryCreateMutable(storage.data(), size, tuning);
      if (!msg.ok()) {
        continue;
      }
      phaser::ProtoBuffer in(wire.data(), wire.size());
      absl::Status status = msg->Deserialize(in);
      if (status.ok()) {
        EXPECT_FALSE(msg->AllocationFailed());
        EXPECT_EQ(Serialized(*msg), wire) << "size " << size;
        successes++;
      } else {
        EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted)
            << "size " << size << ": " << status;
        EXPECT_TRUE(msg->AllocationFailed()) << "size " << size;
      }
      EXPECT_TRUE(CanaryIntact(storage, size)) << "size " << size;
    }
    EXPECT_GT(successes, 0u);
  }
}

TEST(FixedBufferTest, BuildInEveryBufferSizeSucceedsOrReportsExhaustion) {
  const std::string wire = ReferenceWire<Message>();
  for (phaser::Tuning tuning : kTunings) {
    std::vector<std::byte> storage(4096 + kCanarySize);
    size_t successes = 0;
    for (size_t size = sizeof(phaser::PayloadBuffer) + 16;
         size + kCanarySize <= storage.size(); size += 4) {
      SetCanary(storage, size);
      absl::StatusOr<Message> msg =
          Message::TryCreateMutable(storage.data(), size, tuning);
      if (!msg.ok()) {
        continue;
      }
      Populate(*msg);
      if (msg->AllocationFailed()) {
        std::array<char, 8192> out{};
        EXPECT_FALSE(msg->SerializeToArray(out.data(), out.size()));
      } else {
        EXPECT_EQ(Serialized(*msg), wire) << "size " << size;
        successes++;
      }
      EXPECT_TRUE(CanaryIntact(storage, size)) << "size " << size;
    }
    EXPECT_GT(successes, 0u);
  }
}

TEST(FixedBufferTest, RosArraysInEveryBufferSize) {
  const std::string wire = ReferenceWire<RosMessage>();
  ASSERT_NE(wire, "<serialize failed>");
  for (phaser::Tuning tuning : kTunings) {
    std::vector<std::byte> storage(4096 + kCanarySize);
    size_t built = 0;
    size_t parsed = 0;
    for (size_t size = sizeof(phaser::PayloadBuffer) + 16;
         size + kCanarySize <= storage.size(); size += 4) {
      SetCanary(storage, size);
      absl::StatusOr<RosMessage> msg =
          RosMessage::TryCreateMutable(storage.data(), size, tuning);
      if (!msg.ok()) {
        continue;
      }
      Populate(*msg);
      if (!msg->AllocationFailed()) {
        EXPECT_EQ(Serialized(*msg), wire) << "size " << size;
        built++;
      }

      absl::StatusOr<RosMessage> target =
          RosMessage::TryCreateMutable(storage.data(), size, tuning);
      ASSERT_TRUE(target.ok());
      phaser::ProtoBuffer in(wire.data(), wire.size());
      absl::Status status = target->Deserialize(in);
      if (status.ok()) {
        EXPECT_EQ(Serialized(*target), wire) << "size " << size;
        parsed++;
      } else {
        EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted)
            << "size " << size << ": " << status;
      }
      EXPECT_TRUE(CanaryIntact(storage, size)) << "size " << size;
    }
    EXPECT_GT(built, 0u);
    EXPECT_GT(parsed, 0u);
  }
}

TEST(FixedBufferTest, RosArrayAccessInEveryBufferSize) {
  for (phaser::Tuning tuning : kTunings) {
    std::vector<std::byte> storage(4096 + kCanarySize);
    size_t complete = 0;
    for (size_t size = sizeof(phaser::PayloadBuffer) + 16;
         size + kCanarySize <= storage.size(); size += 4) {
      SetCanary(storage, size);
      absl::StatusOr<RosMessage> msg =
          RosMessage::TryCreateMutable(storage.data(), size, tuning);
      if (!msg.ok()) {
        continue;
      }
      msg->x = 3;
      msg->fixed_values[3] = 99;
      int32_t next = 1;
      for (int32_t& v : msg->fixed_values) {
        v = next++;
      }
      msg->fixed_names[1] = "a fixed name";
      msg->fixed_inners[1]->id = 5;
      msg->fixed_inners[0]->label = "a fixed inner";
      msg->inners.Add()->id = 6;
      EXPECT_TRUE(CanaryIntact(storage, size)) << "size " << size;
      EXPECT_EQ(msg->x.Get(), 3);
      if (msg->AllocationFailed()) {
        continue;
      }
      complete++;
      ASSERT_NE(msg->fixed_values.data(), nullptr);
      EXPECT_EQ(msg->fixed_values[0], 1);
      EXPECT_EQ(msg->fixed_values[3], 4);
      EXPECT_EQ(std::string_view(msg->fixed_names[1]), "a fixed name");
      EXPECT_EQ(msg->fixed_inners[1]->id.Get(), 5);
      EXPECT_EQ(std::string_view(msg->fixed_inners[0]->label), "a fixed inner");
      EXPECT_EQ(msg->inners[0]->id.Get(), 6);
    }
    EXPECT_GT(complete, 0u);
  }
}

}  // namespace
