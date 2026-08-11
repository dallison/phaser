// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Vector fields (repeated fields).

#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/wireformat.h"
#include "toolbelt/payload_buffer.h"

namespace phaser {

class ProtoBuffer;

#define DECLARE_ZERO_COPY_VECTOR_BITS(vtype, itype, ctype, utype)              \
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
  iterator begin() { return iterator(this, BaseOffset()); }                    \
  iterator end() {                                                             \
    return iterator(this,                                                      \
                    BaseOffset() + static_cast<::toolbelt::BufferOffset>(      \
                                       NumElements() * sizeof(value_type)));   \
  }                                                                            \
  const_iterator begin() const { return const_iterator(this, BaseOffset()); }  \
  const_iterator end() const {                                                 \
    return const_iterator(                                                     \
        this, BaseOffset() + static_cast<::toolbelt::BufferOffset>(            \
                                 NumElements() * sizeof(value_type)));         \
  }                                                                            \
  const_iterator cbegin() const { return const_iterator(this, BaseOffset()); } \
  const_iterator cend() const {                                                \
    return const_iterator(                                                     \
        this, BaseOffset() + static_cast<::toolbelt::BufferOffset>(            \
                                 NumElements() * sizeof(value_type)));         \
  }                                                                            \
                                                                               \
  reverse_iterator rbegin() {                                                  \
    return reverse_iterator(this, BaseOffset(), true);                         \
  }                                                                            \
  reverse_iterator rend() {                                                    \
    return reverse_iterator(                                                   \
        this,                                                                  \
        BaseOffset() + static_cast<::toolbelt::BufferOffset>(                  \
                           NumElements() * sizeof(value_type)),                \
        true);                                                                 \
  }                                                                            \
  const_reverse_iterator rbegin() const {                                      \
    return const_reverse_iterator(this, BaseOffset(), true);                   \
  }                                                                            \
  const_reverse_iterator rend() const {                                        \
    return const_reverse_iterator(                                             \
        this,                                                                  \
        BaseOffset() + static_cast<::toolbelt::BufferOffset>(                  \
                           NumElements() * sizeof(value_type)),                \
        true);                                                                 \
  }                                                                            \
  const_reverse_iterator crbegin() const {                                     \
    return const_reverse_iterator(this, BaseOffset(), true);                   \
  }                                                                            \
  const_reverse_iterator crend() const {                                       \
    return const_reverse_iterator(                                             \
        this,                                                                  \
        BaseOffset() + static_cast<::toolbelt::BufferOffset>(                  \
                           NumElements() * sizeof(value_type)),                \
        true);                                                                 \
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
// The binary message contains a toolbelt::VectorHeader at the binary offset.
// This contains the number of elements and the base offset for the data.
template <typename T, bool FixedSize = false, bool Signed = false,
          bool Packed = true>
class PrimitiveVectorField : public Field {
 public:
  PrimitiveVectorField() = default;
  explicit PrimitiveVectorField(uint32_t source_offset,
                                uint32_t relative_binary_offset, int id,
                                int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  PrimitiveVectorField(const PrimitiveVectorField&) = default;
  PrimitiveVectorField(PrimitiveVectorField&&) = default;

  T& operator[](int index) {
    static T empty;
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return empty;
    }
    return base[index];
  }

  T operator[](int index) const {
    static T empty;
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return empty;
    }
    return base[index];
  }

  T front() { return (*this)[0]; }
  const T front() const { return (*this)[0]; }
  T back() { return (*this)[size() - 1]; }
  const T back() const { return (*this)[size() - 1]; }

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
      v.push_back((*this)[i]);
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
    ::toolbelt::PayloadBuffer::VectorPush<T>(
        GetBufferAddr(), Header(relative_binary_offset_), v);
  }

  void reserve(size_t n) {
    ::toolbelt::PayloadBuffer::VectorReserve<T>(
        GetBufferAddr(), Header(relative_binary_offset_), n);
  }

  void resize(size_t n) {
    ::toolbelt::PayloadBuffer::VectorResize<T>(
        GetBufferAddr(), Header(relative_binary_offset_), n);
  }

  void Clear() {
    ::toolbelt::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                              Header(relative_binary_offset_));
  }
  void clear() { Clear(); }  // STL compatibility.

  PrimitiveVectorField& operator=(const PrimitiveVectorField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      push_back(other[i]);
    }
    ResetFieldCache();
    return *this;
  }
  PrimitiveVectorField& operator=(PrimitiveVectorField&& other) noexcept {
    return operator=(static_cast<const PrimitiveVectorField&>(other));
  }

  size_t size() const { return NumElements(); }
  T* data() { return GetRuntime()->template ToAddress<T>(BaseOffset()); }
  const T* data() const {
    return GetRuntime()->template ToAddress<const T>(BaseOffset());
  }
  size_t Size() const { return NumElements(); }

  absl::Span<T> AsMutableSpan() {
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    T* base = GetRuntime()->template ToAddress<T>(hdr->data);
    if (base == nullptr) {
      return absl::Span<T>();
    }

    return absl::Span<T>(base, hdr->num_elements);
  }

  absl::Span<const T> AsSpan() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return absl::Span<T>();
    }
    toolbelt::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    const T* base = GetRuntime()->template ToAddress<const T>(hdr->data);
    if (base == nullptr) {
      return absl::Span<const T>();
    }

    return absl::Span<const T>(base, hdr->num_elements);
  }

  bool empty() const { return size() == 0; }

  size_t capacity() const {
    ::toolbelt::BufferOffset offset = BaseOffset();
    if (offset == 0) {
      return 0;
    }
    ::toolbelt::BufferOffset* addr =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(offset);
    if (addr == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return addr[-1] / sizeof(value_type);
  }

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

  bool operator==(
      const PrimitiveVectorField<T, FixedSize, Packed, Signed>& other) const {
    size_t n = size();
    for (size_t i = 0; i < n; i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(
      const PrimitiveVectorField<T, FixedSize, Packed, Signed>& other) const {
    return !(*this == other);
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
  friend FieldIterator<PrimitiveVectorField, T>;
  friend FieldIterator<PrimitiveVectorField, const T>;
  toolbelt::VectorHeader* Header(uint32_t offset) const {
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) + offset);
  }

  ::toolbelt::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->num_elements;
  }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
};

template <typename Enum = int, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser, bool Packed = true>
class EnumVectorField : public Field {
 public:
  EnumVectorField() = default;
  explicit EnumVectorField(uint32_t source_offset,
                           uint32_t relative_binary_offset, int id, int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  EnumVectorField(const EnumVectorField&) = default;
  EnumVectorField(EnumVectorField&&) = default;

  using T = typename std::underlying_type<Enum>::type;

  Enum& operator[](int index) {
    T* base = GetRuntime()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      static Enum empty = static_cast<Enum>(0);
      return *reinterpret_cast<Enum*>(&empty);
    }
    return *reinterpret_cast<Enum*>(&base[index]);
  }

  const Enum operator[](int index) const {
    const T* base = GetRuntime()->template ToAddress<const T>(BaseOffset());
    if (base == nullptr) {
      return static_cast<Enum>(0);
    }
    return *reinterpret_cast<const Enum*>(&base[index]);
  }

  Enum front() { return (*this)[0]; }
  const Enum front() const { return (*this)[0]; }
  Enum back() { return (*this)[size() - 1]; }
  const Enum back() const { return (*this)[size() - 1]; }

  const std::vector<Enum> Get() const {
    size_t n = size();
    std::vector<Enum> r;
    for (size_t i = 0; i < n; i++) {
      r[i] = (*this)[i];
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
    toolbelt::VectorHeader* hdr = Header(relative_binary_offset_);
    Enum* base = GetRuntime()->template ToAddress<Enum>(hdr->data);
    if (base == nullptr) {
      return absl::Span<Enum>();
    }

    return absl::Span<Enum>(base, hdr->num_elements);
  }

  absl::Span<const Enum> AsSpan() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return absl::Span<Enum>();
    }
    toolbelt::VectorHeader* hdr = Header(static_cast<uint32_t>(offset));
    const Enum* base = GetRuntime()->template ToAddress<const Enum>(hdr->data);
    if (base == nullptr) {
      return absl::Span<const Enum>();
    }

    return absl::Span<const Enum>(base, hdr->num_elements);
  }

  void push_back(const Enum& v) {
    ::toolbelt::PayloadBuffer::VectorPush<T>(
        GetBufferAddr(), Header(relative_binary_offset_), static_cast<T>(v));
  }

  void reserve(size_t n) {
    ::toolbelt::PayloadBuffer::VectorReserve<T>(
        GetBufferAddr(), Header(relative_binary_offset_), n);
  }

  void resize(size_t n) {
    ::toolbelt::PayloadBuffer::VectorResize<T>(
        GetBufferAddr(), Header(relative_binary_offset_), n);
  }

  void Add(Enum v) { push_back(v); }

  void Clear() {
    ::toolbelt::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                              Header(relative_binary_offset_));
  }
  void clear() { Clear(); }  // STL compatibility.

  EnumVectorField& operator=(const EnumVectorField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      push_back(other[i]);
    }
    ResetFieldCache();
    return *this;
  }
  EnumVectorField& operator=(EnumVectorField&& other) noexcept {
    return operator=(static_cast<const EnumVectorField&>(other));
  }

  size_t size() const { return NumElements(); }
  Enum* data() { return GetRuntime()->template ToAddress<Enum>(BaseOffset()); }
  const Enum* data() const {
    return GetRuntime()->template ToAddress<const Enum>(BaseOffset());
  }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  size_t capacity() const {
    toolbelt::VectorHeader* hdr = Header();
    ::toolbelt::BufferOffset* addr =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    if (addr == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return addr[-1] / sizeof(T);
  }

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

  bool operator==(
      const EnumVectorField<Enum, Stringizer, Parser, Packed>& other) const {
    size_t n = size();
    for (size_t i = 0; i < n; i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(
      const EnumVectorField<Enum, Stringizer, Parser, Packed>& other) const {
    return !(*this == other);
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
  friend EnumFieldIterator<EnumVectorField, Enum>;
  friend EnumFieldIterator<EnumVectorField, const Enum>;
  toolbelt::VectorHeader* Header(::toolbelt::BufferOffset offset) const {
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) + offset);
  }

  ::toolbelt::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->num_elements;
  }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
};

// The vector contains a set of ::toolbelt::BufferOffsets allocated in the
// buffer, each of which contains the absolute offset of the message.
template <typename T>
class MessageVectorField : public Field {
 public:
  MessageVectorField() = default;
  explicit MessageVectorField(uint32_t source_offset,
                              uint32_t relative_binary_offset, int id,
                              int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  MessageVectorField(const MessageVectorField&) = default;
  MessageVectorField(MessageVectorField&&) = default;

  const MessageObject<T>& operator[](int index) const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return empty_;
    }
    auto hdr = Header(static_cast<uint32_t>(offset));
    if (static_cast<uint32_t>(index) >= hdr->num_elements) {
      return empty_;
    }
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    if (data[index] == 0) {
      return empty_;
    }
    if (static_cast<size_t>(index) >= msgs_.size()) {
      msgs_.resize(static_cast<size_t>(index) + 1);
    }
    if (msgs_[static_cast<size_t>(index)].empty()) {
      msgs_[static_cast<size_t>(index)] =
          MessageObject<T>(GetRuntime(), data[index]);
    }
    return msgs_[static_cast<size_t>(index)];
  }

  MessageObject<T>& operator[](int index) {
    return const_cast<MessageObject<T>&>(
        static_cast<const MessageVectorField*>(this)->operator[](index));
  }

  MessageObject<T>& front() {
    Populate();
    return msgs_.front();
  }
  const MessageObject<T>& front() const {
    Populate();
    return msgs_.front();
  }
  MessageObject<T>& back() {
    Populate();
    return msgs_.back();
  }
  const MessageObject<T>& back() const {
    Populate();
    return msgs_.back();
  }

  using value_type = MessageObject<T>;
  using reference = value_type&;
  using const_reference = value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using iterator = typename std::vector<MessageObject<T>>::iterator;
  using const_iterator = typename std::vector<MessageObject<T>>::const_iterator;
  using reverse_iterator =
      typename std::vector<MessageObject<T>>::reverse_iterator;
  using const_reverse_iterator =
      typename std::vector<MessageObject<T>>::const_reverse_iterator;

  iterator begin() {
    Populate();
    return msgs_.begin();
  }
  iterator end() {
    Populate();
    return msgs_.end();
  }
  reverse_iterator rbegin() {
    Populate();
    return msgs_.rbegin();
  }
  reverse_iterator rend() {
    Populate();
    return msgs_.rend();
  }
  const_iterator begin() const {
    Populate();
    return msgs_.begin();
  }
  const_iterator end() const {
    Populate();
    return msgs_.end();
  }
  const_iterator cbegin() const {
    Populate();
    return msgs_.cbegin();
  }
  const_iterator cend() const {
    Populate();
    return msgs_.cend();
  }
  const_reverse_iterator rbegin() const {
    Populate();
    return msgs_.rbegin();
  }
  const_reverse_iterator rend() const {
    Populate();
    return msgs_.rend();
  }
  const_reverse_iterator crbegin() const {
    Populate();
    return msgs_.crbegin();
  }
  const_reverse_iterator crend() const {
    Populate();
    return msgs_.crend();
  }

  void push_back(const T& v) {
    ::toolbelt::BufferOffset offset = v.absolute_binary_offset;
    ::toolbelt::PayloadBuffer::VectorPush<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), offset);
    MessageObject<T> obj(GetRuntime(), offset);
    obj.msg_ = v;
    msgs_.push_back(std::move(obj));
  }

  void push_back(T&& v) {
    ::toolbelt::BufferOffset offset = v.absolute_binary_offset;
    ::toolbelt::PayloadBuffer::VectorPush<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), offset);
    MessageObject<T> obj(GetRuntime(), offset);
    obj.msg_ = v;
    msgs_.push_back(std::move(obj));
  }

  T* Add() {
    // Allocate a new message.
    void* binary =
        ::toolbelt::PayloadBuffer::Allocate(GetBufferAddr(), T::BinarySize());
    ::toolbelt::BufferOffset absolute_binary_offset =
        GetRuntime()->ToOffset(binary);
    ::toolbelt::PayloadBuffer::VectorPush<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), absolute_binary_offset);
    auto obj = MessageObject<T>(GetRuntime(), absolute_binary_offset);
    obj.InstallMetadata();
    msgs_.push_back(std::move(obj));
    return msgs_.back().Mutable();
  }

  const T& Get(size_t index) const {
    return (*this)[static_cast<int>(index)].Get();
  }

  T* Mutable(size_t index) {
    if (static_cast<size_t>(index) >= msgs_.size()) {
      ::toolbelt::PayloadBuffer::VectorResize<::toolbelt::BufferOffset>(
          GetBufferAddr(), Header(), static_cast<size_t>(index) + 1);
      msgs_.resize(static_cast<size_t>(index) + 1);
    }

    if (msgs_[static_cast<size_t>(index)].IsPlaceholder()) {
      void* binary =
          ::toolbelt::PayloadBuffer::Allocate(GetBufferAddr(), T::BinarySize());
      ::toolbelt::BufferOffset absolute_binary_offset =
          GetRuntime()->ToOffset(binary);

      auto hdr = Header();
      ::toolbelt::BufferOffset* data =
          GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
      data[index] = absolute_binary_offset;

      auto obj = MessageObject<T>(GetRuntime(), absolute_binary_offset);
      obj.InstallMetadata();
      msgs_[static_cast<size_t>(index)] = std::move(obj);
    }
    return msgs_[static_cast<size_t>(index)].Mutable();
  }

  void SetOffset(int index, toolbelt::BufferOffset offset) {
    if (static_cast<size_t>(index) >= msgs_.size()) {
      msgs_.resize(static_cast<size_t>(index) + 1);
    }
    auto hdr = Header();
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    if (!msgs_[static_cast<size_t>(index)].IsPlaceholder() &&
        data[index] != 0) {
      // Already set, free the current value.
      msgs_[static_cast<size_t>(index)].Clear();
    }
    data[index] = offset;
    msgs_[static_cast<size_t>(index)] = MessageObject<T>(GetRuntime(), offset);
  }

  // Allocate a bunch of empty messages.
  std::vector<T*> Allocate(size_t n) {
    std::vector<T*> result;
    result.resize(n);
    this->resize(n);
    // Allocate memory for n messages in the payload buffer.
    std::vector<void*> addrs = ::toolbelt::PayloadBuffer::AllocateMany(
        GetBufferAddr(), T::BinarySize(), static_cast<uint32_t>(n), true);

    toolbelt::VectorHeader* hdr = Header();
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);

    // Fill in the msgs_ vector with MessageObject objects referring to the
    // allocated memory.
    for (size_t i = 0; i < n; i++) {
      auto& msg = msgs_[i].MutableMsg();
      msg.runtime = GetRuntime();
      toolbelt::BufferOffset offset = GetRuntime()->ToOffset(addrs[i]);
      msg.absolute_binary_offset = offset;
      msgs_[i].InstallMetadata();
      result[i] = &msg;
      data[i] = offset;
    }
    return result;
  }

  size_t capacity() const {
    ::toolbelt::BufferOffset* base =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(
            BaseOffset());

    if (base == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return base[-1] / sizeof(::toolbelt::BufferOffset);
  }

  void reserve(size_t n) {
    ::toolbelt::PayloadBuffer::VectorReserve<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), n);
    msgs_.reserve(n);
  }

  void resize(size_t n) {
    // Resize the vector data in the binary.  This contains BufferOffets.
    ::toolbelt::PayloadBuffer::VectorResize<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), n);
    msgs_.resize(n);
  }

  void Clear() {
    auto hdr = Header();
    auto data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    for (uint32_t i = 0; i < hdr->num_elements; i++) {
      if (msgs_[i].empty()) {
        continue;
      }
      msgs_[i].Clear();
      if (data[i] == 0) {
        continue;
      }
      GetBuffer()->Free(GetRuntime()->ToAddress(data[i]));
    }
    ::toolbelt::PayloadBuffer::VectorClear<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header());
    msgs_.clear();
  }

  size_t size() const { return NumElements(); }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  MessageVectorField& operator=(const MessageVectorField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    other.Populate();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      T* m = Add();
      if (absl::Status s = m->CloneFrom(other[i].Get()); !s.ok()) {
        return *this;
      }
    }
    ResetFieldCache();
    return *this;
  }
  MessageVectorField& operator=(MessageVectorField&& other) noexcept {
    return operator=(static_cast<const MessageVectorField&>(other));
  }

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

  bool operator==(const MessageVectorField<T>& other) const {
    return msgs_ != other.msgs_;
  }
  bool operator!=(const MessageVectorField<T>& other) const {
    return !(*this == other);
  }

  std::vector<MessageObject<T>>& Get() { return msgs_; }

  const std::vector<MessageObject<T>>& Get() const { return msgs_; }

  void Populate() const {
    if (!msgs_.empty()) {
      return;
    }
    // Populate the msgs vector with MessageObject objects referring to the
    // binary messages.
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return;
    }
    auto hdr = Header(static_cast<uint32_t>(offset));
    msgs_.resize(hdr->num_elements);
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    for (uint32_t i = 0; i < hdr->num_elements; i++) {
      if (data[i] == 0) {
        continue;
      }
      MessageObject<T> obj(GetRuntime(), data[i]);
      msgs_[i] = std::move(obj);
    }
  }

  size_t SerializedSize() const {
    Populate();
    size_t length = 0;
    for (size_t i = 0; i < size(); i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Number(), msgs_[i].SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    Populate();
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    for (const auto& msg : msgs_) {
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
    T* msg = Add();
    if (absl::Status status = msg->Deserialize(msg_buffer); !status.ok()) {
      return status;
    }
    return absl::OkStatus();
  }

  void SyncToPayload() const {
    Populate();
    for (const auto& message : msgs_) {
      if (!message.empty()) {
        message.Get().SyncToPayload();
      }
    }
  }

 private:
  friend FieldIterator<MessageVectorField, T>;
  friend FieldIterator<MessageVectorField, const T>;
  toolbelt::VectorHeader* Header(
      ::toolbelt::BufferOffset relative_offset = 0) const {
    if (relative_offset == 0) {
      relative_offset = relative_binary_offset_;
    }
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        GetMessageBinaryStart() + relative_offset);
  }

  ::toolbelt::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->num_elements;
  }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

 protected:
  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  mutable std::vector<MessageObject<T>> msgs_;
  MessageObject<T> empty_;
};

// This is a little more complex.  The binary vector contains a set of
// ::toolbelt::BufferOffsets each of which contains the offset into the
// ::toolbelt::PayloadBuffer of a toolbelt::StringHeader.  Each
// toolbelt::StringHeader contains the offset of the string data which is a
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
class StringVectorField : public Field {
 public:
  StringVectorField() = default;
  explicit StringVectorField(uint32_t source_offset,
                             uint32_t relative_binary_offset, int id,
                             int number)
      : Field(id, number),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  StringVectorField(const StringVectorField&) = default;
  StringVectorField(StringVectorField&& other) noexcept
      : Field(std::move(other)),
        source_offset_(other.source_offset_),
        relative_binary_offset_(other.relative_binary_offset_) {}

  const NonEmbeddedStringField& operator[](int index) const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return empty_;
    }
    auto hdr = Header(static_cast<uint32_t>(offset));
    if (static_cast<uint32_t>(index) >= hdr->num_elements) {
      return empty_;
    }
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    if (data[index] == 0) {
      return empty_;
    }
    if (static_cast<size_t>(index) >= strings_.size()) {
      strings_.resize(static_cast<size_t>(index) + 1);
    }
    if (strings_[static_cast<size_t>(index)].IsPlaceholder()) {
      strings_[static_cast<size_t>(index)] = NonEmbeddedStringField(
          Message::GetMessage(this, source_offset_), data[index]);
    }
    return strings_[static_cast<size_t>(index)];
  }

  NonEmbeddedStringField& operator[](int index) {
    return const_cast<NonEmbeddedStringField&>(
        static_cast<const StringVectorField*>(this)->operator[](index));
  }

  using value_type = NonEmbeddedStringField;
  using reference = value_type&;
  using const_reference = value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using iterator = typename std::vector<NonEmbeddedStringField>::iterator;
  using const_iterator =
      typename std::vector<NonEmbeddedStringField>::const_iterator;
  using reverse_iterator =
      typename std::vector<NonEmbeddedStringField>::reverse_iterator;
  using const_reverse_iterator =
      typename std::vector<NonEmbeddedStringField>::const_reverse_iterator;

  iterator begin() {
    Populate();
    return strings_.begin();
  }
  iterator end() {
    Populate();
    return strings_.end();
  }
  reverse_iterator rbegin() {
    Populate();
    return strings_.rbegin();
  }
  reverse_iterator rend() {
    Populate();
    return strings_.rend();
  }
  const_iterator begin() const {
    Populate();
    return strings_.begin();
  }
  const_iterator end() const {
    Populate();
    return strings_.end();
  }
  const_iterator cbegin() const {
    Populate();
    return strings_.cbegin();
  }
  const_iterator cend() const {
    Populate();
    return strings_.cend();
  }
  const_reverse_iterator rbegin() const {
    Populate();
    return strings_.rbegin();
  }
  const_reverse_iterator rend() const {
    Populate();
    return strings_.rend();
  }
  const_reverse_iterator crbegin() const {
    Populate();
    return strings_.crbegin();
  }
  const_reverse_iterator crend() const {
    Populate();
    return strings_.crend();
  }

  size_t size() const { return NumElements(); }
  NonEmbeddedStringField* data() {
    Populate();
    return strings_.data();
  }
  const NonEmbeddedStringField* data() const {
    Populate();
    return strings_.data();
  }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  NonEmbeddedStringField& front() {
    Populate();
    return strings_.front();
  }
  const NonEmbeddedStringField& front() const {
    Populate();
    return strings_.front();
  }
  NonEmbeddedStringField& back() {
    Populate();
    return strings_.back();
  }
  const NonEmbeddedStringField& back() const {
    Populate();
    return strings_.back();
  }

  StringVectorField& operator=(const StringVectorField& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    other.Populate();
    reserve(other.size());
    for (size_t i = 0; i < other.size(); i++) {
      push_back(other[i].Get());
    }
    ResetFieldCache();
    return *this;
  }

  StringVectorField& operator=(StringVectorField&& other) noexcept {
    return operator=(static_cast<const StringVectorField&>(other));
  }

  template <typename Str>
  void push_back(Str s) {
    // Allocate string header in buffer.
    void* str_hdr = ::toolbelt::PayloadBuffer::Allocate(
        GetBufferAddr(), sizeof(toolbelt::StringHeader));
    ::toolbelt::BufferOffset hdr_offset = GetRuntime()->ToOffset(str_hdr);
    ::toolbelt::PayloadBuffer::SetString(GetBufferAddr(), s, hdr_offset);

    // Add an offset for the new string to the binary.
    ::toolbelt::PayloadBuffer::VectorPush<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), hdr_offset);

    // Add a source string field.
    NonEmbeddedStringField field(Message::GetMessage(this, source_offset_),
                                 hdr_offset);
    strings_.push_back(std::move(field));
  }

  void Add(const char* s, size_t len) { push_back(std::string(s, len)); }
  template <typename Str>
  void Add(Str s) {
    push_back(s);
  }

  std::string_view Get(size_t index) const {
    return (*this)[static_cast<int>(index)].Get();
  }

  template <typename Str>
  void Set(size_t index, Str s) {
    if (static_cast<size_t>(index) >= strings_.size()) {
      ::toolbelt::PayloadBuffer::VectorResize<::toolbelt::BufferOffset>(
          GetBufferAddr(), Header(), static_cast<size_t>(index) + 1);
      strings_.resize(static_cast<size_t>(index) + 1);
    }

    if (strings_[static_cast<size_t>(index)].IsPlaceholder()) {
      // Allocate string header in buffer.
      void* str_hdr = ::toolbelt::PayloadBuffer::Allocate(
          GetBufferAddr(), sizeof(toolbelt::StringHeader));
      ::toolbelt::BufferOffset hdr_offset = GetRuntime()->ToOffset(str_hdr);

      auto hdr = Header();
      ::toolbelt::BufferOffset* data =
          GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
      data[index] = hdr_offset;

      // Add a source string field.
      NonEmbeddedStringField field(Message::GetMessage(this, source_offset_),
                                   hdr_offset);
      strings_[static_cast<size_t>(index)] = std::move(field);
    }
    strings_[static_cast<size_t>(index)].Set(s);
  }

  size_t capacity() const {
    ::toolbelt::BufferOffset* base =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(
            BaseOffset());

    if (base == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return base[-1] / sizeof(::toolbelt::BufferOffset);
  }

  void reserve(size_t n) {
    ::toolbelt::PayloadBuffer::VectorReserve<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), n);
    strings_.reserve(n);
  }

  void resize(size_t n) {
    // Resize the vector data in the binary.  This contains BufferOffets.
    ::toolbelt::PayloadBuffer::VectorResize<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header(), n);
    strings_.resize(n);
  }

  void Clear() {
    for (auto& s : strings_) {
      if (!s.IsPlaceholder()) {
        s.Clear();
      }
    }
    strings_.clear();
    ::toolbelt::PayloadBuffer::VectorClear<::toolbelt::BufferOffset>(
        GetBufferAddr(), Header());
  }

  void clear() { Clear(); }  // STL compatibility.

  ::toolbelt::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(toolbelt::VectorHeader);
  }
  ::toolbelt::BufferOffset BinaryOffset() const {
    return relative_binary_offset_;
  }

  bool operator==(const StringVectorField& other) const {
    return strings_ == other.strings_;
  }
  bool operator!=(const StringVectorField& other) const {
    return !(*this == other);
  }

  // Populate the vector with the strings from the binary message.  This must be
  // called before you access the vector via iterators.
  void Populate() const {
    if (!strings_.empty()) {
      return;
    }
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return;
    }
    auto hdr = Header(static_cast<uint32_t>(offset));
    strings_.resize(hdr->num_elements);
    ::toolbelt::BufferOffset* data =
        GetRuntime()->template ToAddress<::toolbelt::BufferOffset>(hdr->data);
    for (uint32_t i = 0; i < hdr->num_elements; i++) {
      if (data[i] == 0) {
        continue;
      }
      strings_[i] = NonEmbeddedStringField(
          Message::GetMessage(this, source_offset_), data[i]);
    }
  }

  std::vector<std::string_view> Get() const {
    std::vector<std::string_view> r;
    for (const auto& s : strings_) {
      r.push_back(s.Get());
    }
    return r;
  }

  size_t SerializedSize() const {
    Populate();
    size_t length = 0;
    for (size_t i = 0; i < size(); i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Number(), strings_[i].SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    Populate();
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    for (const auto& s : strings_) {
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
  toolbelt::VectorHeader* Header(
      ::toolbelt::BufferOffset relative_offset = 0) const {
    if (relative_offset == 0) {
      relative_offset = relative_binary_offset_;
    }
    return GetRuntime()->template ToAddress<toolbelt::VectorHeader>(
        GetMessageBinaryStart() + relative_offset);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::toolbelt::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(static_cast<uint32_t>(offset))->num_elements;
  }

  ::toolbelt::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::toolbelt::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::toolbelt::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }
  uint32_t source_offset_;
  ::toolbelt::BufferOffset relative_binary_offset_;
  mutable std::vector<NonEmbeddedStringField> strings_;
  NonEmbeddedStringField empty_;
};

#undef DECLARE_ZERO_COPY_VECTOR_BITS
#undef DECLARE_RELAY_VECTOR_BITS

}  // namespace phaser
