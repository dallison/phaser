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
#include <string_view>
#include <utility>

#include "phaser/runtime/fields.h"

namespace phaser {

struct RosHeaderView {
  uint32_t seq = 0;
  ::ros::Time stamp;
  std::string_view frame_id;

  ::std_msgs::Header ToOwned() const {
    ::std_msgs::Header result;
    result.seq = seq;
    result.stamp = stamp;
    if (!frame_id.empty()) {
      result.frame_id.assign(frame_id.data(), frame_id.size());
    }
    return result;
  }
};

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

  template <typename Backend>
  static RosHeaderView LoadView(const Backend& backend) {
    return {
        .seq = static_cast<uint32_t>(backend.seq.Get()),
        .stamp = backend.stamp.Get(),
        .frame_id = backend.frame_id.Get(),
    };
  }

  static void Print(std::ostream& os, RosHeaderView value) {
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

template <typename Owner>
class RosHeaderMutableView {
 public:
  class FrameIdProxy {
   public:
    explicit FrameIdProxy(Owner* owner) : owner_(owner) {}
    operator std::string_view() const { return owner_->Get().frame_id; }
    std::string_view Get() const { return owner_->Get().frame_id; }
    friend bool operator==(const FrameIdProxy& lhs, std::string_view rhs) {
      return lhs.Get() == rhs;
    }
    friend bool operator==(std::string_view lhs, const FrameIdProxy& rhs) {
      return lhs == rhs.Get();
    }
    friend bool operator!=(const FrameIdProxy& lhs, std::string_view rhs) {
      return !(lhs == rhs);
    }
    friend bool operator!=(std::string_view lhs, const FrameIdProxy& rhs) {
      return !(lhs == rhs);
    }
    template <typename String>
    FrameIdProxy& operator=(String value) {
      owner_->SetFrameId(value);
      return *this;
    }

   private:
    Owner* owner_;
  };

  explicit RosHeaderMutableView(Owner* owner)
      : owner_(owner),
        seq(owner->Get().seq),
        stamp(owner->Get().stamp),
        frame_id(owner) {}
  RosHeaderMutableView(const RosHeaderMutableView&) = delete;
  RosHeaderMutableView& operator=(const RosHeaderMutableView&) = delete;
  RosHeaderMutableView(RosHeaderMutableView&& other) noexcept
      : owner_(other.owner_),
        seq(other.seq),
        stamp(other.stamp),
        frame_id(owner_) {
    other.active_ = false;
  }
  ~RosHeaderMutableView() {
    if (active_) {
      owner_->CommitMutable(seq, stamp);
    }
  }

  RosHeaderView Get() const {
    return {.seq = seq, .stamp = stamp, .frame_id = frame_id.Get()};
  }
  ::std_msgs::Header ToOwned() const { return Get().ToOwned(); }

 private:
  Owner* owner_;
  bool active_ = true;

 public:
  uint32_t seq;
  ::ros::Time stamp;
  FrameIdProxy frame_id;
};

template <typename Backend>
class RosHeaderField : public IndirectMessageField<Backend> {
 public:
  using Base = IndirectMessageField<Backend>;
  using MutableView = RosHeaderMutableView<RosHeaderField<Backend>>;
  using Base::Base;

  struct ConstArrow {
    RosHeaderView view;
    const RosHeaderView* operator->() const { return &view; }
  };
  struct MutableArrow {
    MutableView view;
    MutableView* operator->() { return &view; }
  };

  RosHeaderField() = default;
  RosHeaderField(const RosHeaderField&) = default;
  RosHeaderField(RosHeaderField&&) = default;

  RosHeaderField& operator=(const RosHeaderField& other) {
    if (this != &other) {
      Set(other.Get());
    }
    return *this;
  }
  RosHeaderField& operator=(RosHeaderField&& other) {
    if (this != &other) {
      Set(other.Get());
    }
    return *this;
  }
  RosHeaderField& operator=(const ::std_msgs::Header& value) {
    Set(value);
    return *this;
  }

  operator RosHeaderView() const { return Get(); }
  RosHeaderView operator*() const { return Get(); }
  MutableView operator*() { return Mutable(); }
  ConstArrow operator->() const { return ConstArrow{Get()}; }
  MutableArrow operator->() { return MutableArrow{Mutable()}; }

  RosHeaderView Get() const {
    if (!Base::IsPresent()) {
      return {};
    }
    return internal::RosHeaderTraits::LoadView(Base::Get());
  }

  ::std_msgs::Header ToOwned() const { return Get().ToOwned(); }

  MutableView Mutable() {
    Base::Mutable();
    return MutableView(this);
  }

  template <typename String>
  void SetFrameId(String value) {
    Backend* backend = Base::Mutable();
    backend->frame_id = value;
  }

  void CommitMutable(uint32_t seq, const ::ros::Time& stamp) {
    Backend* backend = Base::Mutable();
    backend->seq = seq;
    backend->stamp = stamp;
    backend->SyncToPayload();
  }

  void Set(const ::std_msgs::Header& value) {
    auto backend = Mutable();
    backend.seq = value.seq;
    backend.stamp = value.stamp;
    backend.frame_id = value.frame_id;
  }
  void Set(RosHeaderView value) {
    auto backend = Mutable();
    backend.seq = value.seq;
    backend.stamp = value.stamp;
    backend.frame_id = value.frame_id;
  }

  bool IsPresent() const { return Base::IsPresent(); }

  void Clear() { Base::Clear(); }

  void SyncToPayload() const {
    if (Base::IsPresent()) {
      Base::Get().SyncToPayload();
    }
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
    return Base::Deserialize(buffer);
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  const RosHeaderField& field) {
    internal::RosHeaderTraits::Print(os, field.Get());
    return os;
  }
};

}  // namespace phaser
