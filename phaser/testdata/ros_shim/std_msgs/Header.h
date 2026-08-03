#pragma once

#include <cstdint>
#include <string>

#include <ros/time.h>

namespace std_msgs {

struct Header {
  uint32_t seq = 0;
  ::ros::Time stamp;
  std::string frame_id;

  bool operator==(const Header& other) const {
    return seq == other.seq && stamp == other.stamp &&
           frame_id == other.frame_id;
  }
};

}  // namespace std_msgs
