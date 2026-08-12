// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/message.h"

#include <cstring>

#include "toolbelt/hexdump.h"

namespace phaser {

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
