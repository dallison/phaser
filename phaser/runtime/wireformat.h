// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include <cstddef>
#include <stdint.h>
#include <string.h>
#include <string>
#include <string_view>

namespace phaser {

enum class WireType {
  kVarint = 0,
  kFixed64 = 1,
  kLengthDelimited = 2,
  kStartGroup = 3,
  kEndGroup = 4,
  kFixed32 = 5,
};

class ProtoBuffer {
public:
  static constexpr int kFieldIdShift = 3;
  static constexpr int kWireTypeMask = (1 << kFieldIdShift) - 1;
  static constexpr int kFieldIdMask = ~kWireTypeMask;

  // Dynamic buffer with own memory allocation.
  ProtoBuffer(size_t initial_size = 16) : owned_(true), size_(initial_size) {
    if (initial_size < 16) {
      // Need a reasonable size to start with.
      abort();
    }
    start_ = reinterpret_cast<char *>(malloc(size_));
    if (start_ == nullptr) {
      abort();
    }
    addr_ = start_;
    end_ = start_ + size_;
  }

  // Fixed buffer in non-owned memory.
  ProtoBuffer(char *addr, size_t size)
      : owned_(false), start_(addr), size_(size), addr_(addr),
        end_(addr_ + size) {}

  ProtoBuffer(const char *addr, size_t size)
      : owned_(false), start_(const_cast<char*>(addr)), size_(size), addr_(const_cast<char*>(addr)),
        end_(addr_ + size) {}

  ProtoBuffer(absl::Span<char> v) {
    size_ = v.size();
    start_ = v.data();
    addr_ = start_;
    end_ = start_ + size_;
  }

  ProtoBuffer(std::string_view v) {
    size_ = v.size();
    start_ = const_cast<char*>(v.data());
    addr_ = start_;
    end_ = start_ + size_;
  }

  ~ProtoBuffer() {
    if (owned_) {
      free(start_);
    }
  }

  size_t Size() const { return addr_ - start_; }

  size_t size() const { return Size(); }

  template <typename T> T *Data() { return reinterpret_cast<T *>(start_); }

  char *data() { return Data<char>(); }

  std::string AsString() const { return std::string(start_, addr_ - start_); }

  template <typename T> absl::Span<const T> AsSpan() const {
    return absl::Span<T>(reinterpret_cast<const T *>(start_), addr_ - start_);
  }

  bool Eof() const { return addr_ == end_; }

  void Clear() {
    addr_ = start_;
    end_ = start_;
  }

  template <typename T> static T ZigZag(T value) {
    return (value << 1) ^ (value >> 31);
  }
  template <typename T> static T ZagZig(T value) {
    const uint64_t mask = (1LL << (sizeof(T) * 8 - 1)) - 1LL;
    return ((value >> 1) & static_cast<T>(mask)) ^ -(value & 1);
  }

  template <typename T> static constexpr WireType FixedWireType() {
    if (sizeof(T) == 4) {
      return WireType::kFixed32;
    } else if (sizeof(T) == 8) {
      return WireType::kFixed64;
    }
    abort();
  }

  // Size functions.
  static size_t TagSize(int field_number, WireType wire_type) {
    return VarintSize<int32_t, false>(MakeTag(field_number, wire_type));
  }

  template <typename T, bool Signed> static size_t VarintSize(T value) {
    if (Signed) {
      value = ZigZag(value);
    }
    size_t size = 0;
    for (;;) {
      if ((value & ~0x7f) == 0) {
        return size + 1;
      } else {
        size++;
        value >>= 7;
      }
    }
  }

  inline static size_t LengthDelimitedSize(int field_number, size_t length) {
    return TagSize(field_number, WireType::kLengthDelimited) +
           VarintSize<int32_t, false>(length) + length;
  }

  inline static size_t StringSize(int field_number, std::string_view str) {
    return LengthDelimitedSize(field_number, str.size());
  }

  // Serialization functions.

  template <typename T, bool Signed> absl::Status SerializeRawVarint(T value) {
    if (Signed) {
      value = ZigZag(value);
    }
    if (auto status = HasSpaceFor(VarintSize<T, false>(value)); !status.ok()) {
      return status;
    }
    for (;;) {
      if ((value & ~0x7f) == 0) {
        *addr_++ = static_cast<char>(value);
        break;
      } else {
        *addr_++ = static_cast<char>((value & 0xfF) | 0x80);
        value >>= 7;
      }
    }
    return absl::OkStatus();
  }

  template <typename T, bool Signed>
  absl::Status SerializeVarint(int field_number, T value) {
    if (auto status = SerializeTag(field_number, WireType::kVarint);
        !status.ok()) {
      return status;
    }
    return SerializeRawVarint<T, Signed>(value);
  }

  absl::Status SerializeTag(int field_number, WireType wire_type) {
    return SerializeRawVarint<uint32_t, false>(
        MakeTag(field_number, wire_type));
  }

  template <typename T> absl::Status SerializeFixed(int field_number, T value) {
    if (auto status = SerializeTag(field_number, FixedWireType<T>());
        !status.ok()) {
      return status;
    }

    if (auto status = HasSpaceFor(sizeof(T)); !status.ok()) {
      return status;
    }
    memcpy(addr_, &value, sizeof(T));
    addr_ += sizeof(T);
    return absl::OkStatus();
  }

  absl::Status SerializeLengthDelimited(int field_number, const void *data,
                                        size_t length) {
    if (auto status = SerializeTag(field_number, WireType::kLengthDelimited);
        !status.ok()) {
      return status;
    }
    if (absl::Status status = SerializeRawVarint<int32_t, false>(length);
        !status.ok()) {
      return status;
    }
    if (auto status = HasSpaceFor(length); !status.ok()) {
      return status;
    }
    memcpy(addr_, data, length);
    addr_ += length;
    return absl::OkStatus();
  }

  absl::Status SerializeLengthDelimitedHeader(int field_number, size_t length) {
    if (auto status = SerializeTag(field_number, WireType::kLengthDelimited);
        !status.ok()) {
      return status;
    }
    return SerializeRawVarint<int32_t, false>(length);
  }

  absl::Status SerializeRaw(const void *data, size_t length) {
    if (auto status = HasSpaceFor(length); !status.ok()) {
      return status;
    }
    memcpy(addr_, data, length);
    addr_ += length;
    return absl::OkStatus();
  }

  // Deserialization functions.
  // Deserialize a tag and return the field number and wire type.
  absl::StatusOr<uint32_t> DeserializeTag() {
    return DeserializeVarint<uint32_t, false>();
  }

  absl::Status SkipVarint() {
    for (;;) {
      if (absl::Status status = Check(1); !status.ok()) {
        return status;
      }
      if ((*addr_++ & 0x80) == 0) {
        return absl::OkStatus();
      }
    }
  }

  absl::Status SkipTag(uint32_t tag) {
    WireType wire_type = WireType(tag & kWireTypeMask);
    tag >>= kFieldIdShift;
    switch (wire_type) {
    case WireType::kVarint:
      if (absl::Status status = SkipVarint(); !status.ok()) {
        return status;
      }
      break;
    case WireType::kFixed64:
      if (absl::Status status = Check(8); !status.ok()) {
        return status;
      }
      addr_ += 8;
      break;
    case WireType::kLengthDelimited: {
      absl::StatusOr<uint32_t> length = DeserializeVarint<uint32_t, false>();
      if (!length.ok()) {
        return length.status();
      }
      if (absl::Status status = Check(*length); !status.ok()) {
        return status;
      }
      addr_ += *length;
      break;
    }
    case WireType::kStartGroup:
    case WireType::kEndGroup:
      return absl::InternalError("Unsupported wire type");
    case WireType::kFixed32:
      if (absl::Status status = Check(4); !status.ok()) {
        return status;
      }
      addr_ += 4;
      break;
    }

    return absl::OkStatus();
  }

  // Tag has already been read.
  template <typename T, bool Signed> absl::StatusOr<T> DeserializeVarint() {
    uint32_t value = 0;
    for (int shift = 0; shift < sizeof(T) * 8; shift += 7) {
      if (absl::Status status = Check(1); !status.ok()) {
        return status;
      }
      uint32_t byte = *addr_++;
      value |= (byte & 0x7f) << shift;
      if ((byte & 0x80) == 0) {
        return Signed ? ZagZig(value) : value;
      }
    }
    return absl::InternalError("Varint too long");
  }

  template <typename T> absl::StatusOr<T> DeserializeFixed() {
    if (absl::Status status = Check(sizeof(T)); !status.ok()) {
      return status;
    }
    T value;
    memcpy(&value, addr_, sizeof(T));
    addr_ += sizeof(T);
    return value;
  }

  absl::StatusOr<absl::Span<char>> DeserializeLengthDelimited() {
    absl::StatusOr<int32_t> length = DeserializeVarint<int32_t, false>();
    if (!length.ok()) {
      return length.status();
    }
    if (absl::Status status = Check(*length); !status.ok()) {
      return status;
    }
    absl::Span<char> span(addr_, *length);
    addr_ += *length;
    return span;
  }

  absl::StatusOr<std::string_view> DeserializeString() {
    absl::StatusOr<int32_t> length = DeserializeVarint<int32_t, false>();
    if (!length.ok()) {
      return length.status();
    }
    if (absl::Status status = Check(*length); !status.ok()) {
      return status;
    }
    std::string_view str(addr_, *length);
    addr_ += *length;
    return str;
  }

  absl::Status CopyRaw(char *dest, size_t length) {
    if (absl::Status status = Check(length); !status.ok()) {
      return status;
    }
    memcpy(dest, addr_, length);
    addr_ += length;
    return absl::OkStatus();
  }

private:
  size_t static MakeTag(int field_number, WireType wire_type) {
    return static_cast<size_t>((field_number << kFieldIdShift) |
                               int(wire_type));
  }

  absl::Status HasSpaceFor(size_t n) {
    char *next = addr_ + n;
    // Off-by-one complexity here.  The end is one past the end of the buffer.
    if (next > end_) {
      if (owned_) {
        // Expand the buffer.
        size_t new_size = size_ * 2;

        char *new_start = reinterpret_cast<char *>(realloc(start_, new_size));
        if (new_start == nullptr) {
          abort();
        }
        size_t curr_length = addr_ - start_;
        start_ = new_start;
        addr_ = start_ + curr_length;
        end_ = start_ + new_size;
        size_ = new_size;
        return absl::OkStatus();
      }
      return absl::InternalError(absl::StrFormat(
          "No space in buffer: length: %d, need: %d", size_, next - start_));
    }
    return absl::OkStatus();
  }

  absl::Status Check(size_t n) {
    char *next = addr_ + n;
    if (next <= end_) {
      return absl::OkStatus();
    }
    return absl::InternalError("End of buffer");
  }

  bool owned_ = false;    // Memory is owned by this buffer.
  char *start_ = nullptr; //
  size_t size_ = 0;
  char *addr_ = nullptr;
  char *end_ = nullptr;
};

} // namespace phaser
