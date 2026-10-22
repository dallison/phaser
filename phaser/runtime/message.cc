// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/message.h"

#include <cstring>
#include <limits>

#include "phaser/runtime/runtime.h"
#include "toolbelt/hexdump.h"

namespace phaser {

namespace {
// Keeping a bigger image would cost more memory than copying it saves time.
constexpr size_t kMaxNewBufferImageSize = 64 * 1024;
}  // namespace

NewBufferImage::NewBufferImage(Maker make) {
  for (size_t scratch_size = 4096; scratch_size <= 2 * kMaxNewBufferImageSize;
       scratch_size *= 2) {
    // Zeroed, so that the unused ends of small blocks are defined.
    auto scratch = std::make_unique<char[]>(scratch_size);
    ::phaser::PayloadBuffer* pb = make(scratch.get(), scratch_size, true);
    if (pb == nullptr) {
      continue;
    }
    MessageRuntime runtime(pb, true);
    runtime.MarkNewBuffer();
    if (runtime.ResetMark() == 0 || pb->hwm > kMaxNewBufferImageSize) {
      return;
    }
    size_ = pb->hwm;
    bytes_ = std::make_unique<char[]>(size_);
    memcpy(bytes_.get(), scratch.get(), size_);
    return;
  }
}

::phaser::PayloadBuffer* NewBufferImage::CopyTo(void* addr,
                                                size_t size) const {
  if (size_ == 0 || size < size_ ||
      size > std::numeric_limits<uint32_t>::max()) {
    return nullptr;
  }
  memcpy(addr, bytes_.get(), size_);
  auto* pb = static_cast<::phaser::PayloadBuffer*>(addr);
  pb->full_size = static_cast<uint32_t>(size);
  pb->AddressOf<::phaser::FreeBlockHeader>(pb->free_list)->length =
      static_cast<uint32_t>(size) - pb->free_list;
  return pb;
}

::phaser::PayloadBuffer* NewDynamicBuffer(size_t initial_size,
                                            Tuning tuning) {
  absl::StatusOr<::phaser::PayloadBuffer*> r = NewDynamicBuffer(
      initial_size, [](size_t size) -> void* { return ::malloc(size); },
      [](void* p, size_t /*old_size*/, size_t new_size) -> void* {
        return ::realloc(p, new_size);
      },
      tuning);
  if (!r.ok()) {
    std::cerr << "Failed to allocate PayloadBuffer of size " << initial_size
              << std::endl;
    abort();
  }
  return *r;
}

absl::StatusOr<::phaser::PayloadBuffer*> NewDynamicBuffer(
    size_t initial_size, std::function<absl::StatusOr<void*>(size_t)> alloc,
    std::function<absl::StatusOr<void*>(void*, size_t, size_t)> realloc,
    Tuning tuning) {
  absl::StatusOr<void*> buffer = alloc(initial_size);
  if (!buffer.ok()) {
    return buffer.status();
  }
  // Zero the freshly allocated buffer so that unused padding/free regions are
  // initialized. This avoids spurious "uninitialised value" reports from tools
  // like valgrind when the allocator scans or copies free space.
  memset(*buffer, 0, initial_size);
  ::phaser::PayloadBuffer* pb = new (*buffer)::phaser::PayloadBuffer(
      static_cast<uint32_t>(initial_size),
      [initial_size, realloc_fn = std::move(realloc)](
          ::phaser::PayloadBuffer** p, size_t old_size, size_t new_size) {
        absl::StatusOr<void*> r = realloc_fn(*p, old_size, new_size);
        if (!r.ok()) {
          std::cerr << "Failed to resize PayloadBuffer from " << initial_size
                    << " to " << new_size << std::endl;
          abort();
        }
        // Zero the newly grown region for the same reason as above.
        if (new_size > old_size) {
          memset(reinterpret_cast<char*>(*r) + old_size, 0,
                 new_size - old_size);
        }
        *p = reinterpret_cast<::phaser::PayloadBuffer*>(*r);
      },
      tuning == Tuning::kPerformance);
  return pb;
}

namespace internal {

absl::Status ReadNewString(::phaser::PayloadBuffer** pb,
                           ::phaser::BufferOffset header,
                           ::phaser::ROSReader& reader) {
  if (ABSL_PREDICT_FALSE(reader.Remaining() < sizeof(uint32_t))) {
    return reader.TruncatedError(sizeof(uint32_t));
  }
  const uint32_t length = reader.ReadUnchecked<uint32_t>();
  if (ABSL_PREDICT_FALSE(length > reader.Remaining())) {
    return reader.TruncatedStringError(length);
  }
  ::phaser::PayloadBuffer::NewString(pb, reader.ReadRawUnchecked(length),
                                     length, header);
  return absl::OkStatus();
}

absl::Status SerializeMessageVectorToROSSlow(
    ::phaser::ROSBuffer& buffer, size_t size,
    absl::FunctionRef<char*(char*, char*)> write) {
  if (absl::Status status = buffer.EnsureSpace(size); !status.ok()) {
    return status;
  }
  // A buffer with room for nothing may have no storage, which TryWrite()
  // needs.
  if (size == 0 || buffer.TryWrite(write)) {
    return absl::OkStatus();
  }
  // With room for it all, only a message ROS cannot hold, such as a
  // populated google.protobuf.Any, stops the write.
  return absl::UnimplementedError(
      "ROS serialization of a message in the vector is unsupported");
}

}  // namespace internal
}  // namespace phaser
