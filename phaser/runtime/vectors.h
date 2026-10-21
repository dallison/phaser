// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Vector fields (repeated fields).

#include <stdint.h>
#include <stdlib.h>

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/wireformat.h"
#include "phaser/runtime/payload_buffer.h"

namespace phaser {

class ProtoBuffer;

#define DECLARE_ZERO_COPY_VECTOR_BITS(vtype, itype, ctype, utype)              \
  using value_type = vtype;                                                    \
  using reference = value_type&;                                               \
  using const_reference = const value_type&;                                   \
  using pointer = value_type*;                                                 \
  using const_pointer = const value_type*;                                     \
  using size_type = size_t;                                                    \
  using difference_type = ptrdiff_t;                                           \
                                                                               \
  using iterator = itype;                                                      \
  using const_iterator = ctype;                                                \
  using reverse_iterator = std::reverse_iterator<iterator>;                    \
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;        \
                                                                               \
  iterator begin() { return iterator(this, BaseOffset()); }                    \
  iterator end() {                                                             \
    return iterator(this,                                                      \
                    BaseOffset() + static_cast<::phaser::BufferOffset>(      \
                                       NumElements() * sizeof(value_type)));   \
  }                                                                            \
  const_iterator begin() const { return const_iterator(this, BaseOffset()); }  \
  const_iterator end() const {                                                 \
    return const_iterator(                                                     \
        this, BaseOffset() + static_cast<::phaser::BufferOffset>(            \
                                 NumElements() * sizeof(value_type)));         \
  }                                                                            \
  const_iterator cbegin() const { return const_iterator(this, BaseOffset()); } \
  const_iterator cend() const {                                                \
    return const_iterator(                                                     \
        this, BaseOffset() + static_cast<::phaser::BufferOffset>(            \
                                 NumElements() * sizeof(value_type)));         \
  }                                                                            \
                                                                               \
  reverse_iterator rbegin() { return reverse_iterator(end()); }                \
  reverse_iterator rend() { return reverse_iterator(begin()); }                \
  const_reverse_iterator rbegin() const {                                      \
    return const_reverse_iterator(end());                                      \
  }                                                                            \
  const_reverse_iterator rend() const {                                        \
    return const_reverse_iterator(begin());                                    \
  }                                                                            \
  const_reverse_iterator crbegin() const {                                     \
    return const_reverse_iterator(cend());                                     \
  }                                                                            \
  const_reverse_iterator crend() const {                                       \
    return const_reverse_iterator(cbegin());                                   \
  }

// vtype: value type
// rtype: relay type (like std::array<T,N>)
// relay: member to relay through
#define DECLARE_RELAY_VECTOR_BITS(vtype, rtype, relay)                   \
  using value_type = vtype;                                              \
  using reference = value_type&;                                         \
  using const_reference = value_type&;                                   \
  using pointer = value_type*;                                           \
  using const_pointer = const value_type*;                               \
  using size_type = size_t;                                              \
  using difference_type = ptrdiff_t;                                     \
                                                                         \
  using iterator = typename rtype::iterator;                             \
  using const_iterator = typename rtype::const_iterator;                 \
  using reverse_iterator = typename rtype::reverse_iterator;             \
  using const_reverse_iterator = typename rtype::const_reverse_iterator; \
                                                                         \
  iterator begin() { return relay.begin(); }                             \
  iterator end() { return relay.end(); }                                 \
  reverse_iterator rbegin() { return relay.rbegin(); }                   \
  reverse_iterator rend() { return relay.rend(); }                       \
  const_iterator begin() const { return relay.begin(); }                 \
  const_iterator end() const { return relay.end(); }                     \
  const_iterator cbegin() const { return relay.begin(); }                \
  const_iterator cend() const { return relay.end(); }                    \
  const_reverse_iterator rbegin() const { return relay.rbegin(); }       \
  const_reverse_iterator rend() const { return relay.rend(); }           \
  const_reverse_iterator crbegin() const { return relay.crbegin(); }     \
  const_reverse_iterator crend() const { return relay.crend(); }

// This is a variable length vector of T.  It looks like a std::vector<T>.
// The binary message contains a ::phaser::VectorHeader at the binary offset.
// This contains the number of elements and the base offset for the data.
template <typename T, bool FixedSize = false, bool Signed = false,
          bool Packed = true, typename Loc = StoredFieldLoc>
class PrimitiveVectorField : public FieldBase<Loc> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  PrimitiveVectorField() = default;
  using FieldBase<Loc>::FieldBase;
  PrimitiveVectorField(const PrimitiveVectorField&) = default;
  PrimitiveVectorField(PrimitiveVectorField&&) = default;

  T& operator[](int index) {
    static T empty;
    empty = T();
    if (index < 0 || static_cast<size_t>(index) >= size()) {
      return empty;
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return empty;
    }
    return base[index];
  }

  T operator[](int index) const {
    if (index < 0 || static_cast<size_t>(index) >= size()) {
      return T();
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return T();
    }
    return base[index];
  }

  T& at(size_t index) {
    if (index >= size()) {
      throw std::out_of_range("PrimitiveVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }
  T at(size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("PrimitiveVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }

  T& front() { return (*this)[0]; }
  const T front() const { return (*this)[0]; }
  T& back() { return (*this)[static_cast<int>(size()) - 1]; }
  const T back() const { return (*this)[static_cast<int>(size()) - 1]; }

  T Get(size_t index) const { return (*this)[static_cast<int>(index)]; }

  void Set(size_t index, T v) {
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return;
    }
    base[index] = v;
  }

  std::vector<T> Get() const {
    std::vector<T> v;
    size_t n = size();
    for (size_t i = 0; i < n; i++) {
      v.push_back((*this)[static_cast<int>(i)]);
    }
    return v;
  }

  void Add(T v) { push_back(v); }

#define ITYPE FieldIterator<PrimitiveVectorField, value_type>
#define CTYPE FieldIterator<PrimitiveVectorField, const value_type>
  DECLARE_ZERO_COPY_VECTOR_BITS(T, ITYPE, CTYPE, T)
#undef ITYPE
#undef CTYPE

  void push_back(const T& v) {
    ::phaser::PayloadBuffer::VectorPush<T>(GetBufferAddr(), Header(Offset()),
                                           v);
  }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    push_back(T(std::forward<Args>(args)...));
    return (*this)[static_cast<int>(size() - 1)];
  }

  void pop_back() {
    if (!empty()) {
      resize(size() - 1);
    }
  }

  void reserve(size_t n) {
    ::phaser::PayloadBuffer::VectorReserve<T>(GetBufferAddr(), Header(Offset()),
                                              n);
  }

  void resize(size_t n) {
    ::phaser::PayloadBuffer::VectorResize<T>(GetBufferAddr(), Header(Offset()),
                                             n);
  }
  void resize(size_t n, const T& value) {
    const T value_copy = value;
    const size_t old_size = size();
    resize(n);
    for (size_t i = old_size; i < n; ++i) {
      Set(i, value_copy);
    }
  }

  void assign(size_t count, const T& value) {
    const T value_copy = value;
    Clear();
    resize(count, value_copy);
  }
  template <typename InputIterator,
            std::enable_if_t<!std::is_integral_v<InputIterator>, int> = 0>
  void assign(InputIterator first, InputIterator last) {
    Clear();
    for (; first != last; ++first) {
      push_back(*first);
    }
  }
  void assign(std::initializer_list<T> values) {
    assign(values.begin(), values.end());
  }

  template <typename Iterator>
  iterator insert(Iterator pos, const T& value) {
    const size_t index = static_cast<size_t>((pos.offset - BaseOffset()) /
                                             static_cast<ptrdiff_t>(sizeof(T)));
    std::vector<T> values = Get();
    values.insert(values.begin() + static_cast<ptrdiff_t>(index), value);
    assign(values.begin(), values.end());
    return begin() + index;
  }
  template <typename Iterator, typename... Args>
  iterator emplace(Iterator pos, Args&&... args) {
    return insert(pos, T(std::forward<Args>(args)...));
  }
  template <typename Iterator>
  iterator erase(Iterator pos) {
    return erase(pos, pos + 1);
  }
  template <typename Iterator>
  iterator erase(Iterator first, Iterator last) {
    const size_t first_index = static_cast<size_t>(
        (first.offset - BaseOffset()) / static_cast<ptrdiff_t>(sizeof(T)));
    const size_t last_index = static_cast<size_t>(
        (last.offset - BaseOffset()) / static_cast<ptrdiff_t>(sizeof(T)));
    std::vector<T> values = Get();
    values.erase(values.begin() + static_cast<ptrdiff_t>(first_index),
                 values.begin() + static_cast<ptrdiff_t>(last_index));
    assign(values.begin(), values.end());
    return begin() + first_index;
  }
  void swap(PrimitiveVectorField& other) {
    if (this == &other) {
      return;
    }
    const std::vector<T> mine = Get();
    const std::vector<T> theirs = other.Get();
    *this = theirs;
    other = mine;
  }

  void Clear() {
    ::phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(), Header(Offset()));
  }
  void clear() { Clear(); }  // STL compatibility.

  PrimitiveVectorField& operator=(const PrimitiveVectorField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  PrimitiveVectorField& operator=(
      const PrimitiveVectorField<T, FixedSize, Signed, Packed, OtherLoc>&
          other) {
    Assign(other);
    return *this;
  }
  PrimitiveVectorField& operator=(PrimitiveVectorField&& other) noexcept {
    return operator=(static_cast<const PrimitiveVectorField&>(other));
  }
  PrimitiveVectorField& operator=(std::initializer_list<T> values) {
    assign(values);
    return *this;
  }
  template <typename Allocator>
  PrimitiveVectorField& operator=(const std::vector<T, Allocator>& values) {
    assign(values.begin(), values.end());
    return *this;
  }

  size_t size() const { return NumElements(); }
  T* data() { return GetRuntime()->template ToAddress<T>(BaseOffset()); }
  const T* data() const {
    // A default message has no runtime.
    const RuntimeHandle& runtime = GetRuntime();
    return runtime == nullptr
               ? nullptr
               : runtime->template ToAddress<const T>(BaseOffset());
  }
  size_t Size() const { return NumElements(); }

  absl::Span<T> AsMutableSpan() {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr) {
      return absl::Span<T>();
    }
    T* base = GetRuntime()->template ToAddress<T>(hdr->data);
    if (base == nullptr) {
      return absl::Span<T>();
    }
    const size_t n = GetRuntime()->ClampElementCount(
        hdr->data, hdr->num_elements, sizeof(T));
    return absl::Span<T>(base, n);
  }

  absl::Span<const T> AsSpan() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return absl::Span<T>();
    }
    ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return absl::Span<const T>();
    }
    const T* base = GetRuntime()->template ToAddress<const T>(hdr->data);
    if (base == nullptr) {
      return absl::Span<const T>();
    }
    const size_t n = GetRuntime()->ClampElementCount(
        hdr->data, hdr->num_elements, sizeof(T));
    return absl::Span<const T>(base, n);
  }

  bool empty() const { return size() == 0; }

  size_t capacity() const {
    return GetRuntime()->AllocatedCapacity(BaseOffset(), sizeof(value_type));
  }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  template <typename OtherLoc>
  bool operator==(const PrimitiveVectorField<T, FixedSize, Signed, Packed,
                                             OtherLoc>& other) const {
    const size_t n = size();
    if (n != other.size()) {
      return false;
    }
    for (size_t i = 0; i < n; i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const PrimitiveVectorField<T, FixedSize, Signed, Packed,
                                             OtherLoc>& other) const {
    return !(*this == other);
  }
  template <typename Allocator>
  bool operator==(const std::vector<T, Allocator>& other) const {
    return size() == other.size() && std::equal(begin(), end(), other.begin());
  }
  template <typename Allocator>
  bool operator!=(const std::vector<T, Allocator>& other) const {
    return !(*this == other);
  }
  template <typename Allocator>
  friend bool operator==(const std::vector<T, Allocator>& lhs,
                         const PrimitiveVectorField& rhs) {
    return rhs == lhs;
  }
  template <typename Allocator>
  friend bool operator!=(const std::vector<T, Allocator>& lhs,
                         const PrimitiveVectorField& rhs) {
    return !(rhs == lhs);
  }

  size_t SerializedSize() const {
    size_t sz = size();
    if (sz == 0) {
      return 0;
    }
    size_t length = 0;

    // Packed is default in proto3 but optional in proto2.
    if constexpr (Packed) {
      if constexpr (FixedSize) {
        return ProtoBuffer::LengthDelimitedSize(Number(), sz * sizeof(T));
      } else {
        T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
        if (base == nullptr) {
          return 0;
        }
        for (size_t i = 0; i < sz; i++) {
          length += ProtoBuffer::VarintSize<T, Signed>(base[i]);
        }
        return ProtoBuffer::LengthDelimitedSize(Number(), length);
      }
    }

    // Not packed, just a sequence of individual fields, all with the same
    // tag.
    if constexpr (FixedSize) {
      length += sz * (ProtoBuffer::TagSize(Number(),
                                           ProtoBuffer::FixedWireType<T>()) +
                      sizeof(T));
    } else {
      T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
      if (base == nullptr) {
        return 0;
      }
      for (size_t i = 0; i < sz; i++) {
        length += ProtoBuffer::TagSize(Number(), WireType::kVarint) +
                  ProtoBuffer::VarintSize<T, Signed>(base[i]);
      }
    }

    return ProtoBuffer::LengthDelimitedSize(Number(), length);
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return absl::OkStatus();
    }
    // Packed is default in proto3 but optional in proto2.
    if constexpr (Packed) {
      if constexpr (FixedSize) {
        return buffer.SerializeLengthDelimited(
            Number(), reinterpret_cast<const char*>(base), sz * sizeof(T));
      } else {
        size_t length = 0;
        for (size_t i = 0; i < sz; i++) {
          length += ProtoBuffer::VarintSize<T, Signed>(base[i]);
        }

        if (absl::Status status =
                buffer.SerializeLengthDelimitedHeader(Number(), length);
            !status.ok()) {
          return status;
        }

        for (size_t i = 0; i < sz; i++) {
          if (absl::Status status =
                  buffer.SerializeRawVarint<T, Signed>(base[i]);
              !status.ok()) {
            return status;
          }
        }
        return absl::OkStatus();
      }
    }

    // Not packed, just a sequence of individual fields, all with the same
    // tag.
    if constexpr (FixedSize) {
      for (size_t i = 0; i < sz; i++) {
        if (absl::Status status = buffer.SerializeFixed<T>(Number(), base[i]);
            !status.ok()) {
          return status;
        }
      }
    } else {
      for (size_t i = 0; i < sz; i++) {
        if (absl::Status status =
                buffer.SerializeVarint<T, Signed>(Number(), base[i]);
            !status.ok()) {
          return status;
        }
      }
    }

    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    if constexpr (Packed) {
      absl::StatusOr<absl::Span<char>> data =
          buffer.DeserializeLengthDelimited();
      if (!data.ok()) {
        return data.status();
      }
      if constexpr (FixedSize) {
        if (data->size() % sizeof(T) != 0) {
          return absl::InvalidArgumentError(
              "Packed fixed-width field has a partial element");
        }
        resize(data->size() / sizeof(T));
        if (data->empty()) {
          return absl::OkStatus();
        }
        T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
        memcpy(base, data->data(), data->size());
        return absl::OkStatus();
      } else {
        ProtoBuffer sub_buffer(*data);
        while (!sub_buffer.Eof()) {
          absl::StatusOr<T> v = sub_buffer.DeserializeVarint<T, Signed>();
          if (!v.ok()) {
            return v.status();
          }
          push_back(*v);
        }
      }
    } else {
      if constexpr (FixedSize) {
        absl::StatusOr<T> v = buffer.DeserializeFixed<T>();
        if (!v.ok()) {
          return v.status();
        }
        push_back(*v);
      } else {
        absl::StatusOr<T> v = buffer.DeserializeVarint<T, Signed>();
        if (!v.ok()) {
          return v.status();
        }
        push_back(*v);
      }
    }
    return absl::OkStatus();
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      push_back(other[static_cast<int>(i)]);
    }
  }

  friend FieldIterator<PrimitiveVectorField, T>;
  friend FieldIterator<PrimitiveVectorField, const T>;
  ::phaser::VectorHeader* Header(uint32_t offset) const {
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + offset);
  }

  ::phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->ClampElementCount(hdr->data, hdr->num_elements,
                                           sizeof(T));
  }
};

template <typename Enum = int, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser, bool Packed = true,
          typename Loc = StoredFieldLoc>
class EnumVectorField : public FieldBase<Loc> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  EnumVectorField() = default;
  using FieldBase<Loc>::FieldBase;
  EnumVectorField(const EnumVectorField&) = default;
  EnumVectorField(EnumVectorField&&) = default;

  using T = typename std::underlying_type<Enum>::type;

  Enum& operator[](int index) {
    static Enum empty = static_cast<Enum>(0);
    empty = static_cast<Enum>(0);
    if (index < 0 || static_cast<size_t>(index) >= size()) {
      return empty;
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return empty;
    }
    return *reinterpret_cast<Enum*>(&base[index]);
  }

  const Enum operator[](int index) const {
    if (index < 0 || static_cast<size_t>(index) >= size()) {
      return static_cast<Enum>(0);
    }
    const T* base = GetRuntime()->template ToAddress<const T>(BaseOffset());
    if (base == nullptr) {
      return static_cast<Enum>(0);
    }
    return *reinterpret_cast<const Enum*>(&base[index]);
  }

  Enum& at(size_t index) {
    if (index >= size()) {
      throw std::out_of_range("EnumVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }
  Enum at(size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("EnumVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }

  Enum& front() { return (*this)[0]; }
  const Enum front() const { return (*this)[0]; }
  Enum& back() { return (*this)[static_cast<int>(size()) - 1]; }
  const Enum back() const { return (*this)[static_cast<int>(size()) - 1]; }

  std::vector<Enum> Get() const {
    const size_t n = size();
    std::vector<Enum> r;
    r.reserve(n);
    for (size_t i = 0; i < n; i++) {
      r.push_back((*this)[static_cast<int>(i)]);
    }
    return r;
  }

  Enum Get(size_t index) const { return (*this)[static_cast<int>(index)]; }

  void Set(size_t index, Enum v) {
    Enum* base = GetRuntime()->template ToAddress<Enum>(BaseOffset());
    if (base == nullptr) {
      return;
    }
    base[index] = v;
  }
#define ITYPE EnumFieldIterator<EnumVectorField, value_type>
#define CTYPE EnumFieldIterator<EnumVectorField, const value_type>
  DECLARE_ZERO_COPY_VECTOR_BITS(Enum, ITYPE, CTYPE, T)
#undef ITYPE
#undef CTYPE

  absl::Span<Enum> AsMutableSpan() {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr) {
      return absl::Span<Enum>();
    }
    Enum* base = GetRuntime()->template ToAddress<Enum>(hdr->data);
    if (base == nullptr) {
      return absl::Span<Enum>();
    }
    const size_t n = GetRuntime()->ClampElementCount(
        hdr->data, hdr->num_elements, sizeof(T));
    return absl::Span<Enum>(base, n);
  }

  absl::Span<const Enum> AsSpan() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return absl::Span<const Enum>();
    }
    ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return absl::Span<const Enum>();
    }
    const Enum* base = GetRuntime()->template ToAddress<const Enum>(hdr->data);
    if (base == nullptr) {
      return absl::Span<const Enum>();
    }
    const size_t n = GetRuntime()->ClampElementCount(
        hdr->data, hdr->num_elements, sizeof(T));
    return absl::Span<const Enum>(base, n);
  }

  void push_back(const Enum& v) {
    ::phaser::PayloadBuffer::VectorPush<T>(GetBufferAddr(), Header(Offset()),
                                           static_cast<T>(v));
  }

  template <typename... Args>
  Enum& emplace_back(Args&&... args) {
    push_back(Enum(std::forward<Args>(args)...));
    return (*this)[static_cast<int>(size() - 1)];
  }

  void pop_back() {
    if (!empty()) {
      resize(size() - 1);
    }
  }

  void reserve(size_t n) {
    ::phaser::PayloadBuffer::VectorReserve<T>(GetBufferAddr(), Header(Offset()),
                                              n);
  }

  void resize(size_t n) {
    ::phaser::PayloadBuffer::VectorResize<T>(GetBufferAddr(), Header(Offset()),
                                             n);
  }
  void resize(size_t n, Enum value) {
    const size_t old_size = size();
    resize(n);
    for (size_t i = old_size; i < n; ++i) {
      Set(i, value);
    }
  }

  void Add(Enum v) { push_back(v); }

  void assign(size_t count, Enum value) {
    Clear();
    resize(count, value);
  }
  template <typename InputIterator,
            std::enable_if_t<!std::is_integral_v<InputIterator>, int> = 0>
  void assign(InputIterator first, InputIterator last) {
    Clear();
    for (; first != last; ++first) {
      push_back(*first);
    }
  }
  void assign(std::initializer_list<Enum> values) {
    assign(values.begin(), values.end());
  }

  template <typename Iterator>
  iterator insert(Iterator pos, Enum value) {
    const size_t index = static_cast<size_t>((pos.offset - BaseOffset()) /
                                             static_cast<ptrdiff_t>(sizeof(T)));
    std::vector<Enum> values = Get();
    values.insert(values.begin() + static_cast<ptrdiff_t>(index), value);
    assign(values.begin(), values.end());
    return begin() + index;
  }
  template <typename Iterator, typename... Args>
  iterator emplace(Iterator pos, Args&&... args) {
    return insert(pos, Enum(std::forward<Args>(args)...));
  }
  template <typename Iterator>
  iterator erase(Iterator pos) {
    return erase(pos, pos + 1);
  }
  template <typename Iterator>
  iterator erase(Iterator first, Iterator last) {
    const size_t first_index = static_cast<size_t>(
        (first.offset - BaseOffset()) / static_cast<ptrdiff_t>(sizeof(T)));
    const size_t last_index = static_cast<size_t>(
        (last.offset - BaseOffset()) / static_cast<ptrdiff_t>(sizeof(T)));
    std::vector<Enum> values = Get();
    values.erase(values.begin() + static_cast<ptrdiff_t>(first_index),
                 values.begin() + static_cast<ptrdiff_t>(last_index));
    assign(values.begin(), values.end());
    return begin() + first_index;
  }
  void swap(EnumVectorField& other) {
    if (this == &other) {
      return;
    }
    const std::vector<Enum> mine = Get();
    const std::vector<Enum> theirs = other.Get();
    *this = theirs;
    other = mine;
  }

  void Clear() {
    ::phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(), Header(Offset()));
  }
  void clear() { Clear(); }  // STL compatibility.

  EnumVectorField& operator=(const EnumVectorField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  EnumVectorField& operator=(const EnumVectorField<Enum, Stringizer, Parser,
                                                   Packed, OtherLoc>& other) {
    Assign(other);
    return *this;
  }
  EnumVectorField& operator=(EnumVectorField&& other) noexcept {
    return operator=(static_cast<const EnumVectorField&>(other));
  }
  EnumVectorField& operator=(std::initializer_list<Enum> values) {
    assign(values);
    return *this;
  }
  template <typename Allocator>
  EnumVectorField& operator=(const std::vector<Enum, Allocator>& values) {
    assign(values.begin(), values.end());
    return *this;
  }

  size_t size() const { return NumElements(); }
  Enum* data() { return GetRuntime()->template ToAddress<Enum>(BaseOffset()); }
  const Enum* data() const {
    // A default message has no runtime.
    const RuntimeHandle& runtime = GetRuntime();
    return runtime == nullptr
               ? nullptr
               : runtime->template ToAddress<const Enum>(BaseOffset());
  }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  size_t capacity() const {
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->AllocatedCapacity(hdr->data, sizeof(T));
  }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  template <typename OtherLoc>
  bool operator==(const EnumVectorField<Enum, Stringizer, Parser, Packed,
                                        OtherLoc>& other) const {
    const size_t n = size();
    if (n != other.size()) {
      return false;
    }
    for (size_t i = 0; i < n; i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const EnumVectorField<Enum, Stringizer, Parser, Packed,
                                        OtherLoc>& other) const {
    return !(*this == other);
  }
  template <typename Allocator>
  bool operator==(const std::vector<Enum, Allocator>& other) const {
    return size() == other.size() && std::equal(begin(), end(), other.begin());
  }
  template <typename Allocator>
  bool operator!=(const std::vector<Enum, Allocator>& other) const {
    return !(*this == other);
  }
  template <typename Allocator>
  friend bool operator==(const std::vector<Enum, Allocator>& lhs,
                         const EnumVectorField& rhs) {
    return rhs == lhs;
  }
  template <typename Allocator>
  friend bool operator!=(const std::vector<Enum, Allocator>& lhs,
                         const EnumVectorField& rhs) {
    return !(rhs == lhs);
  }

  size_t SerializedSize() const {
    size_t sz = size();
    if (sz == 0) {
      return 0;
    }
    size_t length = 0;

    // Packed is default in proto3 but optional in proto2.
    if constexpr (Packed) {
      T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
      if (base == nullptr) {
        return 0;
      }
      for (size_t i = 0; i < sz; i++) {
        length += ProtoBuffer::VarintSize<T, false>(base[i]);
      }
      return ProtoBuffer::LengthDelimitedSize(Number(), length);
    }

    // Not packed, just a sequence of individual fields, all with the same
    // tag.
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return 0;
    }
    for (size_t i = 0; i < sz; i++) {
      length += ProtoBuffer::TagSize(Number(), WireType::kVarint) +
                ProtoBuffer::VarintSize<T, false>(base[i]);
    }

    return ProtoBuffer::LengthDelimitedSize(Number(), length);
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return absl::OkStatus();
    }
    // Packed is default in proto3 but optional in proto2.
    if constexpr (Packed) {
      size_t length = 0;
      for (size_t i = 0; i < sz; i++) {
        length += ProtoBuffer::VarintSize<T, false>(base[i]);
      }

      if (absl::Status status =
              buffer.SerializeLengthDelimitedHeader(Number(), length);
          !status.ok()) {
        return status;
      }

      for (size_t i = 0; i < sz; i++) {
        if (absl::Status status = buffer.SerializeRawVarint<T, false>(base[i]);
            !status.ok()) {
          return status;
        }
      }
      return absl::OkStatus();
    }

    // Not packed, just a sequence of individual fields, all with the same
    // tag.

    for (size_t i = 0; i < sz; i++) {
      if (absl::Status status =
              buffer.SerializeVarint<T, false>(Number(), base[i]);
          !status.ok()) {
        return status;
      }
    }

    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    if constexpr (Packed) {
      absl::StatusOr<absl::Span<char>> data =
          buffer.DeserializeLengthDelimited();
      if (!data.ok()) {
        return data.status();
      }
      ProtoBuffer sub_buffer(*data);
      while (!sub_buffer.Eof()) {
        absl::StatusOr<T> v = sub_buffer.DeserializeVarint<T, false>();
        if (!v.ok()) {
          return v.status();
        }
        push_back(static_cast<Enum>(*v));
      }
    } else {
      absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
      if (!v.ok()) {
        return v.status();
      }
      push_back(static_cast<Enum>(*v));
    }
    return absl::OkStatus();
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      push_back(other[static_cast<int>(i)]);
    }
  }

  friend EnumFieldIterator<EnumVectorField, Enum>;
  friend EnumFieldIterator<EnumVectorField, const Enum>;
  ::phaser::VectorHeader* Header(::phaser::BufferOffset offset) const {
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + offset);
  }

  ::phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->ClampElementCount(hdr->data, hdr->num_elements,
                                           sizeof(T));
  }
};

namespace internal {
template <typename T, typename = void>
struct HasPhaserWriter : std::false_type {};
template <typename T>
struct HasPhaserWriter<T, std::void_t<typename T::PhaserWriter>>
    : std::true_type {};
}  // namespace internal

namespace internal {
// What a vector of T does that does not depend on where the vector is.
template <typename T>
class MessageVectorOps {
 public:
  // These work on the vector of T whose VectorHeader is at 'header' in the
  // mutable buffer of 'runtime', which this process built.

  // AddElement() for the vector at 'header'.
  static ::phaser::BufferOffset AddElementAt(const RuntimeHandle& runtime,
                                             ::phaser::BufferOffset header) {
    return AddElementTo(
        runtime,
        runtime->pb->template UncheckedAddress<::phaser::VectorHeader>(header));
  }

  // Appends 'n' elements to the vector at 'header', calling 'f' with the
  // offset of each after adding it, as AddElementAt() would, and stops at the
  // first error 'f' returns.  'f' may allocate in the buffer but must not
  // change this vector.
  template <typename F>
  static absl::Status AddElementsAt(const RuntimeHandle& runtime,
                                    ::phaser::BufferOffset header, uint32_t n,
                                    F&& f) {
    return AppendElements<false>(runtime, header, n, f);
  }

  // AddElementsAt() for an 'f' that neither fails nor allocates, called with
  // a copy of 'f' with where to write each element and its index among the
  // 'n'.  That may be a copy of the element, stored once 'f' returns, so 'f'
  // must write only there.  The size includes the new elements only once all
  // are added.
  template <typename F>
  static void AddElementsInPlaceAt(const RuntimeHandle& runtime,
                                   ::phaser::BufferOffset header, uint32_t n,
                                   F&& f) {
    (void)AppendElements<true>(runtime, header, n, f);
  }

  // reserve() for the vector at 'header'.
  static void ReserveAt(const RuntimeHandle& runtime,
                        ::phaser::BufferOffset header, size_t n) {
    if (n != 0) {
      Grow(runtime, header, n, n);
    }
  }

  template <bool kInPlace, typename F>
  static absl::Status AppendElements(const RuntimeHandle& runtime,
                                     ::phaser::BufferOffset header, uint32_t n,
                                     F& f) {
    if (n == 0) {
      return absl::OkStatus();
    }
    ::phaser::PayloadBuffer** buffer = &runtime->pb;
    const uint32_t old_size =
        (*buffer)
            ->template AddressOf<::phaser::VectorHeader>(header)
            ->num_elements;
    const uint64_t end = uint64_t(old_size) + n;
    if (end > UINT32_MAX) {
      throw std::bad_alloc();
    }
    ReserveAt(runtime, header, end);
    // Only an allocation by 'f' can move the buffer.
    ::phaser::PayloadBuffer* pb = *buffer;
    const ::phaser::BufferOffset block_offset =
        pb->template AddressOf<::phaser::VectorHeader>(header)->data;
    const ::phaser::MessageVectorBlock block =
        *pb->template AddressOf<::phaser::MessageVectorBlock>(block_offset);
    // Chunk 0 holds first_capacity elements.  Chunk k > 0 holds
    // first_capacity << (k - 1) of them, from element first_capacity << (k -
    // 1).
    uint32_t k = 0;
    uint32_t chunk_begin = 0;
    uint32_t capacity = block.first_capacity;
    ::phaser::BufferOffset chunk = static_cast<::phaser::BufferOffset>(
        block_offset + sizeof(::phaser::MessageVectorBlock));
    // In place, a local copy of 'f' keeps its state in registers across the
    // element stores, which could otherwise alias it.
    [[maybe_unused]] F local_f = f;
    uint32_t index = old_size;
    while (index < end) {
      if constexpr (!kInPlace) {
        pb = *buffer;
      }
      while (index - chunk_begin >= capacity) {
        chunk_begin += capacity;
        capacity = block.first_capacity << k;
        ++k;
        chunk = *pb->template AddressOf<::phaser::BufferOffset>(
            static_cast<::phaser::BufferOffset>(
                block.directory + k * sizeof(::phaser::BufferOffset)));
      }
      const uint32_t chunk_end = static_cast<uint32_t>(
          std::min<uint64_t>(end, uint64_t(chunk_begin) + capacity));
      if constexpr (kInPlace) {
        char* binary = pb->template AddressOf<char>(chunk) +
                       size_t{index - chunk_begin} * Stride();
        for (; index < chunk_end; ++index, binary += Stride()) {
          if constexpr (Stride() <= kMaxStagedStride) {
            // Made in a local copy, which can stay in registers, so that only
            // the finished element is stored.
            char staged[Stride()];
            StartElement(staged, block.metadata);
            local_f(staged, index - old_size);
            // Copying the metadata and the presence word as one lets them be
            // stored together.
            uint64_t head;
            memcpy(&head, staged, sizeof(head));
            memcpy(binary, &head, sizeof(head));
            memcpy(binary + sizeof(head), staged + sizeof(head),
                   Stride() - sizeof(head));
          } else {
            StartElement(binary, block.metadata);
            local_f(binary, index - old_size);
          }
        }
      } else {
        for (; index < chunk_end; ++index) {
          pb = *buffer;
          const ::phaser::BufferOffset element =
              static_cast<::phaser::BufferOffset>(
                  chunk + (index - chunk_begin) * Stride());
          StartElement(pb->template AddressOf<char>(element), block.metadata);
          pb->template AddressOf<::phaser::VectorHeader>(header)->num_elements =
              index + 1;
          if (absl::Status status = f(element); !status.ok()) {
            return status;
          }
        }
      }
    }
    if constexpr (kInPlace) {
      pb->template AddressOf<::phaser::VectorHeader>(header)->num_elements =
          static_cast<uint32_t>(end);
    }
    return absl::OkStatus();
  }

  static constexpr uint32_t Stride() {
    return static_cast<uint32_t>((T::BinarySize() + 7) & ~size_t(7));
  }

  // AddElementsInPlaceAt() makes elements up to this size in a local copy.
  static constexpr uint32_t kMaxStagedStride = 64;

  // Makes the empty element at 'binary'.
  static void StartElement(char* binary, ::phaser::BufferOffset metadata) {
    memset(binary, 0, Stride());
    memcpy(binary, &metadata, sizeof(metadata));
  }

  // The first chunk of a vector that was not reserved holds about this many
  // bytes of elements, and at most kMaxDefaultFirstCapacity elements.
  static constexpr uint32_t kDefaultFirstChunkBytes = 64;
  static constexpr uint32_t kMaxDefaultFirstCapacity = 16;

  static uint32_t DefaultFirstCapacity() {
    return std::clamp<uint32_t>(kDefaultFirstChunkBytes / Stride(), 1,
                                kMaxDefaultFirstCapacity);
  }

  // Appends an empty element to the vector whose VectorHeader is 'hdr' in the
  // mutable buffer of 'runtime' and returns its offset.
  static ::phaser::BufferOffset AddElementTo(const RuntimeHandle& runtime,
                                             ::phaser::VectorHeader* hdr) {
    ::phaser::PayloadBuffer** buffer = &runtime->pb;
    const uint32_t index = hdr->num_elements;
    const ::phaser::MessageVectorBlock* block =
        (*buffer)->template UncheckedAddress<::phaser::MessageVectorBlock>(
            hdr->data);
    ::phaser::BufferOffset element;
    if (ABSL_PREDICT_TRUE(block != nullptr && index < block->first_capacity)) {
      element = static_cast<::phaser::BufferOffset>(
          hdr->data + sizeof(::phaser::MessageVectorBlock) + index * Stride());
    } else {
      const ::phaser::BufferOffset header = (*buffer)->UncheckedOffset(hdr);
      element = GrowForAdd(runtime, header, index);
      hdr =
          (*buffer)->template UncheckedAddress<::phaser::VectorHeader>(header);
      block =
          (*buffer)->template UncheckedAddress<::phaser::MessageVectorBlock>(
              hdr->data);
    }
    // This process wrote the block, so its stride is T's.
    StartElement((*buffer)->template UncheckedAddress<char>(element),
                 block->metadata);
    hdr->num_elements = index + 1;
    return element;
  }

  // AddElement() for when the element is not in the first chunk.  It takes
  // the runtime, not the handle, so that the handle need not be in memory.
  ABSL_ATTRIBUTE_NOINLINE static ::phaser::BufferOffset AddElementSlow(
      MessageRuntime* runtime, ::phaser::BufferOffset header) {
    return AddElementAt(runtime, header);
  }

  // Makes room for element 'index', which Add() is about to append to the
  // vector whose VectorHeader is at 'header', and returns its offset.
  ABSL_ATTRIBUTE_NOINLINE static ::phaser::BufferOffset GrowForAdd(
      const RuntimeHandle& runtime, ::phaser::BufferOffset header,
      uint32_t index) {
    Grow(runtime, header, size_t(index) + 1, DefaultFirstCapacity());
    const ::phaser::BufferOffset block =
        runtime->pb->template UncheckedAddress<::phaser::VectorHeader>(header)
            ->data;
    return internal::MessageVectorElement(
        *runtime, internal::LoadMessageVectorLayout(*runtime, block), index);
  }

  // Grows the storage of the vector whose VectorHeader is at 'header' to
  // hold at least 'n' elements.  If there is no storage yet, its first chunk
  // holds 'first_capacity' elements.
  static void Grow(const RuntimeHandle& runtime, ::phaser::BufferOffset header,
                   size_t n, size_t first_capacity) {
    ::phaser::PayloadBuffer** buffer = &runtime->pb;
    if ((*buffer)
            ->template UncheckedAddress<::phaser::VectorHeader>(header)
            ->data == 0) {
      const ::phaser::BufferOffset metadata = Message::MetadataFor<T>(runtime);
      const uint64_t bytes = sizeof(::phaser::MessageVectorBlock) +
                             uint64_t(first_capacity) * Stride();
      void* memory = bytes > UINT32_MAX ? nullptr
                                        : ::phaser::PayloadBuffer::Allocate(
                                              buffer, uint32_t(bytes), false);
      if (memory == nullptr) {
        throw std::bad_alloc();
      }
      *static_cast<::phaser::MessageVectorBlock*>(memory) = {
          Stride(), static_cast<uint32_t>(first_capacity), metadata, 0};
      (*buffer)
          ->template UncheckedAddress<::phaser::VectorHeader>(header)
          ->data = (*buffer)->UncheckedOffset(memory);
      if (first_capacity >= n) {
        return;
      }
    }
    const ::phaser::BufferOffset block_offset =
        (*buffer)
            ->template UncheckedAddress<::phaser::VectorHeader>(header)
            ->data;
    for (;;) {
      const auto* block =
          (*buffer)->template UncheckedAddress<::phaser::MessageVectorBlock>(
              block_offset);
      const uint32_t num_chunks =
          block->directory == 0
              ? 0
              : *(*buffer)->template UncheckedAddress<uint32_t>(
                    block->directory);
      // The next chunk doubles the capacity.
      const uint64_t capacity = uint64_t(block->first_capacity) << num_chunks;
      if (capacity >= n) {
        return;
      }
      const uint64_t bytes = capacity * block->stride;
      void* chunk =
          num_chunks == internal::kMaxMessageVectorChunks || bytes > UINT32_MAX
              ? nullptr
              : ::phaser::PayloadBuffer::Allocate(buffer, uint32_t(bytes),
                                                  false);
      if (chunk == nullptr) {
        throw std::bad_alloc();
      }
      const ::phaser::BufferOffset chunk_offset =
          (*buffer)->UncheckedOffset(chunk);
      const ::phaser::BufferOffset old_directory =
          (*buffer)
              ->template UncheckedAddress<::phaser::MessageVectorBlock>(
                  block_offset)
              ->directory;
      auto* directory =
          static_cast<::phaser::BufferOffset*>(::phaser::PayloadBuffer::Realloc(
              buffer, (*buffer)->UncheckedAddress(old_directory),
              (num_chunks + 2) * sizeof(::phaser::BufferOffset), false));
      if (directory == nullptr) {
        (*buffer)->FreeAt(chunk_offset);
        throw std::bad_alloc();
      }
      directory[0] = num_chunks + 1;
      directory[num_chunks + 1] = chunk_offset;
      (*buffer)
          ->template UncheckedAddress<::phaser::MessageVectorBlock>(
              block_offset)
          ->directory = (*buffer)->UncheckedOffset(directory);
    }
  }
};
}  // namespace internal

// The vector's data is a ::phaser::MessageVectorBlock holding the messages
// inline, in chunks that never move as the vector grows.
template <typename T, typename Loc = StoredFieldLoc>
class MessageVectorField : public FieldBase<Loc> {
  using Ops = internal::MessageVectorOps<T>;
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  MessageVectorField() = default;
  using FieldBase<Loc>::FieldBase;
  MessageVectorField(const MessageVectorField&) = default;
  MessageVectorField(MessageVectorField&&) = default;

  T operator[](int index) const {
    if (index < 0) {
      return T(InternalDefault{});
    }
    const ::phaser::BufferOffset element =
        ElementOffset(static_cast<size_t>(index));
    if (element == 0) {
      return T(InternalDefault{});
    }
    return T(BorrowedRuntime(GetRuntime()), element);
  }

  T operator[](int index) {
    if (GetRuntime() != nullptr && GetRuntime()->IsMutable()) {
      return Mutable(static_cast<size_t>(index));
    }
    return static_cast<const MessageVectorField*>(this)->operator[](index);
  }

  T at(size_t index) {
    if (index >= size()) {
      throw std::out_of_range("MessageVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }
  T at(size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("MessageVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }

  T front() { return (*this)[0]; }
  T front() const { return (*this)[0]; }
  T back() { return (*this)[static_cast<int>(size() - 1)]; }
  T back() const { return (*this)[static_cast<int>(size() - 1)]; }

  using value_type = T;
  using reference = T;
  using const_reference = T;
  using pointer = void;
  using const_pointer = void;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  class const_iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using pointer = void;
    using reference = T;

    const_iterator() = default;
    const_iterator(const MessageVectorField* field, size_t index)
        : field_(field), index_(index) {}
    T operator*() const { return field_->Get(index_); }
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
    size_t Index() const { return index_; }

   private:
    const MessageVectorField* field_ = nullptr;
    size_t index_ = 0;
  };
  using iterator = const_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size()); }
  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, size()); }
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

  void push_back(const T& v) { (void)Add().CloneFrom(v); }

  void push_back(T&& v) { (void)Add().CloneFrom(v); }

  T emplace_back() { return Add(); }

  // The handle borrows the runtime of the message this vector is in, so it is
  // valid only while the handle it was made from exists.
  T Add() {
    if constexpr (std::is_constructible_v<T, MutablePart, const RuntimeHandle&,
                                          ::phaser::BufferOffset>) {
      // Read before the element is stored, which the compiler must assume
      // could change this handle.
      const RuntimeHandle runtime = BorrowedRuntime(GetRuntime());
      const ::phaser::BufferOffset element = AddElement();
      return T(MutablePart{}, runtime, element);
    } else {
      const ::phaser::BufferOffset element = AddElement();
      return T(BorrowedRuntime(GetRuntime()), element);
    }
  }

  // Appends 'n' elements, calling 'f(writer, i)' to set the fields of the
  // i'th through 'writer', a T::PhaserWriter that is valid only during the
  // call.  T has one when its fields are all singular scalars.  'f' must not
  // change the message: 'writer' may write to a copy of the element, stored
  // once 'f' returns, and the size includes the new elements only once all
  // are added.
  template <typename F>
  void Add(size_t n, F&& f) {
    static_assert(internal::HasPhaserWriter<T>::value,
                  "Add(n, f) needs a message whose fields are all singular "
                  "scalars");
    const Message* message = Parent();
    if (ABSL_PREDICT_FALSE(!message->IsMutable())) {
      Field::ThrowReadonlyMessage();
    }
    if (n > UINT32_MAX) {
      throw std::bad_alloc();
    }
    // A mutable message has its compiled layout.
    Ops::AddElementsInPlaceAt(
        message->runtime, message->absolute_binary_offset + Offset(),
        static_cast<uint32_t>(n), [&f](char* binary, uint32_t index) {
          f(typename T::PhaserWriter(binary), size_t{index});
        });
  }

  // Add() without the handle: appends an empty element and returns its offset.
  ::phaser::BufferOffset AddElement() {
    const Message* message = Parent();
    if (ABSL_PREDICT_FALSE(!message->IsMutable())) {
      Field::ThrowReadonlyMessage();
    }
    // A mutable message has its compiled layout.
    MessageRuntime* runtime = message->runtime;
    ::phaser::PayloadBuffer* pb = runtime->pb;
    const ::phaser::BufferOffset header =
        message->absolute_binary_offset + Offset();
    auto* hdr = pb->template AddressOf<::phaser::VectorHeader>(header);
    const uint32_t index = hdr->num_elements;
    if (ABSL_PREDICT_TRUE(hdr->data != 0)) {
      const auto* block =
          pb->template AddressOf<::phaser::MessageVectorBlock>(hdr->data);
      if (ABSL_PREDICT_TRUE(index < block->first_capacity)) {
        const auto element = static_cast<::phaser::BufferOffset>(
            hdr->data + sizeof(::phaser::MessageVectorBlock) +
            index * Ops::Stride());
        Ops::StartElement(pb->template AddressOf<char>(element),
                          block->metadata);
        hdr->num_elements = index + 1;
        return element;
      }
    }
    return Ops::AddElementSlow(runtime, header);
  }

  // The offset of the VectorHeader in a mutable message.
  ::phaser::BufferOffset VectorHeaderOffset() const {
    return (*GetBufferAddr())->UncheckedOffset(ResolvedVectorHeader());
  }

  T Get(size_t index) const { return (*this)[static_cast<int>(index)]; }

  T Mutable(size_t index) {
    if (index >= size()) {
      resize(index + 1);
    }
    const ::phaser::BufferOffset element = ElementOffset(index);
    if (element == 0) {
      return T(InternalDefault{});
    }
    return T(BorrowedRuntime(GetRuntime()), element);
  }

  // Allocate a bunch of empty messages.
  std::vector<T> Allocate(size_t n) {
    std::vector<T> result;
    result.reserve(n);
    Clear();
    reserve(n);
    for (size_t i = 0; i < n; i++) {
      result.push_back(Add());
    }
    return result;
  }

  size_t capacity() const {
    const int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr || hdr->data == 0) {
      return 0;
    }
    return internal::LoadMessageVectorLayout(*GetRuntime(), hdr->data)
        .Capacity();
  }

  void reserve(size_t n) {
    const Message* message = Parent();
    // Throws for a readonly message even when there is room.
    if (ABSL_PREDICT_FALSE(!message->IsMutable())) {
      Field::ThrowReadonlyMessage();
    }
    // A mutable message has its compiled layout.
    Ops::ReserveAt(message->runtime, message->absolute_binary_offset + Offset(),
                   n);
  }

  void resize(size_t n) {
    const size_t old_size = size();
    if (n < old_size) {
      (void)GetBufferAddr();
      if constexpr (!internal::ClearReleasesNothing<T>::value) {
        for (size_t i = n; i < old_size; ++i) {
          internal::ReleaseMessageContents<T>(GetRuntime(), ElementOffset(i));
        }
      }
      ResolvedVectorHeader()->num_elements = static_cast<uint32_t>(n);
      return;
    }
    reserve(n);
    while (size() < n) {
      (void)Add();
    }
  }
  void resize(size_t n, const T& value) {
    const size_t old_size = size();
    resize(n);
    for (size_t i = old_size; i < n; ++i) {
      (void)Mutable(i).CloneFrom(value);
    }
  }

  void pop_back() {
    if (!empty()) {
      resize(size() - 1);
    }
  }

  void assign(size_t count, const T& value) {
    Clear();
    for (size_t i = 0; i < count; ++i) {
      push_back(value);
    }
  }
  template <typename InputIterator>
  void assign(InputIterator first, InputIterator last) {
    Clear();
    for (; first != last; ++first) {
      push_back(*first);
    }
  }
  void assign(std::initializer_list<T> values) {
    assign(values.begin(), values.end());
  }

  iterator insert(const_iterator pos, const T& value) {
    const size_t index = pos.Index();
    std::vector<T> values = CopyValues();
    values.insert(values.begin() + static_cast<ptrdiff_t>(index), value);
    assign(values.begin(), values.end());
    return iterator(this, index);
  }
  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    return insert(pos, T(std::forward<Args>(args)...));
  }
  iterator erase(const_iterator pos) {
    const size_t index = pos.Index();
    std::vector<T> values = CopyValues();
    values.erase(values.begin() + static_cast<ptrdiff_t>(index));
    assign(values.begin(), values.end());
    return iterator(this, index);
  }
  iterator erase(const_iterator first, const_iterator last) {
    const size_t first_index = first.Index();
    std::vector<T> values = CopyValues();
    values.erase(values.begin() + static_cast<ptrdiff_t>(first_index),
                 values.begin() + static_cast<ptrdiff_t>(last.Index()));
    assign(values.begin(), values.end());
    return iterator(this, first_index);
  }
  void swap(MessageVectorField& other) {
    if (this == &other) {
      return;
    }
    const std::vector<T> mine = CopyValues();
    const std::vector<T> theirs = other.CopyValues();
    *this = theirs;
    other = mine;
  }

  void Clear() {
    ::phaser::VectorHeader* hdr = ResolvedVectorHeader();
    if (hdr == nullptr) {
      return;
    }
    (void)GetBufferAddr();
    internal::ReleaseMessageVector<T>(GetRuntime(),
                                      GetRuntime()->ToOffset(hdr));
  }
  void clear() { Clear(); }

  size_t size() const { return NumElements(); }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  MessageVectorField& operator=(const MessageVectorField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  MessageVectorField& operator=(const MessageVectorField<T, OtherLoc>& other) {
    Assign(other);
    return *this;
  }
  MessageVectorField& operator=(MessageVectorField&& other) noexcept {
    return operator=(static_cast<const MessageVectorField&>(other));
  }
  MessageVectorField& operator=(std::initializer_list<T> values) {
    assign(values);
    return *this;
  }
  template <typename Allocator>
  MessageVectorField& operator=(const std::vector<T, Allocator>& values) {
    assign(values.begin(), values.end());
    return *this;
  }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  template <typename OtherLoc>
  bool operator==(const MessageVectorField<T, OtherLoc>& other) const {
    if (size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < size(); ++i) {
      if (Get(i) != other.Get(i)) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const MessageVectorField<T, OtherLoc>& other) const {
    return !operator==(other);
  }
  template <typename Allocator>
  bool operator==(const std::vector<T, Allocator>& other) const {
    if (size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < size(); ++i) {
      if (Get(i) != other[i]) {
        return false;
      }
    }
    return true;
  }
  template <typename Allocator>
  bool operator!=(const std::vector<T, Allocator>& other) const {
    return !(*this == other);
  }
  template <typename Allocator>
  friend bool operator==(const std::vector<T, Allocator>& lhs,
                         const MessageVectorField& rhs) {
    return rhs == lhs;
  }
  template <typename Allocator>
  friend bool operator!=(const std::vector<T, Allocator>& lhs,
                         const MessageVectorField& rhs) {
    return !(rhs == lhs);
  }

  std::vector<T> Get() const {
    std::vector<T> result;
    result.reserve(size());
    for (size_t i = 0; i < size(); ++i) {
      result.push_back(Get(i));
    }
    return result;
  }

  void Populate() const {}

  size_t SerializedSize() const {
    size_t length = 0;
    for (size_t i = 0; i < size(); i++) {
      const T message = Get(i);
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Number(), message.SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    for (size_t i = 0; i < sz; ++i) {
      const T msg = Get(i);
      if (absl::Status status = buffer.SerializeLengthDelimitedHeader(
              Number(), msg.SerializedSize());
          !status.ok()) {
        return status;
      }
      if (absl::Status status = msg.Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<absl::Span<char>> v = buffer.DeserializeLengthDelimited();
    if (!v.ok()) {
      return v.status();
    }
    ProtoBuffer msg_buffer(*v);
    T msg = Add();
    if (absl::Status status = msg.Deserialize(msg_buffer); !status.ok()) {
      return status;
    }
    return absl::OkStatus();
  }

  // Every element handle is a temporary that writes its cached values to the
  // payload when it is destroyed, so no element has anything left to sync.
  void SyncToPayload() const {}

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      T m = Add();
      if (absl::Status s = m.CloneFrom(other.Get(i)); !s.ok()) {
        return;
      }
    }
  }

  std::vector<T> CopyValues() const {
    std::vector<T> result;
    result.reserve(size());
    for (size_t i = 0; i < size(); ++i) {
      T copy;
      (void)copy.CloneFrom(Get(i));
      result.push_back(std::move(copy));
    }
    return result;
  }

  // The offset of element 'index', or 0 if there is no such element.
  ::phaser::BufferOffset ElementOffset(size_t index) const {
    const int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr || hdr->data == 0 || index >= hdr->num_elements) {
      return 0;
    }
    const MessageRuntime& runtime = *GetRuntime();
    const internal::MessageVectorLayout layout =
        internal::LoadMessageVectorLayout(runtime, hdr->data);
    if (index >= layout.Capacity()) {
      return 0;
    }
    return internal::MessageVectorElement(runtime, layout,
                                          static_cast<uint32_t>(index));
  }

  friend FieldIterator<MessageVectorField, T>;
  friend FieldIterator<MessageVectorField, const T>;
  ::phaser::VectorHeader* Header(
      ::phaser::BufferOffset relative_offset = 0) const {
    if (relative_offset == 0) {
      relative_offset = Offset();
    }
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + relative_offset);
  }

  ::phaser::VectorHeader* ResolvedVectorHeader() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      offset = static_cast<int32_t>(Offset());
    }
    return Header(static_cast<uint32_t>(offset));
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr || hdr->data == 0) {
      return 0;
    }
    const MessageRuntime& runtime = *GetRuntime();
    if (runtime.IsMutable()) {
      return hdr->num_elements;
    }
    // A hostile count is limited to as many elements as the buffer could
    // hold.  Reading an element checks that it is inside the buffer.
    return std::min<size_t>(hdr->num_elements,
                            runtime.TrustedSize() / sizeof(uint64_t));
  }
};

// This is a little more complex.  The binary vector contains a set of
// ::phaser::BufferOffsets each of which contains the offset into the
// ::phaser::PayloadBuffer of a ::phaser::StringHeader.  Each
// ::phaser::StringHeader contains the offset of the string data which is a
// length followed by the string contents.
//
// +-----------+
// |           |
// +-----------+         +----------+
// |           +-------->|    hdr   +------->+-------------+
// +-----------+         +----------+        |   length    |
// |    ...    |                             +-------------+
// +-----------+                             |  "string"   |
// |           |                             |   "data"    |
// +-----------+                             +-------------+
template <typename Loc>
class BasicStringVectorField : public FieldBase<Loc> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  using FieldBase<Loc>::FieldBase;
  BasicStringVectorField() = default;
  BasicStringVectorField(const BasicStringVectorField&) = default;
  BasicStringVectorField(BasicStringVectorField&&) = default;

  std::string_view operator[](int index) const {
    if (index < 0 || static_cast<size_t>(index) >= NumElements()) {
      return {};
    }
    int32_t offset = FindFieldOffset();
    if (offset == -1) {
      return {};
    }
    auto hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return {};
    }
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data == nullptr || data[index] == 0) {
      return {};
    }
    return GetRuntime()->GetStringView(data[index]);
  }

  NonEmbeddedStringField operator[](int index) {
    if (index < 0 || static_cast<size_t>(index) >= NumElements()) {
      return {};
    }
    int32_t offset = FindFieldOffset();
    if (offset == -1) {
      return {};
    }
    auto hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return {};
    }
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data == nullptr) {
      return {};
    }
    if (data[index] == 0) {
      void* str_hdr = ::phaser::PayloadBuffer::Allocate(
          GetBufferAddr(), sizeof(::phaser::StringHeader));
      if (str_hdr == nullptr) {
        return {};
      }
      const ::phaser::BufferOffset string_offset =
          GetRuntime()->ToOffset(str_hdr);
      hdr = ResolvedVectorHeader();
      if (hdr == nullptr) {
        return {};
      }
      data =
          GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
      if (data == nullptr) {
        return {};
      }
      data[index] = string_offset;
    }
    return NonEmbeddedStringField(Parent(), data[index]);
  }

  NonEmbeddedStringField at(size_t index) {
    if (index >= size()) {
      throw std::out_of_range("StringVectorField::at");
    }
    return (*this)[static_cast<int>(index)];
  }
  std::string_view at(size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("StringVectorField::at");
    }
    return Get(index);
  }

  using value_type = std::string_view;
  using reference = std::string_view;
  using const_reference = std::string_view;
  using pointer = void;
  using const_pointer = void;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  class const_iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::string_view;
    using difference_type = ptrdiff_t;
    using pointer = void;
    using reference = std::string_view;

    const_iterator() = default;
    const_iterator(const BasicStringVectorField* field, size_t index)
        : field_(field), index_(index) {}
    std::string_view operator*() const { return field_->Get(index_); }
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
    size_t Index() const { return index_; }

   private:
    const BasicStringVectorField* field_ = nullptr;
    size_t index_ = 0;
  };
  using iterator = const_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size()); }
  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, size()); }
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

  size_t size() const { return NumElements(); }
  NonEmbeddedStringField* data() = delete;
  const NonEmbeddedStringField* data() const = delete;
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  NonEmbeddedStringField front() { return (*this)[0]; }
  std::string_view front() const { return Get(0); }
  NonEmbeddedStringField back() {
    return (*this)[static_cast<int>(size() - 1)];
  }
  std::string_view back() const { return Get(size() - 1); }

  BasicStringVectorField& operator=(const BasicStringVectorField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  BasicStringVectorField& operator=(
      const BasicStringVectorField<OtherLoc>& other) {
    Assign(other);
    return *this;
  }

  BasicStringVectorField& operator=(BasicStringVectorField&& other) noexcept {
    return operator=(static_cast<const BasicStringVectorField&>(other));
  }

  template <typename Str>
  void push_back(Str s) {
    // Allocate string header in buffer.
    void* str_hdr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), sizeof(::phaser::StringHeader));
    ::phaser::BufferOffset hdr_offset = GetRuntime()->ToOffset(str_hdr);
    ::phaser::PayloadBuffer::SetString(GetBufferAddr(), s, hdr_offset);

    // Add an offset for the new string to the binary.
    ::phaser::PayloadBuffer::VectorPush<::phaser::BufferOffset>(
        GetBufferAddr(), ResolvedVectorHeader(), hdr_offset);
  }

  template <typename Str>
  NonEmbeddedStringField emplace_back(Str s) {
    push_back(s);
    return back();
  }
  NonEmbeddedStringField emplace_back() {
    push_back(std::string_view());
    return back();
  }

  void Add(const char* s, size_t len) { push_back(std::string_view(s, len)); }
  template <typename Str>
  void Add(Str s) {
    push_back(s);
  }

  std::string_view Get(size_t index) const {
    return (*this)[static_cast<int>(index)];
  }

  template <typename Str>
  void Set(size_t index, Str s) {
    if (index >= size()) {
      ::phaser::PayloadBuffer::VectorResize<::phaser::BufferOffset>(
          GetBufferAddr(), ResolvedVectorHeader(), index + 1);
    }

    auto hdr = ResolvedVectorHeader();
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data[index] == 0) {
      // Allocate string header in buffer.
      void* str_hdr = ::phaser::PayloadBuffer::Allocate(
          GetBufferAddr(), sizeof(::phaser::StringHeader));
      ::phaser::BufferOffset hdr_offset = GetRuntime()->ToOffset(str_hdr);

      hdr = ResolvedVectorHeader();
      data =
          GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
      data[index] = hdr_offset;
    }
    NonEmbeddedStringField(Parent(), data[index]).Set(s);
  }

  size_t capacity() const {
    return GetRuntime()->AllocatedCapacity(BaseOffset(),
                                           sizeof(::phaser::BufferOffset));
  }

  void reserve(size_t n) {
    ::phaser::PayloadBuffer::VectorReserve<::phaser::BufferOffset>(
        GetBufferAddr(), ResolvedVectorHeader(), n);
  }

  void resize(size_t n) {
    const size_t old_size = size();
    if (n < old_size) {
      for (size_t i = n; i < old_size; ++i) {
        auto hdr = ResolvedVectorHeader();
        auto data = GetRuntime()->template ToAddress<::phaser::BufferOffset>(
            hdr->data);
        const ::phaser::BufferOffset offset = data[i];
        if (offset != 0) {
          NonEmbeddedStringField(Parent(), offset).Clear();
        }
      }
    }
    ::phaser::PayloadBuffer::VectorResize<::phaser::BufferOffset>(
        GetBufferAddr(), ResolvedVectorHeader(), n);
  }
  void resize(size_t n, std::string_view value) {
    const std::string value_copy(value);
    const size_t old_size = size();
    resize(n);
    for (size_t i = old_size; i < n; ++i) {
      Set(i, value_copy);
    }
  }

  void pop_back() {
    if (!empty()) {
      resize(size() - 1);
    }
  }

  void assign(size_t count, std::string_view value) {
    const std::string value_copy(value);
    Clear();
    reserve(count);
    for (size_t i = 0; i < count; ++i) {
      push_back(value_copy);
    }
  }
  template <typename InputIterator>
  void assign(InputIterator first, InputIterator last) {
    Clear();
    for (; first != last; ++first) {
      push_back(*first);
    }
  }
  void assign(std::initializer_list<std::string_view> values) {
    assign(values.begin(), values.end());
  }

  iterator insert(const_iterator pos, std::string_view value) {
    const size_t index = pos.Index();
    std::vector<std::string> values = CopyValues();
    values.insert(values.begin() + static_cast<ptrdiff_t>(index),
                  std::string(value));
    assign(values.begin(), values.end());
    return iterator(this, index);
  }
  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    return insert(pos, std::string(std::forward<Args>(args)...));
  }
  iterator erase(const_iterator pos) {
    const size_t index = pos.Index();
    std::vector<std::string> values = CopyValues();
    values.erase(values.begin() + static_cast<ptrdiff_t>(index));
    assign(values.begin(), values.end());
    return iterator(this, index);
  }
  iterator erase(const_iterator first, const_iterator last) {
    const size_t first_index = first.Index();
    std::vector<std::string> values = CopyValues();
    values.erase(values.begin() + static_cast<ptrdiff_t>(first_index),
                 values.begin() + static_cast<ptrdiff_t>(last.Index()));
    assign(values.begin(), values.end());
    return iterator(this, first_index);
  }
  void swap(BasicStringVectorField& other) {
    if (this == &other) {
      return;
    }
    const std::vector<std::string> mine = CopyValues();
    const std::vector<std::string> theirs = other.CopyValues();
    *this = theirs;
    other = mine;
  }

  void Clear() {
    auto hdr = ResolvedVectorHeader();
    auto data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    for (size_t i = 0; i < hdr->num_elements; ++i) {
      const ::phaser::BufferOffset offset = data[i];
      if (offset == 0) {
        continue;
      }
      NonEmbeddedStringField(Parent(), offset).Clear();
    }
    ::phaser::PayloadBuffer::VectorClear<::phaser::BufferOffset>(
        GetBufferAddr(), ResolvedVectorHeader());
  }

  void clear() { Clear(); }  // STL compatibility.

  BasicStringVectorField& operator=(
      std::initializer_list<std::string_view> values) {
    assign(values);
    return *this;
  }
  template <typename String, typename Allocator>
  BasicStringVectorField& operator=(
      const std::vector<String, Allocator>& values) {
    assign(values.begin(), values.end());
    return *this;
  }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  template <typename OtherLoc>
  bool operator==(const BasicStringVectorField<OtherLoc>& other) const {
    if (size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < size(); ++i) {
      if (Get(i) != other.Get(i)) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const BasicStringVectorField<OtherLoc>& other) const {
    return !(*this == other);
  }
  template <typename String, typename Allocator>
  bool operator==(const std::vector<String, Allocator>& other) const {
    if (size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < size(); ++i) {
      if (Get(i) != std::string_view(other[i])) {
        return false;
      }
    }
    return true;
  }
  template <typename String, typename Allocator>
  bool operator!=(const std::vector<String, Allocator>& other) const {
    return !(*this == other);
  }
  template <typename String, typename Allocator>
  friend bool operator==(const std::vector<String, Allocator>& lhs,
                         const BasicStringVectorField& rhs) {
    return rhs == lhs;
  }
  template <typename String, typename Allocator>
  friend bool operator!=(const std::vector<String, Allocator>& lhs,
                         const BasicStringVectorField& rhs) {
    return !(rhs == lhs);
  }

  void Populate() const {}

  std::vector<std::string_view> Get() const {
    std::vector<std::string_view> r;
    r.reserve(size());
    for (size_t i = 0; i < size(); ++i) {
      r.push_back(Get(i));
    }
    return r;
  }

  size_t SerializedSize() const {
    size_t length = 0;
    for (size_t i = 0; i < size(); i++) {
      length +=
          phaser::ProtoBuffer::LengthDelimitedSize(Number(), Get(i).size());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    for (size_t i = 0; i < sz; ++i) {
      const std::string_view s = Get(i);
      if (absl::Status status =
              buffer.SerializeLengthDelimited(Number(), s.data(), s.size());
          !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<std::string_view> v = buffer.DeserializeString();
    if (!v.ok()) {
      return v.status();
    }
    push_back(*v);
    return absl::OkStatus();
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      push_back(other.Get(i));
    }
  }
  std::vector<std::string> CopyValues() const {
    std::vector<std::string> result;
    result.reserve(size());
    for (size_t i = 0; i < size(); ++i) {
      result.emplace_back(Get(i));
    }
    return result;
  }

  ::phaser::VectorHeader* Header(
      ::phaser::BufferOffset relative_offset = 0) const {
    if (relative_offset == 0) {
      relative_offset = Offset();
    }
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + relative_offset);
  }

  ::phaser::VectorHeader* ResolvedVectorHeader() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      offset = static_cast<int32_t>(Offset());
    }
    return Header(static_cast<uint32_t>(offset));
  }

  ::phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const ::phaser::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->ClampElementCount(hdr->data, hdr->num_elements,
                                           sizeof(::phaser::BufferOffset));
  }
};

using StringVectorField = BasicStringVectorField<StoredFieldLoc>;

#undef DECLARE_ZERO_COPY_VECTOR_BITS
#undef DECLARE_RELAY_VECTOR_BITS

namespace internal {
template <typename T, bool FixedSize, bool Signed, bool Packed, typename L>
struct FieldRelease<PrimitiveVectorField<T, FixedSize, Signed, Packed, L>>
    : VectorDataRelease {};

template <typename Enum, typename Stringizer, typename Parser, bool Packed,
          typename L>
struct FieldRelease<EnumVectorField<Enum, Stringizer, Parser, Packed, L>>
    : VectorDataRelease {};

template <typename T, typename L>
struct FieldRelease<MessageVectorField<T, L>> : MessageVectorRelease<T> {};

template <typename T, bool FixedSize, bool Signed, bool Packed, typename L>
struct FieldResetClear<PrimitiveVectorField<T, FixedSize, Signed, Packed, L>>
    : NoResetClear {};

template <typename Enum, typename Stringizer, typename Parser, bool Packed,
          typename L>
struct FieldResetClear<EnumVectorField<Enum, Stringizer, Parser, Packed, L>>
    : NoResetClear {};

template <typename T, typename L>
struct FieldResetClear<MessageVectorField<T, L>> : NoResetClear {};

template <typename L>
struct FieldResetClear<BasicStringVectorField<L>> : NoResetClear {};

template <typename L>
struct FieldRelease<BasicStringVectorField<L>> : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    ::phaser::PayloadBuffer* pb = runtime->pb;
    const ::phaser::VectorHeader* hdr =
        PayloadAt<::phaser::VectorHeader>(pb, field);
    if (hdr == nullptr) {
      return;
    }
    const ::phaser::BufferOffset* data =
        PayloadAt<::phaser::BufferOffset>(pb, hdr->data, hdr->num_elements);
    if (data != nullptr) {
      for (uint32_t i = 0; i < hdr->num_elements; ++i) {
        if (data[i] != 0) {
          pb->FreeAt(OffsetAt(pb, data[i]));
        }
      }
    }
    pb->FreeAt(hdr->data);
  }
};
}  // namespace internal

}  // namespace phaser
