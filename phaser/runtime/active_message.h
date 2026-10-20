// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <any>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace phaser {

// The `active_message` member of a generated message: a value the
// application attaches to a handle, such as what keeps a received buffer
// alive.  It is used like a std::any and converts to one, for std::any_cast,
// but keeps it on the heap, allocated when it is first given a value.  A
// std::any member would pass the handle's address to an unknown function when
// destroyed, so the compiler could not keep any handle in registers.
//
// Initializing a std::any from an ActiveMessage stores the ActiveMessage.  To
// copy the value, convert it first: std::any(static_cast<const std::any&>(m)).
class ActiveMessage {
 public:
  ActiveMessage() = default;
  ActiveMessage(const ActiveMessage& other)
      : value_(other.has_value() ? std::make_unique<std::any>(*other.value_)
                                 : nullptr) {}
  ActiveMessage(ActiveMessage&& other) noexcept = default;
  ActiveMessage& operator=(const ActiveMessage& other) {
    if (this == &other) {
      return *this;
    }
    if (other.has_value()) {
      Value() = *other.value_;
    } else {
      reset();
    }
    return *this;
  }
  ActiveMessage& operator=(ActiveMessage&& other) noexcept = default;
  ~ActiveMessage() = default;

  // Stores 'value' as assigning it to a std::any would.
  template <typename T, typename = std::enable_if_t<
                            !std::is_same_v<std::decay_t<T>, ActiveMessage>>>
  ActiveMessage& operator=(T&& value) {
    Value() = std::forward<T>(value);
    return *this;
  }

  template <typename T, typename... Args>
  std::decay_t<T>& emplace(Args&&... args) {
    return Value().template emplace<T>(std::forward<Args>(args)...);
  }

  bool has_value() const noexcept {
    return value_ != nullptr && value_->has_value();
  }
  const std::type_info& type() const noexcept {
    return value_ == nullptr ? typeid(void) : value_->type();
  }
  void reset() noexcept { value_.reset(); }
  void swap(ActiveMessage& other) noexcept { value_.swap(other.value_); }

  operator std::any&() { return Value(); }
  operator const std::any&() const {
    return value_ == nullptr ? Empty() : *value_;
  }

 private:
  std::any& Value() {
    if (value_ == nullptr) {
      value_ = std::make_unique<std::any>();
    }
    return *value_;
  }
  static const std::any& Empty() {
    static const std::any empty;
    return empty;
  }

  std::unique_ptr<std::any> value_;
};

}  // namespace phaser
