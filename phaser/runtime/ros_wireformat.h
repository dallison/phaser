// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"

namespace phaser {

// A non-owning sequential reader for ROS1 wire data.
class ROSReader {
 public:
  explicit ROSReader(absl::Span<const char> data) : data_(data) {}
  explicit ROSReader(std::string_view data)
      : data_(data.data(), data.size()) {}
  explicit ROSReader(const std::string& data)
      : ROSReader(std::string_view(data)) {}
  ROSReader(std::string&&) = delete;

  size_t Position() const { return position_; }
  size_t Remaining() const { return data_.size() - position_; }
  bool Eof() const { return position_ == data_.size(); }

  template <typename T>
  absl::StatusOr<T> Read() {
    static_assert(std::is_arithmetic_v<T>,
                  "ROSReader::Read only supports arithmetic types");
    if (Remaining() < sizeof(T)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Truncated ROS input at byte %d: need %d bytes, have %d", position_,
          sizeof(T), Remaining()));
    }

    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
      const uint8_t value = static_cast<uint8_t>(
          static_cast<unsigned char>(data_[position_++]));
      if (value > 1) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Invalid ROS bool value %d at byte %d", value, position_ - 1));
      }
      return value != 0;
    } else if constexpr (std::is_integral_v<T>) {
      using U = std::make_unsigned_t<T>;
      U value = 0;
      for (size_t i = 0; i < sizeof(U); ++i) {
        const U byte = static_cast<U>(
            static_cast<unsigned char>(data_[position_++]));
        value |= static_cast<U>(static_cast<uint64_t>(byte)
                                << static_cast<unsigned>(i * 8));
      }
      T result = 0;
      memcpy(&result, &value, sizeof(result));
      return result;
    } else {
      using U = std::conditional_t<sizeof(T) == sizeof(uint32_t), uint32_t,
                                   uint64_t>;
      static_assert(sizeof(U) == sizeof(T));
      U bits = 0;
      for (size_t i = 0; i < sizeof(U); ++i) {
        const U byte = static_cast<U>(
            static_cast<unsigned char>(data_[position_++]));
        bits |= static_cast<U>(static_cast<uint64_t>(byte)
                               << static_cast<unsigned>(i * 8));
      }
      T value = 0;
      memcpy(&value, &bits, sizeof(value));
      return value;
    }
  }

  template <typename T>
  absl::Status ReadArray(absl::Span<T> values) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                  "ROSReader::ReadArray only supports primitive types");
    constexpr size_t kWireElementSize =
        std::is_enum_v<T> ? sizeof(int32_t) : sizeof(T);
    if constexpr (std::is_enum_v<T>) {
      static_assert(sizeof(T) == sizeof(int32_t),
                    "ROS enum storage must be 32 bits");
    }
    if (values.size() >
        std::numeric_limits<size_t>::max() / kWireElementSize) {
      return absl::InvalidArgumentError("ROS array byte size overflow");
    }
    const size_t byte_size = values.size() * kWireElementSize;
    if (Remaining() < byte_size) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Truncated ROS array at byte %d: need %d bytes, have %d", position_,
          byte_size, Remaining()));
    }
    if (values.empty()) {
      return absl::OkStatus();
    }

    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
      static_assert(sizeof(bool) == sizeof(uint8_t));
      for (size_t i = 0; i < values.size(); ++i) {
        const uint8_t value = static_cast<uint8_t>(
            static_cast<unsigned char>(data_[position_ + i]));
        if (value > 1) {
          return absl::InvalidArgumentError(absl::StrFormat(
              "Invalid ROS bool value %d at byte %d", value, position_ + i));
        }
      }
      for (size_t i = 0; i < values.size(); ++i) {
        values[i] = data_[position_ + i] != 0;
      }
      position_ += byte_size;
      return absl::OkStatus();
    }

    const uint16_t endian_marker = 1;
    const bool little_endian =
        *reinterpret_cast<const uint8_t*>(&endian_marker) == 1;
    if (little_endian) {
      memcpy(values.data(), data_.data() + position_, byte_size);
      position_ += byte_size;
      return absl::OkStatus();
    }

    if constexpr (std::is_enum_v<T>) {
      for (T& value : values) {
        absl::StatusOr<int32_t> decoded = Read<int32_t>();
        if (!decoded.ok()) {
          return decoded.status();
        }
        value = static_cast<T>(*decoded);
      }
    } else {
      for (T& value : values) {
        absl::StatusOr<T> decoded = Read<T>();
        if (!decoded.ok()) {
          return decoded.status();
        }
        value = *decoded;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<std::string_view> ReadString() {
    absl::StatusOr<uint32_t> length = Read<uint32_t>();
    if (!length.ok()) {
      return length.status();
    }
    if (*length > Remaining()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Truncated ROS string at byte %d: length %d exceeds remaining %d",
          position_, *length, Remaining()));
    }
    const std::string_view value(data_.data() + position_, *length);
    position_ += *length;
    return value;
  }

  absl::StatusOr<uint32_t> ReadSequenceLength() {
    return Read<uint32_t>();
  }

 private:
  absl::Span<const char> data_;
  size_t position_ = 0;
};

// A sequential ROS1 serialization buffer. ROS1 primitives are always encoded
// little-endian, strings and variable-length sequences use uint32 length
// prefixes, and messages do not carry tags or alignment padding.
class ROSBuffer {
 public:
  explicit ROSBuffer(size_t initial_size = 16)
      : owned_(true), capacity_(std::max<size_t>(initial_size, 16)) {
    data_ = static_cast<char*>(malloc(capacity_));
    allocation_failed_ = data_ == nullptr;
  }

  ROSBuffer(void* data, size_t size)
      : data_(static_cast<char*>(data)), capacity_(size) {}

  ROSBuffer(const ROSBuffer&) = delete;
  ROSBuffer& operator=(const ROSBuffer&) = delete;

  ROSBuffer(ROSBuffer&& other) noexcept { MoveFrom(std::move(other)); }

  ROSBuffer& operator=(ROSBuffer&& other) noexcept {
    if (this != &other) {
      Release();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  ~ROSBuffer() { Release(); }

  size_t Size() const { return size_; }
  size_t size() const { return Size(); }
  size_t Capacity() const { return capacity_; }
  bool empty() const { return size_ == 0; }

  char* Data() { return data_; }
  const char* Data() const { return data_; }
  char* data() { return Data(); }
  const char* data() const { return Data(); }

  std::string AsString() const {
    return data_ == nullptr ? std::string() : std::string(data_, size_);
  }

  absl::Span<const char> AsSpan() const {
    return absl::Span<const char>(data_, size_);
  }

  void Clear() { size_ = 0; }

  absl::Status WriteRaw(const void* source, size_t length) {
    if (length == 0) {
      return absl::OkStatus();
    }
    if (source == nullptr) {
      return absl::InvalidArgumentError(
          "Cannot write non-empty data from a null pointer");
    }
    if (absl::Status status = EnsureSpace(length); !status.ok()) {
      return status;
    }
    memcpy(data_ + size_, source, length);
    size_ += length;
    return absl::OkStatus();
  }

  absl::Status WriteZeros(size_t length) {
    if (absl::Status status = EnsureSpace(length); !status.ok()) {
      return status;
    }
    if (length != 0) {
      memset(data_ + size_, 0, length);
      size_ += length;
    }
    return absl::OkStatus();
  }

  template <typename T>
  absl::Status WriteArray(absl::Span<const T> values) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                  "ROSBuffer::WriteArray only supports primitive types");
    constexpr size_t kWireElementSize =
        std::is_enum_v<T> ? sizeof(int32_t) : sizeof(T);
    if constexpr (std::is_enum_v<T>) {
      static_assert(sizeof(T) == sizeof(int32_t),
                    "ROS enum storage must be 32 bits");
    }
    if (values.size() >
        std::numeric_limits<size_t>::max() / kWireElementSize) {
      return absl::ResourceExhaustedError("ROS array byte size overflow");
    }
    const size_t byte_size = values.size() * kWireElementSize;
    if (absl::Status status = EnsureSpace(byte_size); !status.ok()) {
      return status;
    }
    if (values.empty()) {
      return absl::OkStatus();
    }

    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
      static_assert(sizeof(bool) == sizeof(uint8_t));
      for (bool value : values) {
        data_[size_++] = static_cast<char>(value ? 1 : 0);
      }
      return absl::OkStatus();
    }

    const uint16_t endian_marker = 1;
    const bool little_endian =
        *reinterpret_cast<const uint8_t*>(&endian_marker) == 1;
    if (little_endian) {
      memcpy(data_ + size_, values.data(), byte_size);
      size_ += byte_size;
      return absl::OkStatus();
    }

    for (const T& value : values) {
      absl::Status status;
      if constexpr (std::is_enum_v<T>) {
        status = Write(static_cast<int32_t>(value));
      } else {
        status = Write(value);
      }
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status WriteString(std::string_view value) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) {
      return absl::InvalidArgumentError("ROS1 string exceeds uint32 length");
    }
    const size_t total = sizeof(uint32_t) + value.size();
    if (absl::Status status = EnsureSpace(total); !status.ok()) {
      return status;
    }
    WriteLittleEndianUnchecked(static_cast<uint32_t>(value.size()));
    if (!value.empty()) {
      memcpy(data_ + size_, value.data(), value.size());
      size_ += value.size();
    }
    return absl::OkStatus();
  }

  absl::Status WriteSequenceLength(size_t length) {
    if (length > std::numeric_limits<uint32_t>::max()) {
      return absl::InvalidArgumentError(
          "ROS1 sequence exceeds uint32 length");
    }
    return Write(static_cast<uint32_t>(length));
  }

  absl::Status Write(bool value) {
    return Write(static_cast<uint8_t>(value ? 1 : 0));
  }

  template <typename T,
            std::enable_if_t<std::is_integral_v<T> &&
                                 !std::is_same_v<std::remove_cv_t<T>, bool>,
                             int> = 0>
  absl::Status Write(T value) {
    using U = std::make_unsigned_t<T>;
    if (absl::Status status = EnsureSpace(sizeof(T)); !status.ok()) {
      return status;
    }
    WriteLittleEndianUnchecked(static_cast<U>(value));
    return absl::OkStatus();
  }

  absl::Status Write(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    memcpy(&bits, &value, sizeof(bits));
    return Write(bits);
  }

  absl::Status Write(double value) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    memcpy(&bits, &value, sizeof(bits));
    return Write(bits);
  }

 private:
  absl::Status EnsureSpace(size_t length) {
    if (allocation_failed_) {
      return absl::ResourceExhaustedError(
          "Unable to allocate ROS serialization buffer");
    }
    if (data_ == nullptr && capacity_ != 0) {
      return absl::InvalidArgumentError("ROS output buffer is null");
    }
    if (length > std::numeric_limits<size_t>::max() - size_) {
      return absl::ResourceExhaustedError("ROS output size overflow");
    }
    const size_t required = size_ + length;
    if (required <= capacity_) {
      return absl::OkStatus();
    }
    if (!owned_) {
      return absl::ResourceExhaustedError(absl::StrFormat(
          "No space in ROS output buffer: capacity %d, need %d", capacity_,
          required));
    }

    size_t new_capacity = capacity_;
    while (new_capacity < required) {
      if (new_capacity > std::numeric_limits<size_t>::max() / 2) {
        new_capacity = required;
        break;
      }
      new_capacity *= 2;
    }
    void* replacement = realloc(data_, new_capacity);
    if (replacement == nullptr) {
      allocation_failed_ = true;
      return absl::ResourceExhaustedError(
          "Unable to grow ROS serialization buffer");
    }
    data_ = static_cast<char*>(replacement);
    capacity_ = new_capacity;
    return absl::OkStatus();
  }

  template <typename U>
  void WriteLittleEndianUnchecked(U value) {
    static_assert(std::is_unsigned_v<U>);
    for (size_t i = 0; i < sizeof(U); ++i) {
      data_[size_++] = static_cast<char>(value & static_cast<U>(0xff));
      if constexpr (sizeof(U) > 1) {
        value >>= 8;
      }
    }
  }

  void Release() {
    if (owned_) {
      free(data_);
    }
  }

  void MoveFrom(ROSBuffer&& other) {
    owned_ = other.owned_;
    allocation_failed_ = other.allocation_failed_;
    data_ = other.data_;
    capacity_ = other.capacity_;
    size_ = other.size_;

    other.owned_ = false;
    other.allocation_failed_ = false;
    other.data_ = nullptr;
    other.capacity_ = 0;
    other.size_ = 0;
  }

  bool owned_ = false;
  bool allocation_failed_ = false;
  char* data_ = nullptr;
  size_t capacity_ = 0;
  size_t size_ = 0;
};

}  // namespace phaser
