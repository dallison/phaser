// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#pragma once

// ROS1 compatibility proxies for protobuf message fields. This header is only
// included by generated files that use a ROS intrinsic, so non-ROS Phaser
// users do not need ROS headers or libraries.

#include <ros/time.h>
#include <std_msgs/Header.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phaser/runtime/arrays.h"
#include "phaser/runtime/fields.h"
#include "phaser/runtime/vectors.h"

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

// Presents a repeated Phaser message field as a sequence of ROS values, so a
// ROS `time[]` or `duration[]` reads and writes as `::ros::Time` rather than as
// the google.protobuf.Timestamp backing it. `Sequence` is
// MessageVectorField<Backend> for an unbounded field or
// MessageArrayField<Backend, N> for a fixed extent.
//
// Unlike the singular RosMessageField there is no cache: every element access
// converts through `Traits` against the payload, so a mutation is visible to
// the backing message immediately and SyncToPayload has nothing to reconcile.
template <typename Sequence, typename Traits>
class RosRepeatedMessageField : public Sequence {
 public:
  using RosType = typename Traits::RosType;
  using Sequence::Sequence;

  using value_type = RosType;
  using reference = RosType;
  using const_reference = RosType;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  // Lets `field[i] = value` work without handing out a reference into a
  // payload the element does not own.
  class Proxy {
   public:
    Proxy(RosRepeatedMessageField* field, size_t index)
        : field_(field), index_(index) {}

    operator RosType() const { return field_->Get(index_); }
    RosType Get() const { return field_->Get(index_); }

    Proxy& operator=(const RosType& value) {
      field_->Set(index_, value);
      return *this;
    }
    Proxy& operator=(const Proxy& other) {
      if (this != &other) {
        field_->Set(index_, other.Get());
      }
      return *this;
    }

    friend bool operator==(const Proxy& lhs, const RosType& rhs) {
      return lhs.Get() == rhs;
    }
    friend bool operator==(const RosType& lhs, const Proxy& rhs) {
      return lhs == rhs.Get();
    }
    friend bool operator!=(const Proxy& lhs, const RosType& rhs) {
      return !(lhs == rhs);
    }
    friend bool operator!=(const RosType& lhs, const Proxy& rhs) {
      return !(lhs == rhs);
    }
    friend std::ostream& operator<<(std::ostream& os, const Proxy& proxy) {
      Traits::Print(os, proxy.Get());
      return os;
    }

   private:
    RosRepeatedMessageField* field_;
    size_t index_;
  };

  class const_iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = RosType;
    using difference_type = ptrdiff_t;
    using pointer = void;
    using reference = RosType;

    const_iterator() = default;
    const_iterator(const RosRepeatedMessageField* field, size_t index)
        : field_(field), index_(index) {}

    RosType operator*() const { return field_->Get(index_); }
    const_iterator& operator++() {
      ++index_;
      return *this;
    }
    const_iterator operator++(int) {
      const_iterator result = *this;
      ++*this;
      return result;
    }
    const_iterator& operator--() {
      --index_;
      return *this;
    }
    const_iterator operator--(int) {
      const_iterator result = *this;
      --*this;
      return result;
    }
    bool operator==(const const_iterator& other) const {
      return field_ == other.field_ && index_ == other.index_;
    }
    bool operator!=(const const_iterator& other) const {
      return !(*this == other);
    }

   private:
    const RosRepeatedMessageField* field_ = nullptr;
    size_t index_ = 0;
  };
  using iterator = const_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, this->size()); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  // An out-of-range index needs no guard here: the sequence hands back a
  // default-constructed backend for one, which loads as a zero ROS value.
  RosType Get(size_t index) const {
    RosType value;
    Traits::Load(Sequence::Get(index), value);
    return value;
  }

  void Set(size_t index, const RosType& value) {
    auto backend = Sequence::Mutable(index);
    Traits::Store(value, backend);
  }

  void Add(const RosType& value) {
    auto backend = Sequence::Add();
    Traits::Store(value, backend);
  }

  void push_back(const RosType& value) { Add(value); }

  RosType operator[](size_t index) const { return Get(index); }
  Proxy operator[](size_t index) { return Proxy(this, index); }

  RosType front() const { return Get(0); }
  Proxy front() { return Proxy(this, 0); }
  RosType back() const { return Get(this->size() - 1); }
  Proxy back() { return Proxy(this, this->size() - 1); }

  std::vector<RosType> Get() const {
    std::vector<RosType> result;
    result.reserve(this->size());
    for (size_t i = 0; i < this->size(); ++i) {
      result.push_back(Get(i));
    }
    return result;
  }

  // Formats one element the way the singular field formats its value. Without
  // this a repeated element would stream through ROS's own operator<<, which
  // prints a different shape than the rest of the message.
  static std::ostream& PrintElement(std::ostream& os, const RosType& value) {
    Traits::Print(os, value);
    return os;
  }

  bool operator==(const RosRepeatedMessageField& other) const {
    if (this->size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < this->size(); ++i) {
      if (!(Get(i) == other.Get(i))) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const RosRepeatedMessageField& other) const {
    return !(*this == other);
  }
};

template <typename Backend>
using RosTimeVectorField =
    RosRepeatedMessageField<MessageVectorField<Backend>,
                            internal::RosTimeTraits>;

template <typename Backend>
using RosDurationVectorField =
    RosRepeatedMessageField<MessageVectorField<Backend>,
                            internal::RosDurationTraits>;

template <typename Backend, size_t N>
using RosTimeArrayField =
    RosRepeatedMessageField<MessageArrayField<Backend, N>,
                            internal::RosTimeTraits>;

template <typename Backend, size_t N>
using RosDurationArrayField =
    RosRepeatedMessageField<MessageArrayField<Backend, N>,
                            internal::RosDurationTraits>;

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
