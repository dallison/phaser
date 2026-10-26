// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Heap handle used by the C frontend. view_ is destroyed before the message
// objects it points into.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "phaser/runtime/any.h"
#include "phaser/runtime/phaser_c.h"

namespace phaser::capi {

struct NewMessage {};

template <typename Msg>
class Handle {
 public:
  explicit Handle(NewMessage /*tag*/, size_t initial_size)
      : owned_(std::in_place, initial_size) {
    view_ = &*owned_;
  }
  explicit Handle(Msg* view) : view_(view) {}
  explicit Handle(Msg owned) : owned_value_(std::move(owned)) {
    view_ = &*owned_value_;
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&&) = delete;
  Handle& operator=(Handle&&) = delete;

  Msg* get() const { return view_; }

 private:
  std::optional<Msg> owned_;
  std::optional<Msg> owned_value_;
  Msg* view_ = nullptr;
};

template <typename Msg, typename Opaque>
Handle<Msg>* AsHandle(Opaque* msg) {
  return reinterpret_cast<Handle<Msg>*>(msg);
}

template <typename Msg, typename Opaque>
const Handle<Msg>* AsHandle(const Opaque* msg) {
  return reinterpret_cast<const Handle<Msg>*>(msg);
}

template <typename Msg, typename Opaque>
Opaque* BoxNew(size_t initial_size) {
  try {
    return reinterpret_cast<Opaque*>(
        new Handle<Msg>(NewMessage{}, initial_size));
  } catch (...) {
    return nullptr;
  }
}

template <typename Msg, typename Opaque>
Opaque* BoxView(Msg* view) {
  if (view == nullptr) {
    return nullptr;
  }
  try {
    return reinterpret_cast<Opaque*>(new Handle<Msg>(view));
  } catch (...) {
    return nullptr;
  }
}

template <typename Msg, typename Opaque>
Opaque* BoxValue(Msg owned) {
  try {
    return reinterpret_cast<Opaque*>(new Handle<Msg>(std::move(owned)));
  } catch (...) {
    return nullptr;
  }
}

template <typename Msg, typename Opaque>
void FreeHandle(Opaque* msg) {
  delete AsHandle<Msg>(msg);
}

inline std::string_view Bytes(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return std::string_view();
  }
  return std::string_view(reinterpret_cast<const char*>(data), len);
}

PhaserBytes Copy(std::string_view bytes);

}  // namespace phaser::capi
