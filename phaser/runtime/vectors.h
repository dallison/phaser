#pragma once

// Vector fields (repeated fields).

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/payload_buffer.h"
#include "phaser/runtime/wireformat.h"
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <vector>

namespace phaser {

class ProtoBuffer;

#define DECLARE_ZERO_COPY_VECTOR_BITS(vtype, itype, ctype, utype)              \
  using value_type = vtype;                                                    \
  using reference = value_type &;                                              \
  using const_reference = value_type &;                                        \
  using pointer = value_type *;                                                \
  using const_pointer = const value_type *;                                    \
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
    return iterator(this, BaseOffset() + NumElements() * sizeof(value_type));  \
  }                                                                            \
  const_iterator begin() const { return const_iterator(this, BaseOffset()); }  \
  const_iterator end() const {                                                 \
    return const_iterator(this,                                                \
                          BaseOffset() + NumElements() * sizeof(value_type));  \
  }                                                                            \
  const_iterator cbegin() const { return const_iterator(this, BaseOffset()); } \
  const_iterator cend() const {                                                \
    return const_iterator(this,                                                \
                          BaseOffset() + NumElements() * sizeof(value_type));  \
  }                                                                            \
                                                                               \
  reverse_iterator rbegin() {                                                  \
    return reverse_iterator(this, BaseOffset(), true);                         \
  }                                                                            \
  reverse_iterator rend() {                                                    \
    return reverse_iterator(                                                   \
        this, BaseOffset() + NumElements() * sizeof(value_type), true);        \
  }                                                                            \
  const_reverse_iterator rbegin() const {                                      \
    return const_reverse_iterator(this, BaseOffset(), true);                   \
  }                                                                            \
  const_reverse_iterator rend() const {                                        \
    return const_reverse_iterator(                                             \
        this, BaseOffset() + NumElements() * sizeof(value_type), true);        \
  }                                                                            \
  const_reverse_iterator crbegin() const {                                     \
    return const_reverse_iterator(this, BaseOffset(), true);                   \
  }                                                                            \
  const_reverse_iterator crend() const {                                       \
    return const_reverse_iterator(                                             \
        this, BaseOffset() + NumElements() * sizeof(value_type), true);        \
  }

// vtype: value type
// rtype: relay type (like std::array<T,N>)
// relay: member to relay through
#define DECLARE_RELAY_VECTOR_BITS(vtype, rtype, relay)                         \
  using value_type = vtype;                                                    \
  using reference = value_type &;                                              \
  using const_reference = value_type &;                                        \
  using pointer = value_type *;                                                \
  using const_pointer = const value_type *;                                    \
  using size_type = size_t;                                                    \
  using difference_type = ptrdiff_t;                                           \
                                                                               \
  using iterator = typename rtype::iterator;                                   \
  using const_iterator = typename rtype::const_iterator;                       \
  using reverse_iterator = typename rtype::reverse_iterator;                   \
  using const_reverse_iterator = typename rtype::const_reverse_iterator;       \
                                                                               \
  iterator begin() { return relay.begin(); }                                   \
  iterator end() { return relay.end(); }                                       \
  reverse_iterator rbegin() { return relay.rbegin(); }                         \
  reverse_iterator rend() { return relay.rend(); }                             \
  const_iterator begin() const { return relay.begin(); }                       \
  const_iterator end() const { return relay.end(); }                           \
  const_iterator cbegin() const { return relay.begin(); }                      \
  const_iterator cend() const { return relay.end(); }                          \
  const_reverse_iterator rbegin() const { return relay.rbegin(); }             \
  const_reverse_iterator rend() const { return relay.rend(); }                 \
  const_reverse_iterator crbegin() const { return relay.crbegin(); }           \
  const_reverse_iterator crend() const { return relay.crend(); }

// This is a variable length vector of T.  It looks like a std::vector<T>.
// The binary message contains a phaser::VectorHeader at the binary offset.
// This contains the number of elements and the base offset for the data.
template <typename T, bool FixedSize, bool Signed, bool Packed>
class PrimitiveVectorField : public Field {
public:
  PrimitiveVectorField() = default;
  explicit PrimitiveVectorField(uint32_t source_offset,
                                uint32_t relative_binary_offset, int id,
                                int number)
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}

  const T &operator[](int index) {
    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return T();
    }
    return base[index];
  }

  T operator[](int index) const {
    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return T();
    }
    return base[index];
  }

  T front() { return (*this)[0]; }
  const T front() const { return (*this)[0]; }
  T back() { return (*this)[size() - 1]; }
  const T back() const { return (*this)[size() - 1]; }

  T Get(size_t index) const { return (*this)[index]; }

  void Set(size_t index, T v) {
    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
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

  void push_back(const T &v) {
    phaser::PayloadBuffer::VectorPush<T>(GetBufferAddr(),
                                         Header(relative_binary_offset_), v);
  }

  void reserve(size_t n) {
    phaser::PayloadBuffer::VectorReserve<T>(GetBufferAddr(),
                                            Header(relative_binary_offset_), n);
  }

  void resize(size_t n) {
    phaser::PayloadBuffer::VectorResize<T>(GetBufferAddr(),
                                           Header(relative_binary_offset_), n);
  }

  void Clear() {
    phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                          Header(relative_binary_offset_));
  }
  void clear() { Clear(); } // STL compatibility.

  size_t size() const { return NumElements(); }
  T *data() const { return GetBuffer()->template ToAddress<T>(BaseOffset()); }
  size_t Size() const { return NumElements(); }

  bool empty() const { return size() == 0; }

  size_t capacity() const {
    phaser::BufferOffset *addr = BaseOffset();
    if (addr == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return addr[-1] / sizeof(value_type);
  }

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(phaser::VectorHeader);
  }
  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(
      const PrimitiveVectorField<T, FixedSize, Packed, Signed> &other) const {
    size_t n = size();
    for (size_t i = 0; i < n; i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(
      const PrimitiveVectorField<T, FixedSize, Packed, Signed> &other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t sz = size();
    if (sz == 0) {
      return 0;
    }
    size_t length = 0;

    // Packed is default in proto3 but optional in proto2.
    if (Packed) {
      if (FixedSize) {
        return ProtoBuffer::LengthDelimitedSize(Number(), sz * sizeof(T));
      } else {
        T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
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
    if (FixedSize) {
      length += sz * (ProtoBuffer::TagSize(Number(),
                                           ProtoBuffer::FixedWireType<T>()) +
                      sizeof(T));
    } else {
      T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
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

  absl::Status Serialize(ProtoBuffer &buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return absl::OkStatus();
    }
    // Packed is default in proto3 but optional in proto2.
    if (Packed) {
      if (FixedSize) {
        return buffer.SerializeLengthDelimited(
            Number(), reinterpret_cast<const char *>(base), sz * sizeof(T));
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
    if (FixedSize) {
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

  absl::Status Deserialize(ProtoBuffer &buffer) {
    if (Packed) {
      absl::StatusOr<absl::Span<char>> data =
          buffer.DeserializeLengthDelimited();
      if (!data.ok()) {
        return data.status();
      }
      if (FixedSize) {
        resize(data->size() / sizeof(T));
        T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
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
      if (FixedSize) {
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
  phaser::VectorHeader *Header(uint32_t offset) const {
    return GetBuffer()->template ToAddress<phaser::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) + offset);
  }

  phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->num_elements;
  }

  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  std::shared_ptr<phaser::PayloadBuffer *> GetRuntime() {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
};

template <typename Enum, bool Packed> class EnumVectorField : public Field {
public:
  EnumVectorField() = default;
  explicit EnumVectorField(uint32_t source_offset,
                           uint32_t relative_binary_offset, int id, int number)
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}

  using T = typename std::underlying_type<Enum>::type;

  Enum operator[](int index) {
    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return *reinterpret_cast<Enum *>(0);
    }
    return *reinterpret_cast<Enum *>(&base[index]);
  }

  const Enum operator[](int index) const {
    const T *base = GetBuffer()->template ToAddress<const T>(BaseOffset());
    if (base == nullptr) {
      return *reinterpret_cast<const Enum *>(0);
    }
    return *reinterpret_cast<const Enum *>(&base[index]);
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

  Enum Get(size_t index) const { return (*this)[index]; }

  void Set(size_t index, Enum v) {
    Enum *base = GetBuffer()->template ToAddress<T>(BaseOffset());
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

  void push_back(const Enum &v) {
    phaser::PayloadBuffer::VectorPush<T>(GetBufferAddr(), Header(),
                                         static_cast<T>(v));
  }

  void reserve(size_t n) {
    phaser::PayloadBuffer::VectorReserve<T>(GetBufferAddr(), Header(), n);
  }

  void resize(size_t n) {
    phaser::PayloadBuffer::VectorResize<T>(GetBufferAddr(), Header(), n);
  }

  void Clear() {
    phaser::PayloadBuffer::VectorClear<T>(GetBufferAddr(),
                                          Header(relative_binary_offset_));
  }
  void clear() { Clear(); } // STL compatibility.

  size_t size() const { return NumElements(); }
  Enum *data() const { GetBuffer()->template ToAddress<Enum>(BaseOffset()); }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  size_t capacity() const {
    phaser::VectorHeader *hdr = Header();
    phaser::BufferOffset *addr =
        GetBuffer()->template ToAddress<phaser::BufferOffset>(hdr->data);
    if (addr == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return addr[-1] / sizeof(T);
  }

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(phaser::VectorHeader);
  }
  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(const EnumVectorField<Enum, Packed> &other) const {
    size_t n = size();
    for (size_t i = 0; i < n; i++) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const EnumVectorField<Enum, Packed> &other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    size_t sz = size();
    if (sz == 0) {
      return 0;
    }
    size_t length = 0;

    // Packed is default in proto3 but optional in proto2.
    if (Packed) {
      T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
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
    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return 0;
    }
    for (size_t i = 0; i < sz; i++) {
      length += ProtoBuffer::TagSize(Number(), WireType::kVarint) +
                ProtoBuffer::VarintSize<T, false>(base[i]);
    }

    return ProtoBuffer::LengthDelimitedSize(Number(), length);
  }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    T *base = GetBuffer()->template ToAddress<T>(BaseOffset());
    if (base == nullptr) {
      return absl::OkStatus();
    }
    // Packed is default in proto3 but optional in proto2.
    if (Packed) {
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

  absl::Status Deserialize(ProtoBuffer &buffer) {
    if (Packed) {
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
        push_back(*v);
      }
    } else {
      absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
      if (!v.ok()) {
        return v.status();
      }
      push_back(*v);
    }
    return absl::OkStatus();
  }

private:
  friend EnumFieldIterator<EnumVectorField, Enum>;
  friend EnumFieldIterator<EnumVectorField, const Enum>;
  phaser::VectorHeader *Header(BufferOffset offset) const {
    return GetBuffer()->template ToAddress<phaser::VectorHeader>(
        Message::GetMessageBinaryStart(this, source_offset_) + offset);
  }

  phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->num_elements;
  }

  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
};

// The vector contains a set of phaser::BufferOffsets allocated in the buffer,
// each of which contains the absolute offset of the message.
template <typename T> class MessageVectorField : public Field {
public:
  MessageVectorField() = default;
  explicit MessageVectorField(uint32_t source_offset,
                              uint32_t relative_binary_offset, int id,
                              int number)
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}

  const MessageObject<T> &operator[](int index) const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return empty_;
    }
    auto hdr = Header(offset);
    if (index >= hdr->num_elements) {
      return empty_;
    }
    BufferOffset *data =
        GetBuffer()->template ToAddress<BufferOffset>(hdr->data);
    if (data[index] == 0) {
      return empty_;
    }
    if (index >= msgs_.size()) {
      msgs_.resize(index + 1);
    }
    if (msgs_[index].empty()) {
      msgs_[index] = MessageObject<T>(GetRuntime(), data[index]);
    }
    return msgs_[index];
  }

  MessageObject<T> &front() { return msgs_.front(); }
  const MessageObject<T> &front() const { return msgs_.front(); }
  MessageObject<T> &back() { return msgs_.back(); }
  const MessageObject<T> &back() const { return msgs_.back(); }

#define RTYPE std::vector<MessageObject<T>>
  DECLARE_RELAY_VECTOR_BITS(MessageObject<T>, RTYPE, msgs_)
#undef RTYPE

  void push_back(const T &v) {
    phaser::BufferOffset offset = v.absolute_binary_offset;
    phaser::PayloadBuffer::VectorPush<phaser::BufferOffset>(GetBufferAddr(),
                                                            Header(), offset);
    MessageObject<T> obj(GetRuntime(), offset);
    obj.msg_ = v;
    msgs_.push_back(std::move(obj));
  }

  void push_back(T &&v) {
    phaser::BufferOffset offset = v.absolute_binary_offset;
    phaser::PayloadBuffer::VectorPush<phaser::BufferOffset>(GetBufferAddr(),
                                                            Header(), offset);
    MessageObject<T> obj(GetRuntime(), offset);
    obj.msg_ = v;
    msgs_.push_back(std::move(obj));
  }

  T *Add() {
    // Allocate a new message.
    void *binary = phaser::PayloadBuffer::Allocate(GetBufferAddr(),
                                                   T::BinarySize(), 8, true);
    phaser::BufferOffset absolute_binary_offset = GetBuffer()->ToOffset(binary);
    phaser::PayloadBuffer::VectorPush<phaser::BufferOffset>(
        GetBufferAddr(), Header(), absolute_binary_offset);
    auto obj = MessageObject<T>(GetRuntime(), absolute_binary_offset);
    obj.InstallMetadata();
    msgs_.push_back(std::move(obj));
    return msgs_.back().Mutable();
  }

  const T &Get(size_t index) const { return (*this)[index].Get(); }

  T *Mutable(int index) {
    if (index >= msgs_.size()) {
      phaser::PayloadBuffer::VectorResize<phaser::BufferOffset>(
          GetBufferAddr(), Header(), index + 1);
      msgs_.resize(index + 1);
    }

    if (msgs_[index].IsPlaceholder()) {
      void *binary = phaser::PayloadBuffer::Allocate(GetBufferAddr(),
                                                     T::BinarySize(), 8, true);
      phaser::BufferOffset absolute_binary_offset =
          GetBuffer()->ToOffset(binary);

      auto hdr = Header();
      BufferOffset *data =
          GetBuffer()->template ToAddress<BufferOffset>(hdr->data);
      data[index] = absolute_binary_offset;

      auto obj = MessageObject<T>(GetRuntime(), absolute_binary_offset);
      obj.InstallMetadata();
      msgs_[index] = std::move(obj);
    }
    return msgs_[index].Mutable();
  }

  size_t capacity() const {
    phaser::BufferOffset *base =
        GetBuffer()->template ToAddress<phaser::BufferOffset>(BaseOffset());

    if (base == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return base[-1] / sizeof(phaser::BufferOffset);
  }

  void reserve(size_t n) {
    phaser::PayloadBuffer::VectorReserve<phaser::BufferOffset>(GetBufferAddr(),
                                                               Header(), n);
    msgs_.reserve(n);
  }

  void resize(size_t n) {
    // Resize the vector data in the binary.  This contains BufferOffets.
    phaser::PayloadBuffer::VectorResize<phaser::BufferOffset>(GetBufferAddr(),
                                                              Header(), n);
    msgs_.resize(n);
  }

  void Clear() {
    auto hdr = Header();
    auto data =
        GetBuffer()->template ToAddress<phaser::BufferOffset>(hdr->data);
    for (uint32_t i = 0; i < hdr->num_elements; i++) {
      if (msgs_[i].empty()) {
        continue;
      }
      msgs_[i].Clear();
      if (data[i] == 0) {
        continue;
      }
      GetBuffer()->Free(GetBuffer()->ToAddress(data[i]));
    }
    phaser::PayloadBuffer::VectorClear<phaser::BufferOffset>(GetBufferAddr(),
                                                             Header());
    msgs_.clear();
  }

  size_t size() const { return NumElements(); }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(phaser::VectorHeader);
  }
  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(const MessageVectorField<T> &other) const {
    return msgs_ != other.msgs_;
  }
  bool operator!=(const MessageVectorField<T> &other) const {
    return !(*this == other);
  }

  std::vector<MessageObject<T>> &Get() { return msgs_; }

  const std::vector<MessageObject<T>> &Get() const { return msgs_; }

  void Populate() {
    // Populate the msgs vector with MessageObject objects referring to the
    // binary messages.
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return;
    }
    auto hdr = Header(offset);
    msgs_.resize(hdr->num_elements);
    phaser::BufferOffset *data =
        GetBuffer()->template ToAddress<phaser::BufferOffset>(hdr->data);
    for (uint32_t i = 0; i < hdr->num_elements; i++) {
      if (data[i] == 0) {
        continue;
      }
      MessageObject<T> obj(GetRuntime(), data[i]);
      msgs_.push_back(std::move(obj));
    }
  }

  size_t SerializedSize() const {
    size_t length = 0;
    for (size_t i = 0; i < size(); i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Number(), msgs_[i].SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    for (const auto &msg : msgs_) {
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

  absl::Status Deserialize(ProtoBuffer &buffer) {
    absl::StatusOr<absl::Span<char>> v =
        buffer.DeserializeLengthDelimited();
    if (!v.ok()) {
      return v.status();
    }
    ProtoBuffer msg_buffer(*v);
    T *msg = Add();
    if (absl::Status status = msg->Deserialize(msg_buffer); !status.ok()) {
      return status;
    }
    return absl::OkStatus();
  }

private:
  friend FieldIterator<MessageVectorField, T>;
  friend FieldIterator<MessageVectorField, const T>;
  phaser::VectorHeader *Header(BufferOffset relative_offset = 0) const {
    if (relative_offset == 0) {
      relative_offset = relative_binary_offset_;
    }
    return GetBuffer()->template ToAddress<phaser::VectorHeader>(
        GetMessageBinaryStart() + relative_offset);
  }

  phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->num_elements;
  }

  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  std::shared_ptr<MessageRuntime> GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
  mutable std::vector<MessageObject<T>> msgs_;
  MessageObject<T> empty_;
};

// This is a little more complex.  The binary vector contains a set of
// phaser::BufferOffsets each of which contains the offset into the
// phaser::PayloadBuffer of a phaser::StringHeader.  Each
// phaser::StringHeader contains the offset of the string data which is a
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
      : Field(id, number), source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}

  const NonEmbeddedStringField &operator[](int index) const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return empty_;
    }
    auto hdr = Header(offset);
    if (index >= hdr->num_elements) {
      return empty_;
    }
    BufferOffset *data =
        GetBuffer()->template ToAddress<BufferOffset>(hdr->data);
    if (data[index] == 0) {
      return empty_;
    }
    if (index >= strings_.size()) {
      strings_.resize(index + 1);
    }
    if (strings_[index].IsPlaceholder()) {
      strings_[index] = NonEmbeddedStringField(
          Message::GetMessage(this, source_offset_), data[index]);
    }
    return strings_[index];
  }

#define RTYPE std::vector<NonEmbeddedStringField>
  DECLARE_RELAY_VECTOR_BITS(NonEmbeddedStringField, RTYPE, strings_)
#undef RTYPE

  size_t size() const { return NumElements(); }
  NonEmbeddedStringField *data() {
    Populate();
    return strings_.data();
  }
  bool empty() const { return size() == 0; }
  size_t Size() const { return NumElements(); }

  NonEmbeddedStringField &front() { return strings_.front(); }
  const NonEmbeddedStringField &front() const { return strings_.front(); }
  NonEmbeddedStringField &back() { return strings_.back(); }
  const NonEmbeddedStringField &back() const { return strings_.back(); }

  void push_back(std::string_view s) {
    // Allocate string header in buffer.
    void *str_hdr = phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), sizeof(phaser::StringHeader), 4);
    phaser::BufferOffset hdr_offset = GetBuffer()->ToOffset(str_hdr);
    phaser::PayloadBuffer::SetString(GetBufferAddr(), s, hdr_offset);

    // Add an offset for the new string to the binary.
    phaser::PayloadBuffer::VectorPush<phaser::BufferOffset>(
        GetBufferAddr(), Header(), hdr_offset);

    // Add a source string field.
    NonEmbeddedStringField field(Message::GetMessage(this, source_offset_),
                                 hdr_offset);
    strings_.push_back(std::move(field));
  }

  void Add(const char *s, size_t len) { push_back(std::string(s, len)); }
  void Add(std::string_view s) { push_back(s); }

  std::string_view Get(int index) const { return (*this)[index].Get(); }

  void Set(int index, std::string_view s) {
    if (index >= strings_.size()) {
      phaser::PayloadBuffer::VectorResize<phaser::BufferOffset>(
          GetBufferAddr(), Header(), index + 1);
      strings_.resize(index + 1);
    }

    if (strings_[index].IsPlaceholder()) {
      // Allocate string header in buffer.
      void *str_hdr = phaser::PayloadBuffer::Allocate(
          GetBufferAddr(), sizeof(phaser::StringHeader), 4);
      phaser::BufferOffset hdr_offset = GetBuffer()->ToOffset(str_hdr);

      auto hdr = Header();
      BufferOffset *data =
          GetBuffer()->template ToAddress<BufferOffset>(hdr->data);
      data[index] = hdr_offset;

      // Add a source string field.
      NonEmbeddedStringField field(Message::GetMessage(this, source_offset_),
                                   hdr_offset);
      strings_[index] = std::move(field);
    }
    strings_[index].Set(s);
  }

  size_t capacity() const {
    phaser::BufferOffset *base =
        GetBuffer()->template ToAddress<phaser::BufferOffset>(BaseOffset());

    if (base == nullptr) {
      return 0;
    }
    // Word before memory is size of memory in bytes.
    return base[-1] / sizeof(phaser::BufferOffset);
  }

  void reserve(size_t n) {
    phaser::PayloadBuffer::VectorReserve<phaser::BufferOffset>(GetBufferAddr(),
                                                               Header(), n);
    strings_.reserve(n);
  }

  void resize(size_t n) {
    // Resize the vector data in the binary.  This contains BufferOffets.
    phaser::PayloadBuffer::VectorResize<phaser::BufferOffset>(GetBufferAddr(),
                                                              Header(), n);
    strings_.resize(n);
  }

  void Clear() {
    for (auto &s : strings_) {
      s.Clear();
    }
    strings_.clear();
    phaser::PayloadBuffer::VectorClear<phaser::BufferOffset>(GetBufferAddr(),
                                                             Header());
  }

  void clear() { Clear(); } // STL compatibility.

  phaser::BufferOffset BinaryEndOffset() const {
    return relative_binary_offset_ + sizeof(phaser::VectorHeader);
  }
  phaser::BufferOffset BinaryOffset() const { return relative_binary_offset_; }

  bool operator==(const StringVectorField &other) const {
    return strings_ == other.strings_;
  }
  bool operator!=(const StringVectorField &other) const {
    return !(*this == other);
  }

  // Populate the vector with the strings from the binary message.  This must be
  // called before you access the vector via iterators.
  void Populate() {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset == -1) {
      return;
    }
    auto hdr = Header(offset);
    strings_.resize(hdr->num_elements);
    BufferOffset *data =
        GetBuffer()->template ToAddress<BufferOffset>(hdr->data);
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
    for (const auto &s : strings_) {
      r.push_back(s.Get());
    }
    return r;
  }

  size_t SerializedSize() const {
    size_t length = 0;
    for (size_t i = 0; i < size(); i++) {
      length += phaser::ProtoBuffer::LengthDelimitedSize(
          Number(), strings_[i].SerializedSize());
    }
    return length;
  }

  absl::Status Serialize(ProtoBuffer &buffer) const {
    size_t sz = size();
    if (sz == 0) {
      return absl::OkStatus();
    }

    for (const auto &s : strings_) {
      if (absl::Status status =
              buffer.SerializeLengthDelimited(Number(), s.data(), s.size());
          !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Deserialize(ProtoBuffer &buffer) {
    absl::StatusOr<std::string_view> v = buffer.DeserializeString();
    if (!v.ok()) {
      return v.status();
    }
    push_back(*v);
    return absl::OkStatus();
  }

private:
  phaser::VectorHeader *Header(BufferOffset relative_offset = 0) const {
    if (relative_offset == 0) {
      relative_offset = relative_binary_offset_;
    }
    return GetBuffer()->template ToAddress<phaser::VectorHeader>(
        GetMessageBinaryStart() + relative_offset);
  }

  phaser::BufferOffset BaseOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->data;
  }

  size_t NumElements() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return Header(offset)->num_elements;
  }

  phaser::PayloadBuffer *GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  phaser::PayloadBuffer **GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }
  uint32_t source_offset_;
  phaser::BufferOffset relative_binary_offset_;
  mutable std::vector<NonEmbeddedStringField> strings_;
  NonEmbeddedStringField empty_;
};

#undef DECLARE_ZERO_COPY_VECTOR_BITS
#undef DECLARE_RELAY_VECTOR_BITS

} // namespace phaser
