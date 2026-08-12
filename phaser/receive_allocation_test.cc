#include "phaser/testdata/RosCompile.phaser.h"
#include "phaser/testdata/RosIntrinsics.phaser.h"
#include "phaser/testdata/TestMessage.phaser.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace {

std::atomic<bool> g_count_allocations = false;
std::atomic<size_t> g_allocation_count = 0;

void CountAllocation() {
  if (g_count_allocations.load(std::memory_order_relaxed)) {
    g_allocation_count.fetch_add(1, std::memory_order_relaxed);
  }
}

void* Allocate(size_t size) {
  CountAllocation();
  if (void* result = std::malloc(size == 0 ? 1 : size); result != nullptr) {
    return result;
  }
  throw std::bad_alloc();
}

void* AllocateAligned(size_t size, size_t alignment) {
  CountAllocation();
  void* result = nullptr;
  if (posix_memalign(&result, alignment, size == 0 ? 1 : size) == 0) {
    return result;
  }
  throw std::bad_alloc();
}

struct ExerciseResult {
  bool values_match = true;
  bool protobuf_serialized = true;
  bool ros_serialized = true;
};

template <typename Message>
std::vector<char> CopyPayload(const Message& message) {
  const size_t size = message.ByteSizeLong();
  std::vector<char> result(size);
  std::memcpy(result.data(), message.Data(), size);
  return result;
}

ExerciseResult Exercise(
    const std::vector<char>& ros_compile_payload,
    const std::vector<char>& ros_intrinsic_payload,
    const std::vector<char>& any_payload,
    const std::vector<char>& dense_payload,
    const std::vector<char>& sparse_payload) {
  ExerciseResult result;

  auto ros_message =
      foo::bar::phaser::RosCompileMessage::CreateReadonly(
          ros_compile_payload.data(), ros_compile_payload.size());
  const auto& ros_view = ros_message;
  result.values_match &= ros_view.x.Get() == 42;
  result.values_match &= ros_view.name.Get() == "root";
  result.values_match &=
      ros_view.choice
          .get<foo::bar::phaser::RosCompileMessage::ChoiceCountAlternative>() ==
      17;
  result.values_match &= ros_view.names.size() == 2;
  size_t string_count = 0;
  for (std::string_view value : ros_view.names) {
    result.values_match &= !value.empty();
    ++string_count;
  }
  result.values_match &= string_count == 2;
  size_t message_count = 0;
  for (auto value : ros_view.inners) {
    result.values_match &= value.id.Get() > 0;
    ++message_count;
  }
  result.values_match &= message_count == 2;
  const auto first_inner = ros_view.inners[0];
  auto copied_inner = first_inner;
  const auto moved_inner = std::move(copied_inner);
  result.values_match &= moved_inner.id.Get() == 1;
  result.values_match &= ros_view.fixed_names[0] == "fixed";
  result.values_match &= ros_view.fixed_inners[0].id.Get() == 9;

  char protobuf_output[4096];
  result.protobuf_serialized &=
      ros_view.SerializeToArray(protobuf_output, sizeof(protobuf_output));
  char ros_output[4096];
  result.ros_serialized &=
      ros_view.SerializeToROSArray(ros_output, sizeof(ros_output)).ok();

  auto intrinsic_message =
      foo::bar::phaser::RosIntrinsicMessage::CreateReadonly(
          ros_intrinsic_payload.data(), ros_intrinsic_payload.size());
  const auto& intrinsic_view = intrinsic_message;
  const phaser::RosHeaderView header = intrinsic_view.header.Get();
  result.values_match &= header.seq == 7;
  result.values_match &= header.stamp.sec == 11;
  result.values_match &= header.frame_id == "allocation-free-frame";

  auto any_message = foo::bar::phaser::TestMessage::CreateReadonly(
      any_payload.data(), any_payload.size());
  const auto& any_view = any_message;
  result.values_match &=
      any_view.any().Is<foo::bar::phaser::InnerMessage>();
  const auto embedded =
      any_view.any().As<foo::bar::phaser::InnerMessage>();
  result.values_match &= embedded.str() == "embedded";
  result.values_match &= any_view.has_u3b();
  result.values_match &= any_view.u3b().str() == "oneof-message";
  result.protobuf_serialized &=
      any_view.SerializeToArray(protobuf_output, sizeof(protobuf_output));

  const auto dense_view =
      foo::bar::phaser::HybridLookupMessage::CreateReadonly(
          dense_payload.data(), dense_payload.size());
  const auto sparse_view =
      foo::bar::phaser::SparseLookupMessage::CreateReadonly(
          sparse_payload.data(), sparse_payload.size());
  result.values_match &= dense_view.dense_10() == 10;
  result.values_match &= dense_view.sparse_1000() == 1000;
  result.values_match &= sparse_view.field_1() == 1;
  result.values_match &= sparse_view.field_10000() == 10000;

  return result;
}

ExerciseResult ExerciseFixedOutput() {
  ExerciseResult result;
  alignas(std::max_align_t) std::array<std::byte, 65536> ros_storage{};
  alignas(std::max_align_t) std::array<std::byte, 32768> header_storage{};
  alignas(std::max_align_t) std::array<std::byte, 65536> any_storage{};
  std::array<char, 65536> protobuf_output{};
  std::array<char, 65536> ros_output{};

  auto ros = foo::bar::phaser::RosCompileMessage::CreateMutable(
      ros_storage.data(), ros_storage.size());
  ros.x = 42;
  ros.name = "fixed-output";
  ros.flag = true;
  ros.choice
      .emplace<
          foo::bar::phaser::RosCompileMessage::ChoiceCountAlternative>(17);
  ros.names.push_back("first");
  ros.names.push_back("second");
  ros.inners.Add()->id = 7;
  ros.fixed_names[0] = "array";
  ros.fixed_inners[0]->id = 8;
  result.values_match &= ros.x.Get() == 42;
  result.values_match &= ros.name.Get() == "fixed-output";
  result.values_match &= ros.Data() == ros_storage.data();
  result.values_match &= ros.Size() > 0;
  result.protobuf_serialized &=
      ros.SerializeToArray(protobuf_output.data(), protobuf_output.size());
  result.ros_serialized &=
      ros.SerializeToROSArray(ros_output.data(), ros_output.size()).ok();

  auto intrinsic = foo::bar::phaser::RosIntrinsicMessage::CreateMutable(
      header_storage.data(), header_storage.size());
  {
    auto header = intrinsic.header.Mutable();
    header.seq = 9;
    header.stamp = ::ros::Time(21, 654);
    header.frame_id = "map";
  }
  result.values_match &= intrinsic.header.Get().frame_id == "map";
  result.protobuf_serialized &= intrinsic.SerializeToArray(
      protobuf_output.data(), protobuf_output.size());
  result.ros_serialized &=
      intrinsic.SerializeToROSArray(ros_output.data(), ros_output.size()).ok();

  auto any = foo::bar::phaser::TestMessage::CreateMutable(
      any_storage.data(), any_storage.size());
  any.set_x(5);
  any.set_s("outer");
  any.add_vstr("one");
  any.add_vstr("two");
  any.add_vm()->set_str("nested");
  any.set_u2b("union");
  auto embedded =
      any.mutable_any()->MutableAny<foo::bar::phaser::InnerMessage>();
  embedded.set_str("typed-any");
  result.values_match &=
      any.any().Is<foo::bar::phaser::InnerMessage>() &&
      any.any().As<foo::bar::phaser::InnerMessage>().str() == "typed-any";
  result.protobuf_serialized &=
      any.SerializeToArray(protobuf_output.data(), protobuf_output.size());

  return result;
}

ExerciseResult ExerciseWireDeserialization(std::string_view any_wire,
                                           std::string_view protobuf_ros_wire,
                                           std::string_view ros_wire) {
  ExerciseResult result;
  alignas(std::max_align_t) std::array<std::byte, 65536> message_storage{};
  alignas(std::max_align_t) std::array<std::byte, 65536> header_storage{};
  alignas(std::max_align_t) std::array<std::byte, 4096> ros_storage{};
  std::array<char, 4096> protobuf_storage{};

  auto parsed = foo::bar::phaser::TestMessage::CreateMutable(
      message_storage.data(), message_storage.size());
  result.protobuf_serialized &=
      parsed.ParseFromArray(any_wire.data(), any_wire.size());
  result.values_match &=
      parsed.any().Is<foo::bar::phaser::InnerMessage>() &&
      parsed.any().As<foo::bar::phaser::InnerMessage>().str() == "wire-any";

  ::phaser::ROSBuffer ros_output(ros_storage.data(), ros_storage.size());
  result.ros_serialized &=
      foo::bar::phaser::RosCompileMessage::ProtobufToROS(protobuf_ros_wire,
                                                         ros_output)
          .ok();
  result.values_match &= !ros_output.empty();

  auto intrinsic = foo::bar::phaser::RosIntrinsicMessage::CreateMutable(
      header_storage.data(), header_storage.size());
  result.ros_serialized &=
      intrinsic
          .ParseFromROS(
              absl::Span<const char>(ros_wire.data(), ros_wire.size()))
          .ok();
  result.values_match &= intrinsic.header.Get().frame_id == "wire-map";

  ::phaser::ProtoBuffer protobuf_output(protobuf_storage.data(),
                                        protobuf_storage.size());
  result.protobuf_serialized &=
      foo::bar::phaser::RosIntrinsicMessage::ROSToProtobuf(
          absl::Span<const char>(ros_wire.data(), ros_wire.size()),
          protobuf_output)
          .ok();
  result.values_match &= protobuf_output.Size() > 0;
  return result;
}

}  // namespace

void* operator new(size_t size) { return Allocate(size); }
void* operator new[](size_t size) { return Allocate(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  try {
    return Allocate(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  try {
    return Allocate(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new(size_t size, std::align_val_t alignment) {
  return AllocateAligned(size, static_cast<size_t>(alignment));
}
void* operator new[](size_t size, std::align_val_t alignment) {
  return AllocateAligned(size, static_cast<size_t>(alignment));
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  std::free(ptr);
}
void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  std::free(ptr);
}
void operator delete(void* ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept {
  std::free(ptr);
}
void operator delete(void* ptr, size_t, std::align_val_t) noexcept {
  std::free(ptr);
}
void operator delete[](void* ptr, size_t, std::align_val_t) noexcept {
  std::free(ptr);
}

TEST(ReceiveAllocationTest, TypedNativeReceiveAndSerializationAllocateNothing) {
  foo::bar::phaser::RosCompileMessage ros_message;
  ros_message.x = 42;
  ros_message.name = "root";
  ros_message.choice.emplace<
      foo::bar::phaser::RosCompileMessage::ChoiceCountAlternative>(17);
  ros_message.names.push_back("first");
  ros_message.names.push_back("second");
  ros_message.inners.Add()->id = 1;
  ros_message.inners.Add()->id = 2;
  ros_message.fixed_names[0] = "fixed";
  ros_message.fixed_inners[0]->id = 9;
  const std::vector<char> ros_payload = CopyPayload(ros_message);

  foo::bar::phaser::RosIntrinsicMessage intrinsic_message;
  intrinsic_message.header->seq = 7;
  intrinsic_message.header->stamp = ::ros::Time(11, 13);
  intrinsic_message.header->frame_id = "allocation-free-frame";
  const std::vector<char> intrinsic_payload = CopyPayload(intrinsic_message);

  foo::bar::phaser::TestMessage any_message;
  auto embedded =
      any_message.mutable_any()->MutableAny<foo::bar::phaser::InnerMessage>();
  embedded.set_str("embedded");
  any_message.mutable_u3b()->set_str("oneof-message");
  const std::vector<char> any_payload = CopyPayload(any_message);

  foo::bar::phaser::HybridLookupMessage dense_message;
  dense_message.set_dense_10(10);
  dense_message.set_sparse_1000(1000);
  const std::vector<char> dense_payload = CopyPayload(dense_message);
  foo::bar::phaser::SparseLookupMessage sparse_message;
  sparse_message.set_field_1(1);
  sparse_message.set_field_10000(10000);
  const std::vector<char> sparse_payload = CopyPayload(sparse_message);

  // Prime process-wide bank and status internals before measuring the receive
  // path itself.
  ExerciseResult warmup =
      Exercise(ros_payload, intrinsic_payload, any_payload, dense_payload,
               sparse_payload);
  ASSERT_TRUE(warmup.values_match);
  ASSERT_TRUE(warmup.protobuf_serialized);
  ASSERT_TRUE(warmup.ros_serialized);

  g_allocation_count.store(0, std::memory_order_relaxed);
  g_count_allocations.store(true, std::memory_order_relaxed);
  ExerciseResult measured =
      Exercise(ros_payload, intrinsic_payload, any_payload, dense_payload,
               sparse_payload);
  g_count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_TRUE(measured.values_match);
  EXPECT_TRUE(measured.protobuf_serialized);
  EXPECT_TRUE(measured.ros_serialized);
  EXPECT_EQ(g_allocation_count.load(std::memory_order_relaxed), 0u);
}

TEST(OutputAllocationTest, FixedBufferTypedMutationAndSerializationAllocateNothing) {
  ExerciseResult warmup = ExerciseFixedOutput();
  ASSERT_TRUE(warmup.values_match);
  ASSERT_TRUE(warmup.protobuf_serialized);
  ASSERT_TRUE(warmup.ros_serialized);

  g_allocation_count.store(0, std::memory_order_relaxed);
  g_count_allocations.store(true, std::memory_order_relaxed);
  ExerciseResult measured = ExerciseFixedOutput();
  g_count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_TRUE(measured.values_match);
  EXPECT_TRUE(measured.protobuf_serialized);
  EXPECT_TRUE(measured.ros_serialized);
  EXPECT_EQ(g_allocation_count.load(std::memory_order_relaxed), 0u);
}

TEST(OutputAllocationTest,
     AnyDeserializationAndProtobufToROSAllocateNothingWithFixedBuffers) {
  foo::bar::phaser::TestMessage any_source;
  any_source.mutable_any()
      ->MutableAny<foo::bar::phaser::InnerMessage>()
      .set_str("wire-any");
  const std::string any_wire = any_source.SerializeAsString();

  foo::bar::phaser::RosCompileMessage ros_source;
  ros_source.x = 42;
  ros_source.name = "protobuf-to-ros";
  ros_source.names.push_back("one");
  ros_source.inners.Add()->id = 7;
  const std::string protobuf_ros_wire = ros_source.SerializeAsString();

  foo::bar::phaser::RosIntrinsicMessage intrinsic_source;
  intrinsic_source.stamp = ::ros::Time(12, 34);
  {
    auto header = intrinsic_source.header.Mutable();
    header.seq = 9;
    header.stamp = ::ros::Time(21, 654);
    header.frame_id = "wire-map";
  }
  std::string ros_wire;
  ASSERT_TRUE(intrinsic_source.SerializeToROSString(&ros_wire).ok());

  ExerciseResult warmup =
      ExerciseWireDeserialization(any_wire, protobuf_ros_wire, ros_wire);
  ASSERT_TRUE(warmup.values_match);
  ASSERT_TRUE(warmup.protobuf_serialized);
  ASSERT_TRUE(warmup.ros_serialized);

  g_allocation_count.store(0, std::memory_order_relaxed);
  g_count_allocations.store(true, std::memory_order_relaxed);
  ExerciseResult measured =
      ExerciseWireDeserialization(any_wire, protobuf_ros_wire, ros_wire);
  g_count_allocations.store(false, std::memory_order_relaxed);

  EXPECT_TRUE(measured.values_match);
  EXPECT_TRUE(measured.protobuf_serialized);
  EXPECT_TRUE(measured.ros_serialized);
  EXPECT_EQ(g_allocation_count.load(std::memory_order_relaxed), 0u);
}

TEST(OutputAllocationTest, RuntimeControlPreservesUserMetadataAndHandleLifetime) {
  alignas(std::max_align_t) std::array<std::byte, 32768> storage{};
  auto message = foo::bar::phaser::TestMessage::CreateMutable(
      storage.data(), storage.size());
  void* user_data = message.Allocate(sizeof(uint32_t));
  *static_cast<uint32_t*>(user_data) = 0x12345678;
  ASSERT_TRUE(message.SetUserMetadata(message.ToOffset(user_data)).ok());
  EXPECT_EQ(*static_cast<uint32_t*>(message.GetUserMetadata()), 0x12345678u);

  auto copy = message;
  auto moved = std::move(copy);
  moved.set_x(42);
  EXPECT_EQ(message.x(), 42);
  EXPECT_TRUE(moved.runtime->GetRuntimeControl() != nullptr);
}

TEST(OutputAllocationTest, MetadataEntriesAreReusedAndTypedAnyCanGrowTable) {
  alignas(std::max_align_t) std::array<std::byte, 32768> storage{};
  auto fixed = foo::bar::phaser::TestMessage::CreateMutable(
      storage.data(), storage.size());
  fixed.add_vm()->set_str("first");
  fixed.add_vm()->set_str("second");
  ASSERT_NE(fixed.runtime->GetRuntimeControl(), nullptr);
  EXPECT_EQ(fixed.runtime->GetRuntimeControl()->count, 2u);

  foo::bar::phaser::TestMessage dynamic;
  const uint32_t initial_capacity =
      dynamic.runtime->GetRuntimeControl()->capacity;
  dynamic.mutable_any()
      ->MutableAny<foo::bar::phaser::RosCompileMessage>()
      .name = "first-any-type";
  dynamic.mutable_any()
      ->MutableAny<foo::bar::phaser::RosIntrinsicMessage>()
      .name = "second-any-type";
  dynamic.mutable_any()
      ->MutableAny<foo::bar::phaser::HybridLookupMessage>()
      .set_dense_10(1);
  EXPECT_GT(dynamic.runtime->GetRuntimeControl()->capacity, initial_capacity);
}

TEST(OutputAllocationTest, LegacyMetadataSlotRemainsReadable) {
  alignas(std::max_align_t) std::array<std::byte, 32768> storage{};
  auto message = foo::bar::phaser::TestMessage::CreateMutable(
      storage.data(), storage.size());
  void* user_data = message.Allocate(sizeof(uint32_t));
  *static_cast<uint32_t*>(user_data) = 99;
  message.runtime->pb->metadata = message.ToOffset(user_data);
  EXPECT_EQ(*static_cast<uint32_t*>(message.GetUserMetadata()), 99u);
}

TEST(OutputAllocationTest, ReadonlyMutationIsRejected) {
  alignas(std::max_align_t) std::array<std::byte, 32768> storage{};
  auto mutable_message = foo::bar::phaser::TestMessage::CreateMutable(
      storage.data(), storage.size());
  mutable_message.set_x(7);
  auto readonly = foo::bar::phaser::TestMessage::CreateReadonly(
      mutable_message.Data(), mutable_message.Size());
  EXPECT_THROW(readonly.set_x(8), std::logic_error);
  EXPECT_EQ(readonly.x(), 7);
}

TEST(ReceiveAllocationTest, ReturnedViewsDependOnlyOnReceiveBuffer) {
  foo::bar::phaser::RosCompileMessage source;
  source.names.push_back("borrowed-name");
  source.inners.Add()->id = 23;
  std::vector<char> payload = CopyPayload(source);

  std::string_view borrowed_name;
  auto borrowed_inner = [&]() {
    auto root = foo::bar::phaser::RosCompileMessage::CreateReadonly(
        payload.data(), payload.size());
    borrowed_name = root.names[0];
    return root.inners[0];
  }();

  auto copied_inner = borrowed_inner;
  auto moved_inner = std::move(copied_inner);
  EXPECT_EQ(borrowed_name, "borrowed-name");
  EXPECT_EQ(borrowed_inner.id.Get(), 23);
  EXPECT_EQ(moved_inner.id.Get(), 23);
}
