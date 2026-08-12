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
#include "toolbelt/payload_buffer.h"

namespace phaser {

inline bool HasMutablePayload(
    const std::shared_ptr<MessageRuntime>& runtime) {
  return runtime != nullptr && runtime->IsMutable();
}

template <typename Field, typename Value>
struct FixedArrayConstIterator {
  FixedArrayConstIterator(const Field* f, size_t idx, bool reverse = false)
      : field(f), index(idx), reverse(reverse) {}

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
        this, BaseOffset() + static_cast<::toolbelt::BufferOffset>(extent *     \
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
        this, BaseOffset() + static_cast<::toolbelt::BufferOffset>(extent *     \
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
          bool Packed = true>
class PrimitiveArrayField : public Field {
 public:
  static constexpr size_t kExtent = N;

  PrimitiveArrayField() = default;
  explicit PrimitiveArrayField(uint32_t source_offset,
                               uint32_t relative_binary_offset, int id,
                               int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  PrimitiveArrayField(const PrimitiveArrayField&) = default;
  PrimitiveArrayField(PrimitiveArrayField&&) = default;

  T& operator[](size_t index) {
    EnsureExtent();
    return data()[index];
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
    ::toolbelt::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                              Header(relative_binary_offset_));
    parsed_count_ = 0;
  }

  absl::Status FinalizeDeserialize() {
    if (parsed_count_ > N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    EnsureExtent();
    parsed_count_ = 0;
    return absl::OkStatus();
  }

  void Clear() {
    ::toolbelt::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                              Header(relative_binary_offset_));
    parsed_count_ = 0;
    EnsureStorage(N);
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    for (size_t i = 0; i < N; i++) {
      base[i] = T{};
    }
    SetActiveCount(N);
    this->ResetFieldCache();
  }
  void clear() { Clear(); }

  PrimitiveArrayField& operator=(const PrimitiveArrayField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    for (size_t i = 0; i < N; i++) {
      (*this)[i] = other.GetAt(i);
    }
    this->ResetFieldCache();
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

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

  bool operator==(const PrimitiveArrayField& other) const {
    for (size_t i = 0; i < N; i++) {
      if (GetAt(i) != other.GetAt(i)) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const PrimitiveArrayField& other) const {
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

  absl::Status Deserialize(ProtoBuffer& buffer) {
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
      if (parsed_count_ + count > N) {
        return absl::InvalidArgumentError("array_size overflow");
      }
      if constexpr (FixedSize) {
        EnsureStorage(N);
        T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
        memcpy(base + parsed_count_, payload->data(), payload->size());
        parsed_count_ += count;
        SetActiveCount(parsed_count_);
      } else {
        ProtoBuffer sub_buffer(*payload);
        while (!sub_buffer.Eof()) {
          if (parsed_count_ >= N) {
            return absl::InvalidArgumentError("array_size overflow");
          }
          absl::StatusOr<T> v = sub_buffer.DeserializeVarint<T, Signed>();
          if (!v.ok()) {
            return v.status();
          }
          EnsureStorage(parsed_count_ + 1);
          GetRuntime()->template ToAddress<T>(BaseOffset())[parsed_count_] =
              *v;
          parsed_count_++;
          SetActiveCount(parsed_count_);
        }
      }
    } else {
      if constexpr (FixedSize) {
        if (parsed_count_ >= N) {
          return absl::InvalidArgumentError("array_size overflow");
        }
        absl::StatusOr<T> v = buffer.DeserializeFixed<T>();
        if (!v.ok()) {
          return v.status();
        }
        EnsureStorage(parsed_count_ + 1);
        GetRuntime()->template ToAddress<T>(BaseOffset())[parsed_count_] = *v;
        parsed_count_++;
        SetActiveCount(parsed_count_);
      } else {
        if (parsed_count_ >= N) {
          return absl::InvalidArgumentError("array_size overflow");
        }
        absl::StatusOr<T> v = buffer.DeserializeVarint<T, Signed>();
        if (!v.ok()) {
          return v.status();
        }
        EnsureStorage(parsed_count_ + 1);
        GetRuntime()->template ToAddress<T>(BaseOffset())[parsed_count_] = *v;
        parsed_count_++;
        SetActiveCount(parsed_count_);
      }
    }
    return absl::OkStatus();
  }

 private:
  friend FieldIterator<PrimitiveArrayField, T>;
  friend FieldIterator<PrimitiveArrayField, const T>;

  ::toolbelt::BufferOffset BaseOffset() const {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  toolbelt::VectorHeader* Header(uint32_t offset) const {
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) + offset);
  }

  size_t NumElements() const {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->num_elements;
  }

  void SetActiveCount(size_t count) {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    hdr->num_elements = static_cast<uint32_t>(count);
  }

  void EnsureStorage(size_t count) {
    ::toolbelt::PayloadBuffer::VectorReserve<T>(GetBufferAddr(),
                                                Header(relative_binary_offset_),
                                                count);
    ::toolbelt::PayloadBuffer::VectorResize<T>(GetBufferAddr(),
                                                 Header(relative_binary_offset_),
                                                 count);
  }

  void EnsureExtent() {
    if (!IsMutable()) {
      return;
    }
    const size_t current = NumElements();
    if (current >= N) {
      return;
    }
    EnsureStorage(N);
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

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  size_t parsed_count_ = 0;
};

template <typename Enum, size_t N,
          typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser, bool Packed = true>
class EnumArrayField : public Field {
 public:
  static constexpr size_t kExtent = N;

  EnumArrayField() = default;
  explicit EnumArrayField(uint32_t source_offset,
                          uint32_t relative_binary_offset, int id, int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  EnumArrayField(const EnumArrayField&) = default;
  EnumArrayField(EnumArrayField&&) = default;

  using T = typename std::underlying_type<Enum>::type;

  Enum& operator[](size_t index) {
    EnsureExtent();
    return *reinterpret_cast<Enum*>(&data()[index]);
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
    ::toolbelt::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                              Header(relative_binary_offset_));
    parsed_count_ = 0;
  }

  absl::Status FinalizeDeserialize() {
    if (parsed_count_ > N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    EnsureExtent();
    parsed_count_ = 0;
    return absl::OkStatus();
  }

  void Clear() {
    ::toolbelt::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                              Header(relative_binary_offset_));
    parsed_count_ = 0;
    EnsureStorage(N);
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    for (size_t i = 0; i < N; i++) {
      base[i] = T{};
    }
    SetActiveCount(N);
    this->ResetFieldCache();
  }
  void clear() { Clear(); }

  EnumArrayField& operator=(const EnumArrayField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    for (size_t i = 0; i < N; i++) {
      (*this)[i] = other.GetAt(i);
    }
    this->ResetFieldCache();
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

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

  bool operator==(const EnumArrayField& other) const {
    for (size_t i = 0; i < N; i++) {
      if (GetAt(i) != other.GetAt(i)) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const EnumArrayField& other) const {
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

  absl::Status Deserialize(ProtoBuffer& buffer) {
    if constexpr (Packed) {
      absl::StatusOr<absl::Span<char>> data =
          buffer.DeserializeLengthDelimited();
      if (!data.ok()) {
        return data.status();
      }
      ProtoBuffer sub_buffer(*data);
      while (!sub_buffer.Eof()) {
        if (parsed_count_ >= N) {
          return absl::InvalidArgumentError("array_size overflow");
        }
        absl::StatusOr<T> v = sub_buffer.DeserializeVarint<T, false>();
        if (!v.ok()) {
          return v.status();
        }
        EnsureStorage(parsed_count_ + 1);
        GetRuntime()->template ToAddress<T>(BaseOffset())[parsed_count_] = *v;
        parsed_count_++;
        SetActiveCount(parsed_count_);
      }
    } else {
      if (parsed_count_ >= N) {
        return absl::InvalidArgumentError("array_size overflow");
      }
      absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
      if (!v.ok()) {
        return v.status();
      }
      EnsureStorage(parsed_count_ + 1);
      GetRuntime()->template ToAddress<T>(BaseOffset())[parsed_count_] = *v;
      parsed_count_++;
      SetActiveCount(parsed_count_);
    }
    return absl::OkStatus();
  }

 private:
  friend EnumFieldIterator<EnumArrayField, Enum>;
  friend EnumFieldIterator<EnumArrayField, const Enum>;
  friend FieldIterator<EnumArrayField, Enum>;
  friend FieldIterator<EnumArrayField, const Enum>;

  ::toolbelt::BufferOffset BaseOffset() const {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->data;
  }

  toolbelt::VectorHeader* Header(uint32_t offset) const {
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) + offset);
  }

  size_t NumElements() const {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->num_elements;
  }

  void SetActiveCount(size_t count) {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    hdr->num_elements = static_cast<uint32_t>(count);
  }

  void EnsureStorage(size_t count) {
    ::toolbelt::PayloadBuffer::VectorReserve<T>(GetBufferAddr(),
                                                Header(relative_binary_offset_),
                                                count);
    ::toolbelt::PayloadBuffer::VectorResize<T>(GetBufferAddr(),
                                               Header(relative_binary_offset_),
                                               count);
  }

  void EnsureExtent() {
    if (!IsMutable()) {
      return;
    }
    const size_t current = NumElements();
    if (current >= N) {
      return;
    }
    EnsureStorage(N);
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

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  size_t parsed_count_ = 0;
};

template <typename T, size_t N>
class MessageArrayField : public MessageVectorField<T> {
 public:
  static constexpr size_t kExtent = N;

  MessageArrayField() = default;
  explicit MessageArrayField(uint32_t source_offset,
                             uint32_t relative_binary_offset, int id,
                             int number)
      : MessageVectorField<T>(source_offset, relative_binary_offset, id,
                              number) {}
  MessageArrayField(const MessageArrayField&) = default;
  MessageArrayField(MessageArrayField&&) = default;

  T operator[](size_t index) const {
    return MessageVectorField<T>::operator[](static_cast<int>(index));
  }

  T operator[](size_t index) { return MutableObject(index); }

  T front() { return (*this)[0]; }
  T front() const { return (*this)[0]; }
  T back() { return (*this)[N - 1]; }
  T back() const { return (*this)[N - 1]; }

  using typename MessageVectorField<T>::iterator;
  using typename MessageVectorField<T>::const_iterator;
  using typename MessageVectorField<T>::reverse_iterator;
  using typename MessageVectorField<T>::const_reverse_iterator;

  iterator begin() {
    EnsureExtent();
    return MessageVectorField<T>::begin();
  }
  iterator end() {
    EnsureExtent();
    return MessageVectorField<T>::end();
  }
  const_iterator begin() const {
    return MessageVectorField<T>::begin();
  }
  const_iterator end() const {
    return MessageVectorField<T>::end();
  }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  reverse_iterator rbegin() {
    EnsureExtent();
    return MessageVectorField<T>::rbegin();
  }
  reverse_iterator rend() {
    EnsureExtent();
    return MessageVectorField<T>::rend();
  }
  const_reverse_iterator rbegin() const {
    return MessageVectorField<T>::rbegin();
  }
  const_reverse_iterator rend() const {
    return MessageVectorField<T>::rend();
  }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  void BeginDeserialize() {
    MessageVectorField<T>::Clear();
    parsed_count_ = 0;
  }

  absl::Status FinalizeDeserialize() {
    if (parsed_count_ > N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    while (MessageVectorField<T>::size() < N) {
      MessageVectorField<T>::Add();
    }
    parsed_count_ = 0;
    return absl::OkStatus();
  }

  void Clear() {
    if (!HasMutablePayload(MessageVectorField<T>::GetRuntime())) {
      return;
    }
    MessageVectorField<T>::Clear();
    parsed_count_ = 0;
    for (size_t i = 0; i < N; i++) {
      MessageVectorField<T>::Add();
    }
    this->ResetFieldCache();
  }
  void clear() { Clear(); }

  MessageArrayField& operator=(const MessageArrayField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    for (size_t i = 0; i < N; i++) {
      if (absl::Status s = MutableObject(i).CloneFrom(other.Get(i));
          !s.ok()) {
        return *this;
      }
    }
    this->ResetFieldCache();
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

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return MessageVectorField<T>::BinaryEndOffset();
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return MessageVectorField<T>::BinaryOffset();
  }

  bool operator==(const MessageArrayField& other) const {
    for (size_t i = 0; i < N; i++) {
      if (Get(i) != other.Get(i)) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const MessageArrayField& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t length = 0;
    for (size_t i = 0; i < parsed_count_for_serialize(); i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Field::Number(), (*this)[i].SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t count = parsed_count_for_serialize();
    for (size_t i = 0; i < count; i++) {
      if (absl::Status status = buffer.SerializeLengthDelimitedHeader(
              Field::Number(), (*this)[i].SerializedSize());
          !status.ok()) {
        return status;
      }
      if (absl::Status status = (*this)[i].Serialize(buffer); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    if (parsed_count_ >= N) {
      return absl::InvalidArgumentError("array_size overflow");
    }
    absl::StatusOr<absl::Span<char>> v = buffer.DeserializeLengthDelimited();
    if (!v.ok()) {
      return v.status();
    }
    T msg = parsed_count_ < MessageVectorField<T>::size()
                ? MessageVectorField<T>::Mutable(parsed_count_)
                : MessageVectorField<T>::Add();
    ProtoBuffer msg_buffer(*v);
    if (absl::Status status = msg.Deserialize(msg_buffer); !status.ok()) {
      return status;
    }
    parsed_count_++;
    return absl::OkStatus();
  }

 private:
  T MutableObject(size_t index) {
    if (!HasMutablePayload(MessageVectorField<T>::GetRuntime())) {
      return MessageVectorField<T>::operator[](static_cast<int>(index));
    }
    while (MessageVectorField<T>::size() <= index) {
      MessageVectorField<T>::Add();
    }
    while (MessageVectorField<T>::size() < N) {
      MessageVectorField<T>::Add();
    }
    return MessageVectorField<T>::Mutable(index);
  }

  void EnsureExtent() {
    if (!HasMutablePayload(MessageVectorField<T>::GetRuntime())) {
      return;
    }
    while (MessageVectorField<T>::size() < N) {
      MessageVectorField<T>::Add();
    }
  }

  size_t parsed_count_for_serialize() const {
    size_t count = MessageVectorField<T>::size();
    if (count == 0) {
      return 0;
    }
    if (count > N) {
      return N;
    }
    return count;
  }

  size_t parsed_count_ = 0;
};

template <size_t N>
class StringArrayField : public Field {
 public:
  static constexpr size_t kExtent = N;

  StringArrayField() = default;
  explicit StringArrayField(uint32_t source_offset,
                            uint32_t relative_binary_offset, int id,
                            int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  StringArrayField(const StringArrayField&) = default;
  StringArrayField(StringArrayField&& other) noexcept
      : Field(std::move(other)),
        source_offset_(other.source_offset_),
        relative_binary_offset_(other.relative_binary_offset_),
        parsed_count_(other.parsed_count_) {}

  std::string_view operator[](size_t index) const { return Get(index); }

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
    this->ResetFieldCache();
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
    this->ResetFieldCache();
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
    (*this)[index].Set(s);
  }

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

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
    EnsureStorage(parsed_count_ + 1);
    absl::StatusOr<std::string_view> v = buffer.DeserializeString();
    if (!v.ok()) {
      return v.status();
    }
    void* str_hdr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(), sizeof(toolbelt::StringHeader));
    ::toolbelt::BufferOffset hdr_offset = GetRuntime()->ToOffset(str_hdr);
    ::toolbelt::PayloadBuffer::SetString(GetBufferAddr(), *v, hdr_offset);
    toolbelt::VectorHeader* hdr = Header();
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    data[parsed_count_] = hdr_offset;
    hdr = Header();
    data = GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    strings_[parsed_count_] = NonEmbeddedStringField(
        Message::GetMessage(this, source_offset_), data[parsed_count_]);
    parsed_count_++;
    SetActiveCount(parsed_count_);
    return absl::OkStatus();
  }

 private:
  toolbelt::VectorHeader* Header() const {
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) +
        relative_binary_offset_);
  }

  size_t NumElements() const {
    toolbelt::VectorHeader* hdr = Header();
    if (hdr == nullptr) {
      return 0;
    }
    return hdr->num_elements;
  }

  void SetActiveCount(size_t count) {
    Header()->num_elements = static_cast<uint32_t>(count);
  }

  void EnsureStorage(size_t count) {
    ::toolbelt::PayloadBuffer::VectorReserve<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), count);
    ::toolbelt::PayloadBuffer::VectorResize<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), count);
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
    ::toolbelt::PayloadBuffer::VectorClear<::toolbelt::BufferOffset>(
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
      EnsureStorage(N);
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
    if (FindFieldOffset(source_offset_) < 0) {
      return empty_;
    }
    toolbelt::VectorHeader* hdr = Header();
    if (hdr == nullptr || hdr->data == 0) {
      return empty_;
    }
    const size_t count = hdr->num_elements;
    if (index >= count) {
      return empty_;
    }
    if (!strings_[index].IsPlaceholder()) {
      return strings_[index];
    }
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    if (data[index] == 0) {
      return empty_;
    }
    auto* self = const_cast<StringArrayField*>(this);
    self->strings_[index] = NonEmbeddedStringField(
        Message::GetMessage(this, source_offset_), data[index]);
    return strings_[index];
  }

  void AllocateStringSlot(size_t index) {
    toolbelt::VectorHeader* hdr = Header();
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    if (data[index] == 0) {
      void* str_hdr = ::toolbelt::PayloadBuffer::Allocate(
          GetBufferAddr(), sizeof(toolbelt::StringHeader));
      hdr = Header();
      data = GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(
          hdr->data);
      data[index] = GetRuntime()->ToOffset(str_hdr);
    }
    strings_[index] = NonEmbeddedStringField(
        Message::GetMessage(this, source_offset_), data[index]);
  }

  void BindExistingSlots(size_t start, size_t end) const {
    if (start >= end || end > N) {
      return;
    }
    toolbelt::VectorHeader* hdr = Header();
    if (hdr == nullptr || hdr->data == 0) {
      return;
    }
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    auto* self = const_cast<StringArrayField*>(this);
    for (size_t i = start; i < end; i++) {
      if (data[i] == 0) {
        continue;
      }
      if (self->strings_[i].IsPlaceholder()) {
        self->strings_[i] = NonEmbeddedStringField(
            Message::GetMessage(this, source_offset_), data[i]);
      }
    }
  }

  bool IsMutable() const { return HasMutablePayload(GetRuntime()); }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  mutable std::array<NonEmbeddedStringField, N> strings_;
  size_t parsed_count_ = 0;
  NonEmbeddedStringField empty_;
};

#undef DECLARE_FIXED_ARRAY_BITS

}  // namespace phaser
