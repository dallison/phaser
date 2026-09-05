// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/message.h"

#include <cstring>

#include "toolbelt/hexdump.h"

namespace phaser {

ExternalBufferAllocator::ExternalBufferAllocator()
    : borrowed_resizer_{this, &ExternalBufferAllocator::Resize} {}

ExternalBufferAllocator::ExternalBufferAllocator(
    void* context, AllocateFunction allocate, ReallocateFunction reallocate)
    : context_(context),
      allocate_(allocate),
      reallocate_(reallocate),
      borrowed_resizer_{this, &ExternalBufferAllocator::Resize} {}

ExternalBufferAllocator::ExternalBufferAllocator(
    ExternalBufferAllocator&& other) noexcept
    : context_(other.context_),
      allocate_(other.allocate_),
      reallocate_(other.reallocate_),
      borrowed_resizer_{this, &ExternalBufferAllocator::Resize},
      buffer_(other.buffer_) {
  Rebind(buffer_);
  other.buffer_ = nullptr;
}

ExternalBufferAllocator& ExternalBufferAllocator::operator=(
    ExternalBufferAllocator&& other) noexcept {
  if (this != &other) {
    context_ = other.context_;
    allocate_ = other.allocate_;
    reallocate_ = other.reallocate_;
    borrowed_resizer_ = {this, &ExternalBufferAllocator::Resize};
    buffer_ = other.buffer_;
    Rebind(buffer_);
    other.buffer_ = nullptr;
  }
  return *this;
}

void ExternalBufferAllocator::Reset(void* context, AllocateFunction allocate,
                                    ReallocateFunction reallocate) {
  context_ = context;
  allocate_ = allocate;
  reallocate_ = reallocate;
  borrowed_resizer_ = {this, &ExternalBufferAllocator::Resize};
  buffer_ = nullptr;
}

absl::StatusOr<::toolbelt::PayloadBuffer*> ExternalBufferAllocator::NewBuffer(
    size_t initial_size, Tuning tuning) {
  if (allocate_ == nullptr || reallocate_ == nullptr) {
    return absl::FailedPreconditionError(
        "ExternalBufferAllocator has not been initialized");
  }
  absl::StatusOr<void*> buffer = allocate_(context_, initial_size);
  if (!buffer.ok()) {
    return buffer.status();
  }
  if (*buffer == nullptr) {
    return absl::ResourceExhaustedError(
        "External buffer allocator returned null");
  }
  memset(*buffer, 0, initial_size);
  buffer_ = new (*buffer)::toolbelt::PayloadBuffer(
      static_cast<uint32_t>(initial_size), &borrowed_resizer_,
      tuning == Tuning::kPerformance);
  return buffer_;
}

void ExternalBufferAllocator::Rebind(::toolbelt::PayloadBuffer* buffer) {
  buffer_ = buffer;
  if (buffer != nullptr) {
    buffer->SetBorrowedResizer(&borrowed_resizer_);
  }
}

void ExternalBufferAllocator::Resize(
    void* context, ::toolbelt::PayloadBuffer** buffer, size_t old_size,
    size_t new_size) {
  auto* allocator = static_cast<ExternalBufferAllocator*>(context);
  absl::StatusOr<void*> resized =
      allocator->reallocate_(allocator->context_, *buffer, old_size, new_size);
  if (!resized.ok()) {
    std::cerr << "Failed to resize externally backed PayloadBuffer from "
              << old_size << " to " << new_size << ": " << resized.status()
              << std::endl;
    abort();
  }
  if (*resized == nullptr) {
    std::cerr << "External buffer allocator returned null while resizing from "
              << old_size << " to " << new_size << std::endl;
    abort();
  }
  if (new_size > old_size) {
    memset(reinterpret_cast<char*>(*resized) + old_size, 0,
           new_size - old_size);
  }
  *buffer = reinterpret_cast<::toolbelt::PayloadBuffer*>(*resized);
  allocator->buffer_ = *buffer;
}

::toolbelt::PayloadBuffer* NewDynamicBuffer(size_t initial_size,
                                            Tuning tuning) {
  absl::StatusOr<::toolbelt::PayloadBuffer*> r = NewDynamicBuffer(
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

absl::StatusOr<::toolbelt::PayloadBuffer*> NewDynamicBuffer(
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
  ::toolbelt::PayloadBuffer* pb = new (*buffer)::toolbelt::PayloadBuffer(
      static_cast<uint32_t>(initial_size),
      [initial_size, realloc_fn = std::move(realloc)](
          ::toolbelt::PayloadBuffer** p, size_t old_size, size_t new_size) {
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
        *p = reinterpret_cast<::toolbelt::PayloadBuffer*>(*r);
      },
      tuning == Tuning::kPerformance);
  return pb;
}
}  // namespace phaser
