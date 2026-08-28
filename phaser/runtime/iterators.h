// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Array and vector iterators.

#include <stdint.h>
#include <stdlib.h>

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/message.h"
#include "toolbelt/payload_buffer.h"

namespace phaser {

template <typename Field, typename T>
struct FieldIterator {
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = std::remove_const_t<T>;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using reference = T&;

  FieldIterator(const Field* f, ::toolbelt::BufferOffset o, bool r = false)
      : field(f), offset(o), reverse(r) {}

  FieldIterator& operator++() {
    if (reverse) {
      offset -= static_cast<::toolbelt::BufferOffset>(sizeof(T));
    } else {
      offset += static_cast<::toolbelt::BufferOffset>(sizeof(T));
    }
    return *this;
  }
  FieldIterator& operator--() {
    if (reverse) {
      offset += static_cast<::toolbelt::BufferOffset>(sizeof(T));
    } else {
      offset -= static_cast<::toolbelt::BufferOffset>(sizeof(T));
    }
    return *this;
  }
  FieldIterator operator++(int) {
    FieldIterator result = *this;
    ++*this;
    return result;
  }
  FieldIterator operator--(int) {
    FieldIterator result = *this;
    --*this;
    return result;
  }
  FieldIterator operator+(size_t i) const {
    const auto byte_offset =
        static_cast<::toolbelt::BufferOffset>(i * sizeof(T));
    if (reverse) {
      return FieldIterator(field, offset - byte_offset, true);
    }
    return FieldIterator(field, offset + byte_offset);
  }
  FieldIterator operator-(size_t i) const {
    const auto byte_offset =
        static_cast<::toolbelt::BufferOffset>(i * sizeof(T));
    if (reverse) {
      return FieldIterator(field, offset + byte_offset, true);
    }
    return FieldIterator(field, offset - byte_offset);
  }
  T& operator*() const {
    T* addr = field->GetBuffer()->template ToAddress<T>(offset);
    return *addr;
  }

  bool operator==(const FieldIterator& it) const {
    return field == it.field && offset == it.offset;
  }
  bool operator!=(const FieldIterator& it) const { return !operator==(it); }

  const Field* field;
  ::toolbelt::BufferOffset offset;
  bool reverse;
};

template <typename Field>
struct StringFieldIterator {
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = std::string_view;
  using difference_type = ptrdiff_t;
  using pointer = void;
  using reference = std::string_view;

  StringFieldIterator(const Field* f, ::toolbelt::BufferOffset o,
                      bool r = false)
      : field(f), offset(o), reverse(r) {}

  StringFieldIterator& operator++() {
    if (reverse) {
      offset -= sizeof(::toolbelt::BufferOffset);
    } else {
      offset += sizeof(::toolbelt::BufferOffset);
    }
    return *this;
  }
  StringFieldIterator& operator--() {
    if (reverse) {
      offset += sizeof(::toolbelt::BufferOffset);
    } else {
      offset -= sizeof(::toolbelt::BufferOffset);
    }
    return *this;
  }
  StringFieldIterator operator++(int) {
    StringFieldIterator result = *this;
    ++*this;
    return result;
  }
  StringFieldIterator operator--(int) {
    StringFieldIterator result = *this;
    --*this;
    return result;
  }
  StringFieldIterator operator+(size_t i) const {
    if (reverse) {
      return StringFieldIterator(
          field, offset - i * sizeof(::toolbelt::BufferOffset), true);
    }
    return StringFieldIterator(field,
                               offset + i * sizeof(::toolbelt::BufferOffset));
  }
  StringFieldIterator operator-(size_t i) const {
    if (reverse) {
      return StringFieldIterator(
          field, offset + i * sizeof(::toolbelt::BufferOffset), true);
    }
    return StringFieldIterator(field,
                               offset - i * sizeof(::toolbelt::BufferOffset));
  }
  std::string_view operator*() const {
    return field->GetBuffer()->GetStringView(field->BaseOffset() + offset);
  }

  bool operator==(const StringFieldIterator& it) const {
    return field == it.field && offset == it.offset;
  }
  bool operator!=(const StringFieldIterator& it) const {
    return !operator==(it);
  }

  const Field* field;
  ::toolbelt::BufferOffset offset;
  bool reverse;
};

template <typename Field, typename T>
struct EnumFieldIterator {
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = std::remove_const_t<T>;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using reference = T&;

  EnumFieldIterator(const Field* f, ::toolbelt::BufferOffset o, bool r = false)
      : field(f), offset(o), reverse(r) {}

  EnumFieldIterator& operator++() {
    if (reverse) {
      offset -= static_cast<::toolbelt::BufferOffset>(sizeof(T));
    } else {
      offset += static_cast<::toolbelt::BufferOffset>(sizeof(T));
    }
    return *this;
  }
  EnumFieldIterator& operator--() {
    if (reverse) {
      offset += static_cast<::toolbelt::BufferOffset>(sizeof(T));
    } else {
      offset -= static_cast<::toolbelt::BufferOffset>(sizeof(T));
    }
    return *this;
  }
  EnumFieldIterator operator++(int) {
    EnumFieldIterator result = *this;
    ++*this;
    return result;
  }
  EnumFieldIterator operator--(int) {
    EnumFieldIterator result = *this;
    --*this;
    return result;
  }
  EnumFieldIterator operator+(size_t i) const {
    using Value = std::remove_const_t<T>;
    const auto byte_offset = static_cast<::toolbelt::BufferOffset>(
        i * sizeof(typename std::underlying_type<Value>::type));
    if (reverse) {
      return EnumFieldIterator(field, offset - byte_offset, true);
    }
    return EnumFieldIterator(field, offset + byte_offset);
  }
  EnumFieldIterator operator-(size_t i) const {
    using Value = std::remove_const_t<T>;
    const auto byte_offset = static_cast<::toolbelt::BufferOffset>(
        i * sizeof(typename std::underlying_type<Value>::type));
    if (reverse) {
      return EnumFieldIterator(field, offset + byte_offset, true);
    }
    return EnumFieldIterator(field, offset - byte_offset);
  }

  T& operator*() const {
    using U = typename std::underlying_type<std::remove_const_t<T>>::type;
    U* addr = field->GetBuffer()->template ToAddress<U>(offset);
    // An enum and its fixed underlying type share representation; route the
    // cast through void* so it is not flagged as a dereference of an unrelated
    // reinterpret_cast.
    return *static_cast<T*>(static_cast<void*>(addr));
  }

  bool operator==(const EnumFieldIterator& it) const {
    return field == it.field && offset == it.offset;
  }
  bool operator!=(const EnumFieldIterator& it) const { return !operator==(it); }

  const Field* field;
  ::toolbelt::BufferOffset offset;
  bool reverse;
};

}  // namespace phaser
