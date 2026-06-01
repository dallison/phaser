// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include "absl/status/statusor.h"
#include <cstring>
#include <functional>
#include <string>

namespace phaser::test {

inline void StripProtobufDebugRedaction(std::string &s) {
  constexpr const char *kPrefixes[] = {"goo.gle/debugstr", "goo.gle/debugproto"};
  for (const char *prefix : kPrefixes) {
    const size_t len = std::strlen(prefix);
    if (s.compare(0, len, prefix) == 0) {
      const auto pos = s.find('\n');
      if (pos != std::string::npos) {
        s.erase(0, pos + 1);
      }
      return;
    }
  }
}

// Alloc succeeds until cumulative requested bytes exceed limit.
inline std::function<absl::StatusOr<void *>(size_t)>
AllocUntilLimit(size_t limit) {
  return [remaining = limit](size_t size) mutable -> absl::StatusOr<void *> {
    if (size > remaining) {
      return absl::ResourceExhaustedError("alloc limit exceeded");
    }
    remaining -= size;
    void *p = ::malloc(size);
    if (p == nullptr) {
      return absl::ResourceExhaustedError("malloc failed");
    }
    return p;
  };
}

inline std::function<absl::StatusOr<void *>(void *, size_t, size_t)>
ReallocAlwaysFails() {
  return [](void *, size_t, size_t) -> absl::StatusOr<void *> {
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

} // namespace phaser::test
