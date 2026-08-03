#pragma once

#include <cstdint>

namespace ros {

struct Time {
  Time() = default;
  Time(uint32_t seconds, uint32_t nanoseconds)
      : sec(seconds), nsec(nanoseconds) {}

  uint32_t sec = 0;
  uint32_t nsec = 0;

  bool operator==(const Time& other) const {
    return sec == other.sec && nsec == other.nsec;
  }
};

struct Duration {
  Duration() = default;
  Duration(int32_t seconds, int32_t nanoseconds)
      : sec(seconds), nsec(nanoseconds) {}

  int32_t sec = 0;
  int32_t nsec = 0;

  bool operator==(const Duration& other) const {
    return sec == other.sec && nsec == other.nsec;
  }
};

}  // namespace ros
