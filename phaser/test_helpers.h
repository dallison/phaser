// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <cstring>
#include <functional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace phaser::test {

inline void StripProtobufDebugRedaction(std::string& s) {
  // Recent protobuf versions prepend a non-deterministic redaction marker to
  // DebugString() output (e.g. "goo.gle/debugstr", "goo.gle/debugproto",
  // "goo.gle/debugonly") to discourage parsing the debug format. The marker is
  // emitted on its own leading line, sometimes preceded by a random amount of
  // leading whitespace, so we cannot assume it sits at offset 0. Find the
  // marker, confirm only whitespace precedes it on the first line, then drop
  // the whole marker line so comparisons are stable.
  constexpr std::string_view kMarker = "goo.gle/debug";
  const size_t marker = s.find(kMarker);
  if (marker == std::string::npos) {
    return;
  }
  for (size_t i = 0; i < marker; i++) {
    if (s[i] != ' ' && s[i] != '\t') {
      // Something other than whitespace precedes the marker, so it is not the
      // leading redaction prefix; leave the string untouched.
      return;
    }
  }
  const size_t newline = s.find('\n', marker);
  if (newline == std::string::npos) {
    s.clear();
  } else {
    s.erase(0, newline + 1);
  }
}

// Alloc succeeds until cumulative requested bytes exceed limit.
inline std::function<absl::StatusOr<void*>(size_t)> AllocUntilLimit(
    size_t limit) {
  return [remaining = limit](size_t size) mutable -> absl::StatusOr<void*> {
    if (size > remaining) {
      return absl::ResourceExhaustedError("alloc limit exceeded");
    }
    remaining -= size;
    void* p = ::malloc(size);
    if (p == nullptr) {
      return absl::ResourceExhaustedError("malloc failed");
    }
    return p;
  };
}

inline std::function<absl::StatusOr<void*>(void*, size_t, size_t)>
ReallocAlwaysFails() {
  return [](void*, size_t, size_t) -> absl::StatusOr<void*> {
    return absl::ResourceExhaustedError("realloc denied");
  };
}

inline std::string MakePatternString(size_t n, char fill = 'x') {
  return std::string(n, fill);
}

inline std::string MakePatternBytes(size_t n) {
  std::string s;
  s.reserve(n);
  for (size_t i = 0; i < n; i++) {
    s.push_back(static_cast<char>(i & 0xff));
  }
  return s;
}

}  // namespace phaser::test
