// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Fixed-extent array facades backed by the same VectorHeader layout as repeated
// vector fields. Wire encoding remains standard protobuf repeated-field format.

#include <stdint.h>
#include <stdlib.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/fields.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/vectors.h"
#include "phaser/runtime/wireformat.h"
#include "phaser/runtime/payload_buffer.h"

namespace phaser {

inline bool HasMutablePayload(const RuntimeHandle& runtime) {
  return runtime != nullptr && runtime->IsMutable();
}

template <typename Field, typename Value>
struct FixedArrayConstIterator {
  FixedArrayConstIterator(const Field* f, size_t idx,
                          bool reverse_iteration = false)
      : field(f), index(idx), reverse(reverse_iteration) {}

  FixedArrayConstIterator& operator++() {
    if (reverse) {
      if (index == static_cast<size_t>(-1)) {
        return *this;
      }
      --index;
    } else {
      ++index;
    }
    return *this;
  }
  FixedArrayConstIterator& operator--() {
    if (reverse) {
      ++index;
    } else {
      if (index == 0) {
        index = static_cast<size_t>(-1);
      } else {
        --index;
      }
    }
    return *this;
  }
  FixedArrayConstIterator operator+(size_t i) const {
    if (reverse) {
      return FixedArrayConstIterator(field, index - i, true);
    }
    return FixedArrayConstIterator(field, index + i);
  }
  FixedArrayConstIterator operator-(size_t i) const {
    if (reverse) {
      return FixedArrayConstIterator(field, index + i, true);
    }
    return FixedArrayConstIterator(field, index - i);
  }

  const Value& operator*() const { return field->ConstRefAt(index); }

  bool operator==(const FixedArrayConstIterator& it) const {
    return field == it.field && index == it.index && reverse == it.reverse;
  }
  bool operator!=(const FixedArrayConstIterator& it) const {
    return !operator==(it);
  }

  const Field* field;
  size_t index;
  bool reverse;
};

#define DECLARE_FIXED_ARRAY_BITS(classname, vtype, itype, ctype, utype, extent) \
  using value_type = vtype;                                                    \
  using reference = value_type&;                                               \
  using const_reference = value_type&;                                         \
  using pointer = value_type*;                                                 \
  using const_pointer = const value_type*;                                     \
  using size_type = size_t;                                                    \
  using difference_type = ptrdiff_t;                                           \
                                                                               \
  using iterator = itype;                                                      \
  using const_iterator = ctype;                                                \
  using reverse_iterator = itype;                                              \
  using const_reverse_iterator = ctype;                                        \
                                                                               \
  iterator begin() {                                                             \
    EnsureExtent();                                                              \
    return iterator(this, BaseOffset());                                         \
  }                                                                              \
  iterator end() {                                                               \
    EnsureExtent();                                                              \
    return iterator(                                                             \
        this, BaseOffset() + static_cast<::phaser::BufferOffset>(extent *     \
                                                                    sizeof(      \
                                                                        utype))); \
  }                                                                              \
  const_iterator begin() const {                                                 \
    return const_iterator(this, 0);                                              \
  }                                                                              \
  const_iterator end() const {                                                   \
    return const_iterator(this, extent);                                         \
  }                                                                              \
  const_iterator cbegin() const { return begin(); }                              \
  const_iterator cend() const { return end(); }                                  \
  reverse_iterator rbegin() {                                                    \
    return reverse_iterator(this, BaseOffset(), true);                           \
  }                                                                              \
  reverse_iterator rend() {                                                      \
    return reverse_iterator(                                                     \
        this, BaseOffset() + static_cast<::phaser::BufferOffset>(extent *     \
                                                                    sizeof(      \
                                                                        utype)),   \
        true);                                                                   \
  }                                                                              \
  const_reverse_iterator rbegin() const {                                        \
    return const_reverse_iterator(                                               \
        this, extent == 0 ? static_cast<size_t>(-1) : extent - 1, true);         \
  }                                                                              \
  const_reverse_iterator rend() const {                                          \
    return const_reverse_iterator(this, static_cast<size_t>(-1), true);          \
  }                                                                              \
  const_reverse_iterator crbegin() const { return rbegin(); }                  \
  const_reverse_iterator crend() const { return rend(); }

template <typename T, size_t N, bool FixedSize = false, bool Signed = false,
          bool Packed = true, typename Loc = StoredFieldLoc>
class PrimitiveArrayField : public FieldBase<Loc> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  static constexpr size_t kExtent = N;

  using FieldBase<Loc>::FieldBase;
  PrimitiveArrayField() = default;
  PrimitiveArrayField(const PrimitiveArrayField&) = default;
  PrimitiveArrayField(PrimitiveArrayField&&) = default;

  // A detached value when the buffer has no space for the array, so a write
  // through the reference cannot touch the caller's storage.
  T& operator[](size_t index) {
    EnsureExtent();
    T* base = data();
    if (base == nullptr) {
      static thread_local T detached;
      detached = T{};
      return detached;
    }
    return base[index];
  }

  T operator[](size_t index) const { return GetAt(index); }

  T& front() { return (*this)[0]; }
  const T front() const { return GetAt(0); }
  T& back() { return (*this)[N - 1]; }
  const T back() const { return GetAt(N - 1); }

  T Get(size_t index) const { return GetAt(index); }

  const T& ConstRefAt(size_t index) const {
    if (index >= N) {
      static const T kDefault{};
      return kDefault;
    }
    const size_t count = NumElements();
    if (index >= count) {
      static const T kDefault{};
      return kDefault;
    }
    const T* base = StoragePointer();
    if (base == nullptr) {
      static const T kDefault{};
      return kDefault;
    }
    return base[index];
  }

  void Set(size_t index, T v) { (*this)[index] = v; }

  std::array<T, N> Get() const {
    std::array<T, N> v;
    for (size_t i = 0; i < N; i++) {
      v[i] = GetAt(i);
    }
    return v;
  }

#define ITYPE FieldIterator<PrimitiveArrayField, value_type>
#define CTYPE FixedArrayConstIterator<PrimitiveArrayField, value_type>
  DECLARE_FIXED_ARRAY_BITS(PrimitiveArrayField, T, ITYPE, CTYPE, T, N)
#undef ITYPE
#undef CTYPE

  void BeginDeserialize() {
    ::phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(), Header(Offset()));
  }

  absl::Status FinalizeDeserialize() {
    EnsureExtent();
    return absl::OkStatus();
  }

  void Clear() {
    ::phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(), Header(Offset()));
    if (!EnsureStorage(N)) {
      return;
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return;
    }
    for (size_t i = 0; i < N; i++) {
      base[i] = T{};
    }
    SetActiveCount(N);
  }
  void clear() { Clear(); }

  PrimitiveArrayField& operator=(const PrimitiveArrayField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  PrimitiveArrayField& operator=(
      const PrimitiveArrayField<T, N, FixedSize, Signed, Packed, OtherLoc>&
          other) {
    Assign(other);
    return *this;
  }
  PrimitiveArrayField& operator=(PrimitiveArrayField&& other) noexcept {
    return operator=(static_cast<const PrimitiveArrayField&>(other));
  }

  size_t size() const { return N; }
  size_t Size() const { return N; }
  size_t max_size() const { return N; }
  bool empty() const { return N == 0; }

  T* data() {
    EnsureExtent();
    return GetRuntime()->template ToAddress<T>(BaseOffset());
  }
  const T* data() const { return StoragePointer(); }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  template <typename OtherLoc>
  bool operator==(const PrimitiveArrayField<T, N, FixedSize, Signed, Packed,
                                            OtherLoc>& other) const {
    for (size_t i = 0; i < N; i++) {
      if (GetAt(i) != other.Get(i)) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const PrimitiveArrayField<T, N, FixedSize, Signed, Packed,
                                            OtherLoc>& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t sz = ActiveElementCount();
    if (sz == 0) {
      return 0;
    }
    size_t length = 0;
    if constexpr (Packed) {
      if constexpr (FixedSize) {
        return ProtoBuffer::LengthDelimitedSize(Number(), sz * sizeof(T));
      } else {
        for (size_t i = 0; i < sz; i++) {
          length += ProtoBuffer::VarintSize<T, Signed>(GetAt(i));
        }
        return ProtoBuffer::LengthDelimitedSize(Number(), length);
      }
    }
    if constexpr (FixedSize) {
      length += sz * (ProtoBuffer::TagSize(Number(),
                                           ProtoBuffer::FixedWireType<T>()) +
                      sizeof(T));
    } else {
      for (size_t i = 0; i < sz; i++) {
        length += ProtoBuffer::TagSize(Number(), WireType::kVarint) +
                  ProtoBuffer::VarintSize<T, Signed>(GetAt(i));
      }
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t sz = ActiveElementCount();
    if (sz == 0) {
      return absl::OkStatus();
    }
    if constexpr (Packed) {
      if constexpr (FixedSize) {
        const T* base = StoragePointer();
        if (base == nullptr) {
          return absl::OkStatus();
        }
        return buffer.SerializeLengthDelimited(
            Number(), reinterpret_cast<const char*>(base), sz * sizeof(T));
      } else {
        size_t length = 0;
        for (size_t i = 0; i < sz; i++) {
          length += ProtoBuffer::VarintSize<T, Signed>(GetAt(i));
        }
        if (absl::Status status =
                buffer.SerializeLengthDelimitedHeader(Number(), length);
            !status.ok()) {
          return status;
        }
        for (size_t i = 0; i < sz; i++) {
          if (absl::Status status =
                  buffer.SerializeRawVarint<T, Signed>(GetAt(i));
              !status.ok()) {
            return status;
          }
        }
        return absl::OkStatus();
      }
    }
    if constexpr (FixedSize) {
      for (size_t i = 0; i < sz; i++) {
        if (absl::Status status = buffer.SerializeFixed<T>(Number(), GetAt(i));
            !status.ok()) {
          return status;
        }
      }
    } else {
      for (size_t i = 0; i < sz; i++) {
        if (absl::Status status =
                buffer.SerializeVarint<T, Signed>(Number(), GetAt(i));
            !status.ok()) {
          return status;
        }
      }
    }
    return absl::OkStatus();
  }

  // Between BeginDeserialize() and FinalizeDeserialize() the array holds
  // the elements parsed so far.
  absl::Status Deserialize(ProtoBuffer& buffer) {
    size_t parsed = NumElements();
    if constexpr (Packed) {
      absl::StatusOr<absl::Span<char>> payload =
          buffer.DeserializeLengthDelimited();
      if (!payload.ok()) {
        return payload.status();
      }
      if constexpr (FixedSize) {
        if (payload->size() % sizeof(T) != 0) {
          return absl::InvalidArgumentError(
              "Packed fixed-width array field has a partial element");
        }
      }
      size_t count = payload->size() / sizeof(T);
      if (parsed + count > N) {
        return absl::InvalidArgumentError("array_size overflow");
      }
      if constexpr (FixedSize) {
        if (!EnsureStorage(N)) {
          return internal::AllocationFailedError();
        }
        T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
        if (base == nullptr) {
          return internal::AllocationFailedError();
        }
        memcpy(base + parsed, payload->data(), payload->size());
        parsed += count;
        SetActiveCount(parsed);
      } else {
        ProtoBuffer sub_buffer(*payload);
        while (!sub_buffer.Eof()) {
          if (parsed >= N) {
            return absl::InvalidArgumentError("array_size overflow");
          }
          absl::StatusOr<T> v = sub_buffer.DeserializeVarint<T, Signed>();
          if (!v.ok()) {
            return v.status();
          }
          if (!EnsureStorage(parsed + 1)) {
            return internal::AllocationFailedError();
          }
          T* slot = GetRuntime()->template ToAddress<T>(BaseOffset());
          if (slot == nullptr) {
            return internal::AllocationFailedError();
          }
          slot[parsed] = *v;
          parsed++;
          SetActiveCount(parsed);
        }
      }
    } else {
      if constexpr (FixedSize) {
        if (parsed >= N) {
          return absl::InvalidArgumentError("array_size overflow");
        }
        absl::StatusOr<T> v = buffer.DeserializeFixed<T>();
        if (!v.ok()) {
          return v.status();
        }
        if (!EnsureStorage(parsed + 1)) {
          return internal::AllocationFailedError();
        }
        T* slot = GetRuntime()->template ToAddress<T>(BaseOffset());
        if (slot == nullptr) {
          return internal::AllocationFailedError();
        }
        slot[parsed] = *v;
        parsed++;
        SetActiveCount(parsed);
      } else {
        if (parsed >= N) {
          return absl::InvalidArgumentError("array_size overflow");
        }
        absl::StatusOr<T> v = buffer.DeserializeVarint<T, Signed>();
        if (!v.ok()) {
          return v.status();
        }
        if (!EnsureStorage(parsed + 1)) {
          return internal::AllocationFailedError();
        }
        T* slot = GetRuntime()->template ToAddress<T>(BaseOffset());
        if (slot == nullptr) {
          return internal::AllocationFailedError();
        }
        slot[parsed] = *v;
        parsed++;
        SetActiveCount(parsed);
      }
    }
    return absl::OkStatus();
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    for (size_t i = 0; i < N; i++) {
      (*this)[i] = other.Get(i);
    }
  }
  friend FieldIterator<PrimitiveArrayField, T>;
  friend FieldIterator<PrimitiveArrayField, const T>;

  ::phaser::BufferOffset BaseOffset() const {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  ::phaser::VectorHeader* Header(uint32_t offset) const {
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + offset);
  }

  size_t NumElements() const {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->ClampElementCount(hdr->data, hdr->num_elements,
                                           sizeof(T));
  }

  void SetActiveCount(size_t count) {
    ::phaser::VectorHeader* hdr = Header(Offset());
    hdr->num_elements = static_cast<uint32_t>(count);
  }

  // False, leaving the array unchanged, if the buffer has no space.
  bool EnsureStorage(size_t count) {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr ||
        !::phaser::PayloadBuffer::VectorReserve<T>(GetBufferAddr(), hdr,
                                                   count)) {
      return false;
    }
    hdr = Header(Offset());
    if (hdr == nullptr ||
        !::phaser::PayloadBuffer::VectorResize<T>(GetBufferAddr(), hdr,
                                                  count)) {
      return false;
    }
    return GetRuntime()->template ToAddress<T>(BaseOffset()) != nullptr;
  }

  void EnsureExtent() {
    if (!IsMutable()) {
      return;
    }
    const size_t current = NumElements();
    if (current >= N) {
      return;
    }
    if (!EnsureStorage(N)) {
      return;
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return;
    }
    for (size_t i = current; i < N; i++) {
      base[i] = T{};
    }
    SetActiveCount(N);
  }

  T GetAt(size_t index) const {
    if (index >= N) {
      return T{};
    }
    const size_t count = NumElements();
    if (index >= count) {
      return T{};
    }
    const T* base = StoragePointer();
    if (base == nullptr) {
      return T{};
    }
    return base[index];
  }

  const T* StoragePointer() const {
    if (BaseOffset() == 0) {
      return nullptr;
    }
    return GetRuntime()->template ToAddress<const T>(BaseOffset());
  }

  size_t ActiveElementCount() const {
    size_t count = NumElements();
    if (count > N) {
      return N;
    }
    return count;
  }

  bool IsMutable() const { return HasMutablePayload(GetRuntime()); }
};

template <typename Enum, size_t N, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser, bool Packed = true,
          typename Loc = StoredFieldLoc>
class EnumArrayField : public FieldBase<Loc> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  static constexpr size_t kExtent = N;

  using FieldBase<Loc>::FieldBase;
  EnumArrayField() = default;
  EnumArrayField(const EnumArrayField&) = default;
  EnumArrayField(EnumArrayField&&) = default;

  using T = typename std::underlying_type<Enum>::type;

  // A detached value when the buffer has no space for the array, so a write
  // through the reference cannot touch the caller's storage.
  Enum& operator[](size_t index) {
    EnsureExtent();
    Enum* base = data();
    if (base == nullptr) {
      static thread_local Enum detached;
      detached = static_cast<Enum>(T{});
      return detached;
    }
    return base[index];
  }

  const Enum operator[](size_t index) const { return GetAt(index); }

  Enum& front() { return (*this)[0]; }
  const Enum front() const { return GetAt(0); }
  Enum& back() { return (*this)[N - 1]; }
  const Enum back() const { return GetAt(N - 1); }

  Enum Get(size_t index) const { return GetAt(index); }

  const Enum& ConstRefAt(size_t index) const {
    if (index >= N) {
      static const Enum kDefault = static_cast<Enum>(T{});
      return kDefault;
    }
    const size_t count = NumElements();
    if (index >= count) {
      static const Enum kDefault = static_cast<Enum>(T{});
      return kDefault;
    }
    const T* base = StoragePointer();
    if (base == nullptr) {
      static const Enum kDefault = static_cast<Enum>(T{});
      return kDefault;
    }
    return *reinterpret_cast<const Enum*>(&base[index]);
  }

  void Set(size_t index, Enum v) {
    EnsureExtent();
    GetRuntime()->template ToAddress<T>(BaseOffset())[index] = static_cast<T>(v);
  }

  std::array<Enum, N> Get() const {
    std::array<Enum, N> r;
    for (size_t i = 0; i < N; i++) {
      r[i] = GetAt(i);
    }
    return r;
  }

#define ITYPE EnumFieldIterator<EnumArrayField, value_type>
#define CTYPE FixedArrayConstIterator<EnumArrayField, value_type>
  DECLARE_FIXED_ARRAY_BITS(EnumArrayField, Enum, ITYPE, CTYPE, T, N)
#undef ITYPE
#undef CTYPE

  void BeginDeserialize() {
    ::phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(), Header(Offset()));
  }

  absl::Status FinalizeDeserialize() {
    EnsureExtent();
    return absl::OkStatus();
  }

  void Clear() {
    ::phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(), Header(Offset()));
    if (!EnsureStorage(N)) {
      return;
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return;
    }
    for (size_t i = 0; i < N; i++) {
      base[i] = T{};
    }
    SetActiveCount(N);
  }
  void clear() { Clear(); }

  EnumArrayField& operator=(const EnumArrayField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  EnumArrayField& operator=(const EnumArrayField<Enum, N, Stringizer, Parser,
                                                 Packed, OtherLoc>& other) {
    Assign(other);
    return *this;
  }
  EnumArrayField& operator=(EnumArrayField&& other) noexcept {
    return operator=(static_cast<const EnumArrayField&>(other));
  }

  size_t size() const { return N; }
  size_t Size() const { return N; }
  size_t max_size() const { return N; }
  bool empty() const { return N == 0; }

  Enum* data() {
    EnsureExtent();
    return reinterpret_cast<Enum*>(GetRuntime()->template ToAddress<T>(
        BaseOffset()));
  }
  const Enum* data() const {
    return reinterpret_cast<const Enum*>(StoragePointer());
  }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  template <typename OtherLoc>
  bool operator==(const EnumArrayField<Enum, N, Stringizer, Parser, Packed,
                                       OtherLoc>& other) const {
    for (size_t i = 0; i < N; i++) {
      if (GetAt(i) != other.Get(i)) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const EnumArrayField<Enum, N, Stringizer, Parser, Packed,
                                       OtherLoc>& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t sz = ActiveElementCount();
    if (sz == 0) {
      return 0;
    }
    size_t length = 0;
    if constexpr (Packed) {
      for (size_t i = 0; i < sz; i++) {
        length += ProtoBuffer::VarintSize<T, false>(
            static_cast<T>(GetAt(i)));
      }
      return ProtoBuffer::LengthDelimitedSize(Number(), length);
    }
    for (size_t i = 0; i < sz; i++) {
      const T raw = static_cast<T>(GetAt(i));
      length += ProtoBuffer::TagSize(Number(), WireType::kVarint) +
                ProtoBuffer::VarintSize<T, false>(raw);
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t sz = ActiveElementCount();
    if (sz == 0) {
      return absl::OkStatus();
    }
    if constexpr (Packed) {
      size_t length = 0;
      for (size_t i = 0; i < sz; i++) {
        length += ProtoBuffer::VarintSize<T, false>(static_cast<T>(GetAt(i)));
      }
      if (absl::Status status =
              buffer.SerializeLengthDelimitedHeader(Number(), length);
          !status.ok()) {
        return status;
      }
      for (size_t i = 0; i < sz; i++) {
        const T raw = static_cast<T>(GetAt(i));
        if (absl::Status status = buffer.SerializeRawVarint<T, false>(raw);
            !status.ok()) {
          return status;
        }
      }
      return absl::OkStatus();
    }
    for (size_t i = 0; i < sz; i++) {
      const T raw = static_cast<T>(GetAt(i));
      if (absl::Status status =
              buffer.SerializeVarint<T, false>(Number(), raw);
          !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  // Between BeginDeserialize() and FinalizeDeserialize() the array holds
  // the elements parsed so far.
  absl::Status Deserialize(ProtoBuffer& buffer) {
    size_t parsed = NumElements();
    if constexpr (Packed) {
      absl::StatusOr<absl::Span<char>> data =
          buffer.DeserializeLengthDelimited();
      if (!data.ok()) {
        return data.status();
      }
      ProtoBuffer sub_buffer(*data);
      while (!sub_buffer.Eof()) {
        if (parsed >= N) {
          return absl::InvalidArgumentError("array_size overflow");
        }
        absl::StatusOr<T> v = sub_buffer.DeserializeVarint<T, false>();
        if (!v.ok()) {
          return v.status();
        }
        if (!EnsureStorage(parsed + 1)) {
          return internal::AllocationFailedError();
        }
        T* slot = GetRuntime()->template ToAddress<T>(BaseOffset());
        if (slot == nullptr) {
          return internal::AllocationFailedError();
        }
        slot[parsed] = *v;
        parsed++;
        SetActiveCount(parsed);
      }
    } else {
      if (parsed >= N) {
        return absl::InvalidArgumentError("array_size overflow");
      }
      absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
      if (!v.ok()) {
        return v.status();
      }
      if (!EnsureStorage(parsed + 1)) {
        return internal::AllocationFailedError();
      }
      T* slot = GetRuntime()->template ToAddress<T>(BaseOffset());
      if (slot == nullptr) {
        return internal::AllocationFailedError();
      }
      slot[parsed] = *v;
      parsed++;
      SetActiveCount(parsed);
    }
    return absl::OkStatus();
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    for (size_t i = 0; i < N; i++) {
      (*this)[i] = other.Get(i);
    }
  }
  friend EnumFieldIterator<EnumArrayField, Enum>;
  friend EnumFieldIterator<EnumArrayField, const Enum>;
  friend FieldIterator<EnumArrayField, Enum>;
  friend FieldIterator<EnumArrayField, const Enum>;

  ::phaser::BufferOffset BaseOffset() const {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  ::phaser::VectorHeader* Header(uint32_t offset) const {
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + offset);
  }

  size_t NumElements() const {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->ClampElementCount(hdr->data, hdr->num_elements,
                                           sizeof(T));
  }

  void SetActiveCount(size_t count) {
    ::phaser::VectorHeader* hdr = Header(Offset());
    hdr->num_elements = static_cast<uint32_t>(count);
  }

  // False, leaving the array unchanged, if the buffer has no space.
  bool EnsureStorage(size_t count) {
    ::phaser::VectorHeader* hdr = Header(Offset());
    if (hdr == nullptr ||
        !::phaser::PayloadBuffer::VectorReserve<T>(GetBufferAddr(), hdr,
                                                   count)) {
      return false;
    }
    hdr = Header(Offset());
    if (hdr == nullptr ||
        !::phaser::PayloadBuffer::VectorResize<T>(GetBufferAddr(), hdr,
                                                  count)) {
      return false;
    }
    return GetRuntime()->template ToAddress<T>(BaseOffset()) != nullptr;
  }

  void EnsureExtent() {
    if (!IsMutable()) {
      return;
    }
    const size_t current = NumElements();
    if (current >= N) {
      return;
    }
    if (!EnsureStorage(N)) {
      return;
    }
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return;
    }
    for (size_t i = current; i < N; i++) {
      base[i] = T{};
    }
    SetActiveCount(N);
  }

  Enum GetAt(size_t index) const {
    if (index >= N) {
      return static_cast<Enum>(T{});
    }
    const size_t count = NumElements();
    if (index >= count) {
      return static_cast<Enum>(T{});
    }
    const T* base = StoragePointer();
    if (base == nullptr) {
      return static_cast<Enum>(T{});
    }
    return static_cast<Enum>(base[index]);
  }

  const T* StoragePointer() const {
    if (BaseOffset() == 0) {
      return nullptr;
    }
    return GetRuntime()->template ToAddress<const T>(BaseOffset());
  }

  size_t ActiveElementCount() const {
    size_t count = NumElements();
    if (count > N) {
      return N;
    }
    return count;
  }

  bool IsMutable() const { return HasMutablePayload(GetRuntime()); }
};

template <typename T, size_t N, typename Loc = StoredFieldLoc>
class MessageArrayField : public MessageVectorField<T, Loc> {
  using Vector = MessageVectorField<T, Loc>;

 public:
  static constexpr size_t kExtent = N;

  using Vector::Vector;
  MessageArrayField() = default;
  MessageArrayField(const MessageArrayField&) = default;
  MessageArrayField(MessageArrayField&&) = default;

  T operator[](size_t index) const {
    return Vector::operator[](static_cast<int>(index));
  }

  T operator[](size_t index) { return MutableObject(index); }

  T front() { return (*this)[0]; }
  T front() const { return (*this)[0]; }
  T back() { return (*this)[N - 1]; }
  T back() const { return (*this)[N - 1]; }

  using typename Vector::const_iterator;
  using typename Vector::const_reverse_iterator;
  using typename Vector::iterator;
  using typename Vector::reverse_iterator;

  iterator begin() {
    EnsureExtent();
    return Vector::begin();
  }
  iterator end() {
    EnsureExtent();
    return Vector::end();
  }
  const_iterator begin() const { return Vector::begin(); }
  const_iterator end() const { return Vector::end(); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  reverse_iterator rbegin() {
    EnsureExtent();
    return Vector::rbegin();
  }
  reverse_iterator rend() {
    EnsureExtent();
    return Vector::rend();
  }
  const_reverse_iterator rbegin() const { return Vector::rbegin(); }
  const_reverse_iterator rend() const { return Vector::rend(); }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  void BeginDeserialize() { Vector::Clear(); }

  absl::Status FinalizeDeserialize() {
    while (Vector::size() < N) {
      Vector::Add();
    }
    return absl::OkStatus();
  }

  void Clear() {
    if (!HasMutablePayload(Vector::GetRuntime())) {
      return;
    }
    Vector::Clear();
    for (size_t i = 0; i < N; i++) {
      Vector::Add();
    }
  }
  void clear() { Clear(); }

  MessageArrayField& operator=(const MessageArrayField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  template <typename OtherLoc>
  MessageArrayField& operator=(const MessageArrayField<T, N, OtherLoc>& other) {
    Assign(other);
    return *this;
  }
  MessageArrayField& operator=(MessageArrayField&& other) noexcept {
    return operator=(static_cast<const MessageArrayField&>(other));
  }

  size_t size() const { return N; }
  size_t Size() const { return N; }
  size_t max_size() const { return N; }
  bool empty() const { return N == 0; }

  T Get(size_t index) const { return (*this)[index]; }

  T Mutable(size_t index) { return MutableObject(index); }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Vector::BinaryEndOffset();
  }
  ::phaser::BufferOffset BinaryOffset() const { return Vector::BinaryOffset(); }

  template <typename OtherLoc>
  bool operator==(const MessageArrayField<T, N, OtherLoc>& other) const {
    for (size_t i = 0; i < N; i++) {
      if (Get(i) != other.Get(i)) {
        return false;
      }
    }
    return true;
  }
  template <typename OtherLoc>
  bool operator!=(const MessageArrayField<T, N, OtherLoc>& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t length = 0;
    for (size_t i = 0; i < parsed_count_for_serialize(); i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          this->Number(), (*this)[i].SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t count = parsed_count_for_serialize();
    for (size_t i = 0; i < count; i++) {
      if (absl::Status status = buffer.SerializeLengthDelimitedHeader(
              this->Number(), (*this)[i].SerializedSize());
          !status.ok()) {
        return status;
      }
      if (absl::Status status = (*this)[i].Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  // Between BeginDeserialize() and FinalizeDeserialize() the array holds
  // the elements parsed so far.
  absl::Status Deserialize(ProtoBuffer& buffer) {
    if (Vector::size() >= N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    absl::StatusOr<absl::Span<char>> v = buffer.DeserializeLengthDelimited();
    if (!v.ok()) {
      return v.status();
    }
    T msg = Vector::Add();
    ProtoBuffer msg_buffer(*v);
    return msg.Deserialize(msg_buffer);
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    Clear();
    for (size_t i = 0; i < N; i++) {
      if (absl::Status s = MutableObject(i).CloneFrom(other.Get(i)); !s.ok()) {
        return;
      }
    }
  }

  T MutableObject(size_t index) {
    if (!HasMutablePayload(Vector::GetRuntime())) {
      return Vector::operator[](static_cast<int>(index));
    }
    EnsureExtent();
    if (index >= Vector::size()) {
      return Vector::Mutable(index);
    }
    return Vector::Mutable(index);
  }

  void EnsureExtent() {
    if (!HasMutablePayload(Vector::GetRuntime())) {
      return;
    }
    while (Vector::size() < N) {
      const size_t before = Vector::size();
      Vector::Add();
      if (Vector::size() == before) {
        return;
      }
    }
  }

  size_t parsed_count_for_serialize() const {
    size_t count = Vector::size();
    if (count == 0) {
      return 0;
    }
    if (count > N) {
      return N;
    }
    return count;
  }
};

// The array holds a handle for each string, so it holds where it is.
template <size_t N>
class StringArrayField : public FieldBase<StoredFieldLoc> {
 public:
  static constexpr size_t kExtent = N;

  using FieldBase::FieldBase;
  StringArrayField() = default;
  StringArrayField(const StringArrayField&) = default;
  StringArrayField(StringArrayField&& other) noexcept
      : FieldBase(std::move(other)), parsed_count_(other.parsed_count_) {}

  std::string_view operator[](size_t index) const { return Get(index); }

  // The strings are made on the first change, which can move the buffer, so
  // a string from the buffer is given to one with Set().
  NonEmbeddedStringField operator[](size_t index) {
    EnsureMutableExtent();
    return ConstSlot(index);
  }

  std::string_view front() const { return Get(0); }
  NonEmbeddedStringField front() { return (*this)[0]; }
  std::string_view back() const { return Get(N - 1); }
  NonEmbeddedStringField back() { return (*this)[N - 1]; }

  using value_type = NonEmbeddedStringField;
  using reference = NonEmbeddedStringField;
  using const_reference = std::string_view;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  struct ConstIterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::string_view;
    using difference_type = ptrdiff_t;
    using pointer = void;
    using reference = std::string_view;

    const StringArrayField* field = nullptr;
    size_t index = 0;

    ConstIterator() = default;
    ConstIterator(const StringArrayField* f, size_t i) : field(f), index(i) {}

    ConstIterator& operator++() {
      ++index;
      return *this;
    }
    ConstIterator& operator--() {
      --index;
      return *this;
    }
    std::string_view operator*() const { return field->Get(index); }
    bool operator==(const ConstIterator& it) const {
      return field == it.field && index == it.index;
    }
    bool operator!=(const ConstIterator& it) const { return !operator==(it); }
  };
  struct ConstReverseIterator {
    const StringArrayField* field = nullptr;
    size_t index = 0;

    ConstReverseIterator() = default;
    ConstReverseIterator(const StringArrayField* f, size_t i)
        : field(f), index(i) {}

    ConstReverseIterator& operator++() {
      if (index == static_cast<size_t>(-1)) {
        return *this;
      }
      --index;
      return *this;
    }
    ConstReverseIterator& operator--() {
      ++index;
      return *this;
    }
    std::string_view operator*() const { return field->Get(index); }
    bool operator==(const ConstReverseIterator& it) const {
      return field == it.field && index == it.index;
    }
    bool operator!=(const ConstReverseIterator& it) const {
      return !operator==(it);
    }
  };
  using iterator = ConstIterator;
  using const_iterator = ConstIterator;
  using reverse_iterator = ConstReverseIterator;
  using const_reverse_iterator = ConstReverseIterator;

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, N); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, N); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  reverse_iterator rbegin() {
    return reverse_iterator(this, N == 0 ? static_cast<size_t>(-1) : N - 1);
  }
  reverse_iterator rend() {
    return reverse_iterator(this, static_cast<size_t>(-1));
  }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(
        this, N == 0 ? static_cast<size_t>(-1) : N - 1);
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(this, static_cast<size_t>(-1));
  }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  void BeginDeserialize() {
    if (NumElements() > 0) {
      ClearParsedElements();
    } else {
      ResetSlots();
    }
    parsed_count_ = 0;
  }

  absl::Status FinalizeDeserialize() {
    if (parsed_count_ > N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    EnsureMutableExtent();
    parsed_count_ = 0;
    return absl::OkStatus();
  }

  void Clear() {
    if (!IsMutable()) {
      return;
    }
    parsed_count_ = 0;
    if (NumElements() > 0) {
      ClearParsedElements();
    } else {
      ResetSlots();
    }
    EnsureMutableExtent();
  }
  void clear() { Clear(); }

  StringArrayField& operator=(const StringArrayField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    for (size_t i = 0; i < N; i++) {
      (*this)[i] = other.ConstSlot(i);
    }
    return *this;
  }
  StringArrayField& operator=(StringArrayField&& other) noexcept {
    return operator=(static_cast<const StringArrayField&>(other));
  }

  size_t size() const { return N; }
  size_t Size() const { return N; }
  size_t max_size() const { return N; }
  bool empty() const { return N == 0; }

  NonEmbeddedStringField* data() = delete;
  const NonEmbeddedStringField* data() const = delete;

  std::string_view Get(size_t index) const {
    const NonEmbeddedStringField& slot = ConstSlot(index);
    if (slot.IsPlaceholder()) {
      return {};
    }
    return slot.Get();
  }

  template <typename Str>
  void Set(size_t index, Str s) {
    const ::phaser::StringSource source(*GetBufferAddr(), std::string_view(s));
    (*this)[index].Set(source.In(*GetBufferAddr()));
  }

  ::phaser::BufferOffset BinaryEndOffset() const {
    return Offset() + sizeof(::phaser::VectorHeader);
  }
  ::phaser::BufferOffset BinaryOffset() const { return Offset(); }

  bool operator==(const StringArrayField& other) const {
    for (size_t i = 0; i < N; i++) {
      if (ConstSlot(i).Get() != other.ConstSlot(i).Get()) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const StringArrayField& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t length = 0;
    size_t count = parsed_count_for_serialize();
    for (size_t i = 0; i < count; i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Number(), ConstSlot(i).SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t count = parsed_count_for_serialize();
    for (size_t i = 0; i < count; i++) {
      const NonEmbeddedStringField& slot = ConstSlot(i);
      if (absl::Status status = buffer.SerializeLengthDelimited(
              Number(), slot.data(), slot.size());
          !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    if (parsed_count_ >= N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    if (!EnsureStorage(parsed_count_ + 1)) {
      return internal::AllocationFailedError();
    }
    absl::StatusOr<std::string_view> v = buffer.DeserializeString();
    if (!v.ok()) {
      return v.status();
    }
    void* str_hdr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), sizeof(::phaser::StringHeader));
    if (str_hdr == nullptr) {
      return internal::AllocationFailedError();
    }
    ::phaser::BufferOffset hdr_offset = GetRuntime()->ToOffset(str_hdr);
    if (::phaser::PayloadBuffer::SetString(GetBufferAddr(), *v, hdr_offset) ==
        nullptr) {
      return internal::AllocationFailedError();
    }
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr) {
      return internal::AllocationFailedError();
    }
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data == nullptr) {
      return internal::AllocationFailedError();
    }
    data[parsed_count_] = hdr_offset;
    hdr = Header();
    data = GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    strings_[parsed_count_] =
        NonEmbeddedStringField(Parent(), data[parsed_count_]);
    parsed_count_++;
    SetActiveCount(parsed_count_);
    return absl::OkStatus();
  }

 private:
  ::phaser::VectorHeader* Header() const {
    return GetRuntime()->template ToAddress<::phaser::VectorHeader>(
        GetMessageBinaryStart() + Offset());
  }

  size_t NumElements() const {
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr) {
      return 0;
    }
    return GetRuntime()->ClampElementCount(hdr->data, hdr->num_elements,
                                           sizeof(::phaser::BufferOffset));
  }

  void SetActiveCount(size_t count) {
    Header()->num_elements = static_cast<uint32_t>(count);
  }

  // The slots it adds hold no string.  False if the buffer has no space.
  bool EnsureStorage(size_t count) {
    const size_t current = NumElements();
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr ||
        !::phaser::PayloadBuffer::VectorReserve<::phaser::BufferOffset>(
            GetBufferAddr(), hdr, count)) {
      return false;
    }
    hdr = Header();
    if (hdr == nullptr ||
        !::phaser::PayloadBuffer::VectorResize<::phaser::BufferOffset>(
            GetBufferAddr(), hdr, count)) {
      return false;
    }
    if (count > current) {
      hdr = Header();
      if (hdr == nullptr) {
        return false;
      }
      ::phaser::BufferOffset* data =
          GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
      if (data == nullptr) {
        return false;
      }
      memset(data + current, 0, (count - current) * sizeof(*data));
    }
    return true;
  }

  size_t parsed_count_for_serialize() const {
    size_t count = NumElements();
    if (count == 0) {
      return 0;
    }
    if (count > N) {
      return N;
    }
    return count;
  }

  void ClearParsedElements() {
    for (auto& s : strings_) {
      if (!s.IsPlaceholder()) {
        s.Clear();
      }
    }
    ::phaser::PayloadBuffer::VectorClear<::phaser::BufferOffset>(
        GetBufferAddr(), Header());
    ResetSlots();
  }

  void ResetSlots() {
    for (auto& string : strings_) {
      string = NonEmbeddedStringField();
    }
  }

  void EnsureMutableExtent() {
    if (!IsMutable()) {
      return;
    }
    const size_t current = NumElements();
    if (current < N) {
      if (!EnsureStorage(N)) {
        return;
      }
      for (size_t i = current; i < N; i++) {
        AllocateStringSlot(i);
      }
      SetActiveCount(N);
    }
    BindExistingSlots(0, N);
  }

  const NonEmbeddedStringField& ConstSlot(size_t index) const {
    if (index >= N) {
      return empty_;
    }
    if (FindFieldOffset() < 0) {
      return empty_;
    }
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr || hdr->data == 0) {
      return empty_;
    }
    const size_t count = GetRuntime()->ClampElementCount(
        hdr->data, hdr->num_elements, sizeof(::phaser::BufferOffset));
    if (index >= count) {
      return empty_;
    }
    if (!strings_[index].IsPlaceholder()) {
      return strings_[index];
    }
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data == nullptr || data[index] == 0) {
      return empty_;
    }
    auto* self = const_cast<StringArrayField*>(this);
    self->strings_[index] = NonEmbeddedStringField(Parent(), data[index]);
    return strings_[index];
  }

  void AllocateStringSlot(size_t index) {
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr) {
      return;
    }
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data == nullptr) {
      return;
    }
    if (data[index] == 0) {
      void* str_hdr = ::phaser::PayloadBuffer::Allocate(
          GetBufferAddr(), sizeof(::phaser::StringHeader));
      if (str_hdr == nullptr) {
        return;
      }
      hdr = Header();
      if (hdr == nullptr) {
        return;
      }
      data = GetRuntime()->template ToAddress<::phaser::BufferOffset>(
          hdr->data);
      if (data == nullptr) {
        return;
      }
      data[index] = GetRuntime()->ToOffset(str_hdr);
    }
    strings_[index] = NonEmbeddedStringField(Parent(), data[index]);
  }

  void BindExistingSlots(size_t start, size_t end) const {
    if (start >= end || end > N) {
      return;
    }
    ::phaser::VectorHeader* hdr = Header();
    if (hdr == nullptr || hdr->data == 0) {
      return;
    }
    const size_t count = GetRuntime()->ClampElementCount(
        hdr->data, hdr->num_elements, sizeof(::phaser::BufferOffset));
    if (start >= count) {
      return;
    }
    if (end > count) {
      end = count;
    }
    ::phaser::BufferOffset* data =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(hdr->data);
    if (data == nullptr) {
      return;
    }
    auto* self = const_cast<StringArrayField*>(this);
    for (size_t i = start; i < end; i++) {
      if (data[i] == 0) {
        continue;
      }
      if (self->strings_[i].IsPlaceholder()) {
        self->strings_[i] = NonEmbeddedStringField(Parent(), data[i]);
      }
    }
  }

  bool IsMutable() const { return HasMutablePayload(GetRuntime()); }

  mutable std::array<NonEmbeddedStringField, N> strings_;
  size_t parsed_count_ = 0;
  NonEmbeddedStringField empty_;
};

#undef DECLARE_FIXED_ARRAY_BITS

namespace internal {
template <typename T, size_t N, bool FixedSize, bool Signed, bool Packed,
          typename L>
struct FieldRelease<PrimitiveArrayField<T, N, FixedSize, Signed, Packed, L>>
    : VectorDataRelease {};

template <typename Enum, size_t N, typename Stringizer, typename Parser,
          bool Packed, typename L>
struct FieldRelease<EnumArrayField<Enum, N, Stringizer, Parser, Packed, L>>
    : VectorDataRelease {};

template <typename T, size_t N, typename L>
struct FieldRelease<MessageArrayField<T, N, L>> : MessageVectorRelease<T> {};

// Clearing an array makes its N elements again.
template <typename T, size_t N, bool FixedSize, bool Signed, bool Packed,
          typename L>
struct FieldResetClear<PrimitiveArrayField<T, N, FixedSize, Signed, Packed, L>>
    : ResetClearAfter {};

template <typename Enum, size_t N, typename Stringizer, typename Parser,
          bool Packed, typename L>
struct FieldResetClear<EnumArrayField<Enum, N, Stringizer, Parser, Packed, L>>
    : ResetClearAfter {};

template <typename T, size_t N, typename L>
struct FieldResetClear<MessageArrayField<T, N, L>> : ResetClearAfter {};

template <size_t N>
struct FieldResetClear<StringArrayField<N>> : ResetClearAfter {};
}  // namespace internal

}  // namespace phaser
