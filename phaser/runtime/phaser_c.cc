// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/phaser_c_handle.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

::phaser::AnyMessage* MutableAny(phaser_AnyMessage* msg) {
  auto* handle = ::phaser::capi::AsHandle<::phaser::AnyMessage>(msg);
  if (handle == nullptr) {
    return nullptr;
  }
  return handle->get();
}

const ::phaser::AnyMessage* ConstAny(const phaser_AnyMessage* msg) {
  auto* handle = ::phaser::capi::AsHandle<::phaser::AnyMessage>(msg);
  if (handle == nullptr) {
    return nullptr;
  }
  return handle->get();
}

uint8_t Flag(bool value) {
  return value ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);
}

}  // namespace

namespace phaser::capi {

PhaserBytes Copy(std::string_view bytes) {
  PhaserBytes out{nullptr, size_t{0}};
  if (bytes.empty()) {
    return out;
  }
  void* memory = std::malloc(bytes.size());
  if (memory == nullptr) {
    return out;
  }
  out.data = static_cast<uint8_t*>(memory);
  std::memcpy(out.data, bytes.data(), bytes.size());
  out.size = bytes.size();
  return out;
}

}  // namespace phaser::capi

extern "C" {

void phaser_free(void* p) { std::free(p); }

void phaser_AnyMessage_free(phaser_AnyMessage* msg) {
  ::phaser::capi::FreeHandle<::phaser::AnyMessage>(msg);
}

void phaser_AnyMessage_clear(phaser_AnyMessage* msg) {
  if (::phaser::AnyMessage* any = MutableAny(msg)) {
    any->Clear();
  }
}

void phaser_AnyMessage_set_type_url(phaser_AnyMessage* msg, const uint8_t* data,
                                    size_t len) {
  if (::phaser::AnyMessage* any = MutableAny(msg)) {
    any->set_type_url(::phaser::capi::Bytes(data, len));
  }
}

PhaserBytes phaser_AnyMessage_type_url(const phaser_AnyMessage* msg) {
  const ::phaser::AnyMessage* any = ConstAny(msg);
  if (any == nullptr) {
    return PhaserBytes{nullptr, size_t{0}};
  }
  return ::phaser::capi::Copy(any->type_url());
}

uint8_t phaser_AnyMessage_has_type_url(const phaser_AnyMessage* msg) {
  const ::phaser::AnyMessage* any = ConstAny(msg);
  return any == nullptr ? static_cast<uint8_t>(0) : Flag(any->has_type_url());
}

void phaser_AnyMessage_clear_type_url(phaser_AnyMessage* msg) {
  if (::phaser::AnyMessage* any = MutableAny(msg)) {
    any->clear_type_url();
  }
}

void phaser_AnyMessage_set_value(phaser_AnyMessage* msg, const uint8_t* data,
                                 size_t len) {
  if (::phaser::AnyMessage* any = MutableAny(msg)) {
    any->set_value(::phaser::capi::Bytes(data, len));
  }
}

PhaserBytes phaser_AnyMessage_value(const phaser_AnyMessage* msg) {
  const ::phaser::AnyMessage* any = ConstAny(msg);
  if (any == nullptr) {
    return PhaserBytes{nullptr, size_t{0}};
  }
  return ::phaser::capi::Copy(any->value());
}

uint8_t phaser_AnyMessage_has_value(const phaser_AnyMessage* msg) {
  const ::phaser::AnyMessage* any = ConstAny(msg);
  return any == nullptr ? static_cast<uint8_t>(0) : Flag(any->has_value());
}

void phaser_AnyMessage_clear_value(phaser_AnyMessage* msg) {
  if (::phaser::AnyMessage* any = MutableAny(msg)) {
    any->clear_value();
  }
}

PhaserBytes phaser_AnyMessage_serialize_as_string(const phaser_AnyMessage* msg) {
  const ::phaser::AnyMessage* any = ConstAny(msg);
  if (any == nullptr) {
    return PhaserBytes{nullptr, size_t{0}};
  }
  return ::phaser::capi::Copy(any->SerializeAsString());
}

uint8_t phaser_AnyMessage_parse_from_string(phaser_AnyMessage* msg,
                                            const uint8_t* data, size_t len) {
  ::phaser::AnyMessage* any = MutableAny(msg);
  if (any == nullptr) {
    return static_cast<uint8_t>(0);
  }
  std::string bytes;
  if (data != nullptr && len != 0) {
    bytes.assign(reinterpret_cast<const char*>(data), len);
  }
  return Flag(any->ParseFromString(bytes));
}

size_t phaser_AnyMessage_byte_size_long(const phaser_AnyMessage* msg) {
  const ::phaser::AnyMessage* any = ConstAny(msg);
  if (any == nullptr) {
    return 0;
  }
  return any->ZeroCopySize();
}

}  // extern "C"
