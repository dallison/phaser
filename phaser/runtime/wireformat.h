// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <stdint.h>
#include <string.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "toolbelt/payload_buffer.h"

namespace phaser {

enum class MessageWireFormat {
  kProtobuf,
  kPhaser,
  kUnknown,
  kAmbiguous,
};

namespace internal {

inline uint32_t LoadWireUint32(absl::Span<const char> data, size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint32_t>(
                 static_cast<unsigned char>(data[offset + i]))
             << static_cast<unsigned>(i * 8);
  }
  return value;
}

inline bool ConsumeWireVarint(absl::Span<const char> data, size_t* offset,
                              size_t max_bytes, uint8_t max_last_byte,
                              uint64_t* value) {
  uint64_t result = 0;
  for (size_t i = 0; i < max_bytes; ++i) {
    if (*offset >= data.size()) {
      return false;
    }
    const uint8_t byte =
        static_cast<uint8_t>(static_cast<unsigned char>(data[(*offset)++]));
    if (i + 1 == max_bytes &&
        ((byte & 0x80U) != 0 || (byte & 0x7fU) > max_last_byte)) {
      return false;
    }
    result |= static_cast<uint64_t>(byte & 0x7fU)
              << static_cast<unsigned>(i * 7);
    if ((byte & 0x80U) == 0) {
      *value = result;
      return true;
    }
  }
  return false;
}

inline bool ConsumeProtobufFields(absl::Span<const char> data, size_t* offset,
                                  uint32_t expected_end_group = 0,
                                  size_t depth = 0) {
  constexpr uint32_t kMaxFieldNumber = (uint32_t{1} << 29) - 1;
  constexpr size_t kMaxGroupDepth = 100;
  if (depth > kMaxGroupDepth) {
    return false;
  }
  while (*offset < data.size()) {
    uint64_t tag = 0;
    if (!ConsumeWireVarint(data, offset, 5, 0x0f, &tag)) {
      return false;
    }
    const uint32_t field_number = static_cast<uint32_t>(tag >> 3);
    const uint32_t wire_type = static_cast<uint32_t>(tag & 7);
    if (field_number == 0 || field_number > kMaxFieldNumber) {
      return false;
    }
    if (wire_type == 4) {
      return expected_end_group != 0 && field_number == expected_end_group;
    }

    uint64_t value = 0;
    switch (wire_type) {
      case 0:
        if (!ConsumeWireVarint(data, offset, 10, 1, &value)) {
          return false;
        }
        break;
      case 1:
        if (data.size() - *offset < sizeof(uint64_t)) {
          return false;
        }
        *offset += sizeof(uint64_t);
        break;
      case 2:
        if (!ConsumeWireVarint(data, offset, 10, 1, &value) ||
            value > data.size() - *offset) {
          return false;
        }
        *offset += static_cast<size_t>(value);
        break;
      case 3:
        if (!ConsumeProtobufFields(data, offset, field_number, depth + 1)) {
          return false;
        }
        break;
      case 5:
        if (data.size() - *offset < sizeof(uint32_t)) {
          return false;
        }
        *offset += sizeof(uint32_t);
        break;
      default:
        return false;
    }
  }
  return expected_end_group == 0;
}

inline bool IsStructurallyValidProtobuf(absl::Span<const char> data) {
  size_t offset = 0;
  return ConsumeProtobufFields(data, &offset) && offset == data.size();
}

inline bool IsStructurallyValidPhaser(absl::Span<const char> data) {
  constexpr size_t kMagicOffset =
      offsetof(::toolbelt::PayloadBuffer, magic);
  constexpr size_t kMessageOffset =
      offsetof(::toolbelt::PayloadBuffer, message);
  constexpr size_t kHwmOffset = offsetof(::toolbelt::PayloadBuffer, hwm);
  constexpr size_t kFullSizeOffset =
      offsetof(::toolbelt::PayloadBuffer, full_size);
  constexpr size_t kFreeListOffset =
      offsetof(::toolbelt::PayloadBuffer, free_list);
  constexpr size_t kMetadataOffset =
      offsetof(::toolbelt::PayloadBuffer, metadata);
  constexpr size_t kBitmapsOffset =
      offsetof(::toolbelt::PayloadBuffer, bitmaps);

  if (data.size() < sizeof(::toolbelt::PayloadBuffer)) {
    return false;
  }
  const uint32_t magic = LoadWireUint32(data, kMagicOffset);
  const uint32_t base_magic = magic & ::toolbelt::kBitMapMask;
  const bool movable = base_magic == ::toolbelt::kMovableBufferMagic;
  if (!movable && base_magic != ::toolbelt::kFixedBufferMagic) {
    return false;
  }

  const size_t minimum_header =
      sizeof(::toolbelt::PayloadBuffer) +
      (movable ? sizeof(::toolbelt::Resizer*) : 0);
  const uint32_t message = LoadWireUint32(data, kMessageOffset);
  const uint32_t hwm = LoadWireUint32(data, kHwmOffset);
  const uint32_t full_size = LoadWireUint32(data, kFullSizeOffset);
  const uint32_t free_list = LoadWireUint32(data, kFreeListOffset);
  const uint32_t metadata = LoadWireUint32(data, kMetadataOffset);

  if (full_size < minimum_header || hwm < minimum_header ||
      hwm > full_size || hwm > data.size() || message < minimum_header ||
      (message & 7U) != 0 || message > hwm - sizeof(uint32_t)) {
    return false;
  }
  if (free_list != 0 &&
      (free_list < minimum_header ||
       free_list > full_size - sizeof(::toolbelt::FreeBlockHeader))) {
    return false;
  }
  if (metadata != 0 &&
      (metadata < minimum_header || metadata >= hwm)) {
    return false;
  }
  for (size_t i = 0; i < ::toolbelt::kNumBitmapRuns; ++i) {
    const uint32_t bitmap =
        LoadWireUint32(data, kBitmapsOffset + i * sizeof(uint32_t));
    if (bitmap != 0 && (bitmap < minimum_header || bitmap >= hwm)) {
      return false;
    }
  }
  return true;
}

}  // namespace internal

// Infers a format from its complete byte representation. Both formats are
// validated structurally; the magic prefix alone is deliberately insufficient
// because it can also begin a valid protobuf field tag.
inline MessageWireFormat InferMessageWireFormat(
    absl::Span<const char> data) {
  const bool phaser = internal::IsStructurallyValidPhaser(data);
  const bool protobuf = internal::IsStructurallyValidProtobuf(data);
  if (phaser && protobuf) {
    return MessageWireFormat::kAmbiguous;
  }
  if (phaser) {
    return MessageWireFormat::kPhaser;
  }
  if (protobuf) {
    return MessageWireFormat::kProtobuf;
  }
  return MessageWireFormat::kUnknown;
}

inline MessageWireFormat InferMessageWireFormat(std::string_view data) {
  return InferMessageWireFormat(absl::Span<const char>(data.data(), data.size()));
}

inline MessageWireFormat InferMessageWireFormat(const std::string& data) {
  return InferMessageWireFormat(std::string_view(data));
}

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
    start_ = reinterpret_cast<char*>(malloc(size_));
    if (start_ == nullptr) {
      abort();
    }
    // Initialize the buffer so unwritten regions are never read as
    // uninitialised memory (avoids spurious valgrind reports).
    memset(start_, 0, size_);
    addr_ = start_;
    end_ = start_ + size_;
  }

  // Fixed buffer in non-owned memory.
  ProtoBuffer(char* addr, size_t size)
      : owned_(false),
        start_(addr),
        size_(size),
        addr_(addr),
        end_(addr_ + size) {}

  ProtoBuffer(const char* addr, size_t size)
      : owned_(false),
        start_(const_cast<char*>(addr)),
        size_(size),
        addr_(const_cast<char*>(addr)),
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

  size_t Size() const { return static_cast<size_t>(addr_ - start_); }

  size_t size() const { return Size(); }

  template <typename T>
  T* Data() {
    return reinterpret_cast<T*>(start_);
  }

  char* data() { return Data<char>(); }

  std::string AsString() const {
    return std::string(start_, static_cast<size_t>(addr_ - start_));
  }

  template <typename T>
  absl::Span<const T> AsSpan() const {
    return absl::Span<T>(reinterpret_cast<const T*>(start_),
                         static_cast<size_t>(addr_ - start_));
  }

  bool Eof() const { return addr_ == end_; }

  void Clear() {
    addr_ = start_;
    end_ = start_;
  }

  // ZigZag maps a signed integer to an unsigned representation in which small
  // magnitudes (positive or negative) become small values, as required by the
  // protobuf sint32/sint64 wire types. The math is done on the unsigned type so
  // it is free of signed overflow/shift undefined behavior and is correct for
  // any width of T (the previous implementation hard-coded a 31-bit shift and
  // produced wrong results for 64-bit values).
  template <typename T>
  static T ZigZag(T value) {
    using U = std::make_unsigned_t<T>;
    constexpr unsigned kSignShift = sizeof(T) * 8 - 1;
    const U u = static_cast<U>(value);
    return static_cast<T>((u << 1) ^ static_cast<U>(-(u >> kSignShift)));
  }
  template <typename T>
  static T ZagZig(T value) {
    using U = std::make_unsigned_t<T>;
    const U u = static_cast<U>(value);
    return static_cast<T>((u >> 1) ^ static_cast<U>(-(u & U(1))));
  }

  template <typename T>
  static constexpr WireType FixedWireType() {
    if (sizeof(T) == 4) {
      return WireType::kFixed32;
    } else if (sizeof(T) == 8) {
      return WireType::kFixed64;
    }
    abort();
  }

  // Size functions.
  static size_t TagSize(int field_number, WireType wire_type) {
    return VarintSize<int32_t, false>(
        static_cast<int32_t>(MakeTag(field_number, wire_type)));
  }

  template <typename T, bool Signed>
  static uint64_t ToVarintWire(T value) {
    if constexpr (Signed) {
      return static_cast<uint64_t>(ZigZag(value));
    }
    if constexpr (std::is_same_v<T, bool>) {
      return static_cast<uint8_t>(value);
    }
    if constexpr (std::is_signed_v<T>) {
      // Protobuf int32/int64 varints sign-extend to 64 bits when negative.
      return static_cast<uint64_t>(static_cast<int64_t>(value));
    }
    return static_cast<uint64_t>(value);
  }

  template <typename T, bool Signed>
  static size_t VarintSize(T value) {
    uint64_t uvalue = ToVarintWire<T, Signed>(value);
    size_t size = 0;
    for (;;) {
      if ((uvalue & ~uint64_t(0x7f)) == 0) {
        return size + 1;
      }
      size++;
      uvalue >>= 7;
    }
  }

  inline static size_t LengthDelimitedSize(int field_number, size_t length) {
    return TagSize(field_number, WireType::kLengthDelimited) +
           VarintSize<int32_t, false>(static_cast<int32_t>(length)) + length;
  }

  inline static size_t StringSize(int field_number, std::string_view str) {
    return LengthDelimitedSize(field_number, str.size());
  }

  // Serialization functions.

  template <typename T, bool Signed>
  absl::Status SerializeRawVarint(T value) {
    uint64_t uvalue = ToVarintWire<T, Signed>(value);
    if (auto status = HasSpaceFor(VarintSize<uint64_t, false>(uvalue));
        !status.ok()) {
      return status;
    }
    for (;;) {
      if ((uvalue & ~uint64_t(0x7f)) == 0) {
        *addr_++ = static_cast<char>(uvalue);
        break;
      }
      *addr_++ = static_cast<char>((uvalue & 0x7f) | 0x80);
      uvalue >>= 7;
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
        static_cast<uint32_t>(MakeTag(field_number, wire_type)));
  }

  template <typename T>
  absl::Status SerializeFixed(int field_number, T value) {
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

  absl::Status SerializeLengthDelimited(int field_number, const void* data,
                                        size_t length) {
    if (auto status = SerializeTag(field_number, WireType::kLengthDelimited);
        !status.ok()) {
      return status;
    }
    if (absl::Status status =
            SerializeRawVarint<int32_t, false>(static_cast<int32_t>(length));
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
    return SerializeRawVarint<int32_t, false>(static_cast<int32_t>(length));
  }

  absl::Status SerializeRaw(const void* data, size_t length) {
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
  template <typename T, bool Signed>
  absl::StatusOr<T> DeserializeVarint() {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
      if (absl::Status status = Check(1); !status.ok()) {
        return status;
      }
      uint64_t byte = static_cast<uint8_t>(*addr_++);
      value |= (byte & 0x7f) << shift;
      if ((byte & 0x80) == 0) {
        if constexpr (Signed) {
          if constexpr (std::is_same_v<T, bool>) {
            return static_cast<T>(value != 0);
          } else {
            using ST = std::make_signed_t<T>;
            return static_cast<T>(ZagZig(static_cast<ST>(value)));
          }
        } else {
          return static_cast<T>(value);
        }
      }
    }
    return absl::InternalError("Varint too long");
  }

  template <typename T>
  absl::StatusOr<T> DeserializeFixed() {
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
    const size_t len = static_cast<size_t>(*length);
    if (absl::Status status = Check(len); !status.ok()) {
      return status;
    }
    absl::Span<char> span(addr_, len);
    addr_ += len;
    return span;
  }

  absl::StatusOr<std::string_view> DeserializeString() {
    absl::StatusOr<int32_t> length = DeserializeVarint<int32_t, false>();
    if (!length.ok()) {
      return length.status();
    }
    const size_t len = static_cast<size_t>(*length);
    if (absl::Status status = Check(len); !status.ok()) {
      return status;
    }
    std::string_view str(addr_, len);
    addr_ += len;
    return str;
  }

  absl::Status CopyRaw(char* dest, size_t length) {
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
    char* next = addr_ + n;
    // Off-by-one complexity here.  The end is one past the end of the buffer.
    if (next > end_) {
      if (owned_) {
        // Expand the buffer.
        size_t new_size = size_ * 2;

        char* new_start = reinterpret_cast<char*>(realloc(start_, new_size));
        if (new_start == nullptr) {
          abort();
        }
        // Zero the newly grown region.
        memset(new_start + size_, 0, new_size - size_);
        size_t curr_length = static_cast<size_t>(addr_ - start_);
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
    char* next = addr_ + n;
    if (next <= end_) {
      return absl::OkStatus();
    }
    return absl::InternalError("End of buffer");
  }

  bool owned_ = false;     // Memory is owned by this buffer.
  char* start_ = nullptr;  //
  size_t size_ = 0;
  char* addr_ = nullptr;
  char* end_ = nullptr;
};

// A protobuf sink that either writes to a ProtoBuffer or only counts bytes.
// The counting mode is used to size nested and packed fields without staging
// their encoded bytes in a temporary allocation.
class ProtoWriter {
 public:
  ProtoWriter() = default;
  explicit ProtoWriter(ProtoBuffer& buffer) : buffer_(&buffer) {}

  size_t Size() const { return size_; }

  template <typename T, bool Signed>
  absl::Status SerializeVarint(int field_number, T value) {
    const size_t bytes =
        ProtoBuffer::TagSize(field_number, WireType::kVarint) +
        ProtoBuffer::VarintSize<T, Signed>(value);
    if (buffer_ != nullptr) {
      if (absl::Status status =
              buffer_->SerializeVarint<T, Signed>(field_number, value);
          !status.ok()) {
        return status;
      }
    }
    size_ += bytes;
    return absl::OkStatus();
  }

  template <typename T, bool Signed>
  absl::Status SerializeRawVarint(T value) {
    const size_t bytes = ProtoBuffer::VarintSize<T, Signed>(value);
    if (buffer_ != nullptr) {
      if (absl::Status status = buffer_->SerializeRawVarint<T, Signed>(value);
          !status.ok()) {
        return status;
      }
    }
    size_ += bytes;
    return absl::OkStatus();
  }

  template <typename T>
  absl::Status SerializeFixed(int field_number, T value) {
    const size_t bytes =
        ProtoBuffer::TagSize(field_number, ProtoBuffer::FixedWireType<T>()) +
        sizeof(T);
    if (buffer_ != nullptr) {
      if (absl::Status status = buffer_->SerializeFixed(field_number, value);
          !status.ok()) {
        return status;
      }
    }
    size_ += bytes;
    return absl::OkStatus();
  }

  absl::Status SerializeLengthDelimited(int field_number, const void* data,
                                        size_t length) {
    const size_t bytes = ProtoBuffer::LengthDelimitedSize(field_number, length);
    if (buffer_ != nullptr) {
      if (absl::Status status =
              buffer_->SerializeLengthDelimited(field_number, data, length);
          !status.ok()) {
        return status;
      }
    }
    size_ += bytes;
    return absl::OkStatus();
  }

  absl::Status SerializeLengthDelimitedHeader(int field_number,
                                              size_t length) {
    const size_t bytes = ProtoBuffer::LengthDelimitedSize(field_number, length) -
                         length;
    if (buffer_ != nullptr) {
      if (absl::Status status =
              buffer_->SerializeLengthDelimitedHeader(field_number, length);
          !status.ok()) {
        return status;
      }
    }
    size_ += bytes;
    return absl::OkStatus();
  }

  absl::Status SerializeRaw(const void* data, size_t length) {
    if (buffer_ != nullptr) {
      if (absl::Status status = buffer_->SerializeRaw(data, length);
          !status.ok()) {
        return status;
      }
    }
    size_ += length;
    return absl::OkStatus();
  }

 private:
  ProtoBuffer* buffer_ = nullptr;
  size_t size_ = 0;
};

}  // namespace phaser
