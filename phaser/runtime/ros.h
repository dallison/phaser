// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#pragma once

// ROS1 compatibility proxies for protobuf message fields. This header is only
// included by generated files that use a ROS intrinsic, so non-ROS Phaser
// users do not need ROS headers or libraries.

#include <ros/time.h>
#include <std_msgs/Header.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <utility>

#include "phaser/runtime/fields.h"

namespace phaser {
namespace internal {

struct RosTimeTraits {
  using RosType = ::ros::Time;

  template <typename Backend>
  static void Load(const Backend& backend, RosType& value) {
    value.sec = static_cast<uint32_t>(backend.seconds());
    value.nsec = static_cast<uint32_t>(backend.nanos());
  }

  template <typename Backend>
  static void Store(const RosType& value, Backend& backend) {
    backend.set_seconds(static_cast<int64_t>(value.sec));
    backend.set_nanos(static_cast<int32_t>(value.nsec));
  }

  static void Print(std::ostream& os, const RosType& value) {
    os << "sec: " << value.sec << " nsec: " << value.nsec;
  }
};

struct RosDurationTraits {
  using RosType = ::ros::Duration;

  template <typename Backend>
  static void Load(const Backend& backend, RosType& value) {
    value.sec = static_cast<int32_t>(backend.seconds());
    value.nsec = static_cast<int32_t>(backend.nanos());
  }

  template <typename Backend>
  static void Store(const RosType& value, Backend& backend) {
    backend.set_seconds(static_cast<int64_t>(value.sec));
    backend.set_nanos(static_cast<int32_t>(value.nsec));
  }

  static void Print(std::ostream& os, const RosType& value) {
    os << "sec: " << value.sec << " nsec: " << value.nsec;
  }
};

struct RosHeaderTraits {
  using RosType = ::std_msgs::Header;

  template <typename Backend>
  static void Load(const Backend& backend, RosType& value) {
    value.seq = static_cast<uint32_t>(backend.seq.Get());
    value.stamp = backend.stamp.Get();
    value.frame_id = std::string(backend.frame_id.Get());
  }

  template <typename Backend>
  static void Store(const RosType& value, Backend& backend) {
    backend.seq = value.seq;
    backend.stamp = value.stamp;
    backend.frame_id = value.frame_id;
    backend.SyncToPayload();
  }

  static void Print(std::ostream& os, const RosType& value) {
    os << "seq: " << value.seq << " stamp {";
    RosTimeTraits::Print(os, value.stamp);
    os << "} frame_id: \"" << value.frame_id << "\"";
  }
};

}  // namespace internal

template <typename Backend, typename Traits>
class RosMessageField : public IndirectMessageField<Backend> {
 public:
  using Base = IndirectMessageField<Backend>;
  using RosType = typename Traits::RosType;
  using Base::Base;

  RosMessageField() = default;
  RosMessageField(const RosMessageField&) = default;
  RosMessageField(RosMessageField&&) = default;

  RosMessageField& operator=(const RosMessageField& other) {
    if (this != &other) {
      Set(other.Get());
    }
    return *this;
  }

  RosMessageField& operator=(RosMessageField&& other) {
    if (this != &other) {
      Set(other.Get());
    }
    return *this;
  }

  RosMessageField& operator=(const RosType& value) {
    Set(value);
    return *this;
  }

  operator const RosType&() const { return Get(); }
  operator RosType&() { return MutableRos(); }

  const RosType& operator*() const { return Get(); }
  RosType& operator*() { return MutableRos(); }
  const RosType* operator->() const { return &Get(); }
  RosType* operator->() { return &MutableRos(); }

  const RosType& Get() const {
    LoadCache();
    return cache_;
  }

  RosType& MutableRos() {
    LoadCache();
    dirty_ = true;
    return cache_;
  }

  void Set(const RosType& value) {
    cache_ = value;
    cache_loaded_ = true;
    dirty_ = true;
  }

  bool IsPresent() const { return dirty_ || Base::IsPresent(); }

  void Clear() {
    Base::Clear();
    cache_ = RosType();
    cache_loaded_ = false;
    dirty_ = false;
  }

  void SyncToPayload() const {
    if (!dirty_) {
      if (Base::IsPresent()) {
        Base::Get().SyncToPayload();
      }
      return;
    }
    Backend* backend = const_cast<RosMessageField*>(this)->Base::Mutable();
    Traits::Store(cache_, *backend);
    dirty_ = false;
    cache_loaded_ = true;
  }

  size_t SerializedSize() const {
    SyncToPayload();
    return Base::SerializedSize();
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    SyncToPayload();
    return Base::Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::Status status = Base::Deserialize(buffer);
    if (status.ok()) {
      cache_loaded_ = false;
      dirty_ = false;
    }
    return status;
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  const RosMessageField& field) {
    Traits::Print(os, field.Get());
    return os;
  }

 private:
  void LoadCache() const {
    if (cache_loaded_ || dirty_) {
      return;
    }
    cache_ = RosType();
    if (Base::IsPresent()) {
      Traits::Load(Base::Get(), cache_);
    }
    cache_loaded_ = true;
  }

  mutable RosType cache_;
  mutable bool cache_loaded_ = false;
  mutable bool dirty_ = false;
};

template <typename Backend>
using RosTimeField = RosMessageField<Backend, internal::RosTimeTraits>;

template <typename Backend>
using RosDurationField =
    RosMessageField<Backend, internal::RosDurationTraits>;

template <typename Backend>
using RosHeaderField = RosMessageField<Backend, internal::RosHeaderTraits>;

}  // namespace phaser
