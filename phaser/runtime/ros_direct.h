// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#pragma once

// ROS serialization of messages in a mutable buffer, read straight from the
// payload without a handle per message.  This process built the buffer, so
// every message in it has its compiled layout and no offset needs checking.

#include <stdint.h>
#include <string.h>

#include <string>
#include <string_view>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/payload_buffer.h"
#include "phaser/runtime/ros_wireformat.h"

namespace phaser::internal {

template <typename T>
inline T LoadAt(const ::phaser::PayloadBuffer* pb,
                ::phaser::BufferOffset offset) {
  T value;
  memcpy(&value, reinterpret_cast<const char*>(pb) + offset, sizeof(value));
  return value;
}

// The string whose StringHeader is at 'header'.
inline std::string_view StringAt(const ::phaser::PayloadBuffer* pb,
                                 ::phaser::BufferOffset header) {
  if (header == 0) {
    return {};
  }
  const auto block = LoadAt<::phaser::BufferOffset>(pb, header);
  if (block == 0) {
    return {};
  }
  return std::string_view(
      reinterpret_cast<const char*>(pb) + block + sizeof(uint32_t),
      LoadAt<uint32_t>(pb, block));
}

inline uint32_t VectorSizeAt(const ::phaser::PayloadBuffer* pb,
                             ::phaser::BufferOffset header) {
  const auto hdr = LoadAt<::phaser::VectorHeader>(pb, header);
  return hdr.data == 0 ? 0 : hdr.num_elements;
}

// The elements of the vector of T, or of the StringHeaders of a vector of
// strings, whose VectorHeader is at 'header'.
template <typename T>
inline absl::Span<const T> VectorAt(const ::phaser::PayloadBuffer* pb,
                                    ::phaser::BufferOffset header) {
  const auto hdr = LoadAt<::phaser::VectorHeader>(pb, header);
  if (hdr.data == 0) {
    return {};
  }
  return absl::Span<const T>(
      reinterpret_cast<const T*>(reinterpret_cast<const char*>(pb) + hdr.data),
      hdr.num_elements);
}

// The message in the field at 'field', or 0 if the field holds none.
inline ::phaser::BufferOffset MessageAt(const ::phaser::PayloadBuffer* pb,
                                        ::phaser::BufferOffset field,
                                        bool is_inline) {
  const auto word = LoadAt<::phaser::BufferOffset>(pb, field);
  if (is_inline) {
    return word == 0 ? 0 : field;
  }
  return word;
}

// Calls 'f' with the offset of each element of the vector of messages whose
// VectorHeader is at 'header', in order.  If 'f' returns a status, stops at
// the first error and returns it.
template <typename F>
inline auto ForEachMessageVectorElement(const ::phaser::PayloadBuffer* pb,
                                        ::phaser::BufferOffset header, F&& f) {
  using Result = std::invoke_result_t<F, ::phaser::BufferOffset>;
  constexpr bool kReturnsStatus = std::is_same_v<Result, absl::Status>;
  const auto hdr = LoadAt<::phaser::VectorHeader>(pb, header);
  uint32_t remaining = hdr.data == 0 ? 0 : hdr.num_elements;
  if (remaining != 0) {
    const auto block = LoadAt<::phaser::MessageVectorBlock>(pb, hdr.data);
    ::phaser::BufferOffset chunk = static_cast<::phaser::BufferOffset>(
        hdr.data + sizeof(::phaser::MessageVectorBlock));
    uint32_t capacity = block.first_capacity;
    for (uint32_t k = 1;; ++k) {
      const uint32_t n = remaining < capacity ? remaining : capacity;
      for (uint32_t i = 0; i < n; ++i) {
        const auto element =
            static_cast<::phaser::BufferOffset>(chunk + i * block.stride);
        if constexpr (kReturnsStatus) {
          if (absl::Status status = f(element); !status.ok()) {
            return status;
          }
        } else {
          f(element);
        }
      }
      remaining -= n;
      if (remaining == 0) {
        break;
      }
      // Chunk k holds first_capacity << (k - 1) elements, and its offset
      // follows the chunk count in the directory.
      chunk = LoadAt<::phaser::BufferOffset>(
          pb, static_cast<::phaser::BufferOffset>(
                  block.directory + k * sizeof(::phaser::BufferOffset)));
      capacity = block.first_capacity << (k - 1);
    }
  }
  if constexpr (kReturnsStatus) {
    return absl::OkStatus();
  }
}

// Generated messages whose fields can all be read straight from the payload
// have static PhaserSerializeToROSAt and PhaserROSSerializedSizeAt functions.
template <typename T, typename = void>
struct HasROSAt : std::false_type {};

template <typename T>
struct HasROSAt<T, std::void_t<decltype(&T::PhaserSerializeToROSAt)>>
    : std::true_type {};

// The ROS encoding of a default T.  Reading a message's ROS fields caches
// their values in it, so a shared default message cannot be serialized by
// several threads at once.
template <typename T>
inline const std::string& DefaultROSBytes() {
  static const std::string bytes = [] {
    const T message{InternalDefault{}};
    std::string output;
    if (!message.SerializeToROSString(&output).ok()) {
      output.clear();
    }
    return output;
  }();
  return bytes;
}

// Writes the message of type T at 'offset' in the mutable buffer of
// 'runtime', or a default T if 'offset' is 0.
template <typename T>
inline absl::Status SerializeToROSAt(const RuntimeHandle& runtime,
                                     ::phaser::BufferOffset offset,
                                     ::phaser::ROSBuffer& buffer) {
  if (offset == 0) {
    const std::string& bytes = DefaultROSBytes<T>();
    return buffer.WriteRaw(bytes.data(), bytes.size());
  }
  if constexpr (HasROSAt<T>::value) {
    return T::PhaserSerializeToROSAt(runtime, offset, buffer);
  } else {
    return T(BorrowedRuntime(runtime), offset).SerializeToROS(buffer);
  }
}

template <typename T>
inline size_t ROSSerializedSizeAt(const RuntimeHandle& runtime,
                                  ::phaser::BufferOffset offset) {
  if constexpr (T::PhaserROSFixedSize() != ::phaser::kROSVariableSize) {
    return T::PhaserROSFixedSize();
  } else {
    if (offset == 0) {
      return DefaultROSBytes<T>().size();
    }
    if constexpr (HasROSAt<T>::value) {
      return T::PhaserROSSerializedSizeAt(runtime, offset);
    } else {
      return T(BorrowedRuntime(runtime), offset).ROSSerializedSize();
    }
  }
}

// Writes the elements of the vector of T whose VectorHeader is at 'header',
// without the sequence length.
template <typename T>
inline absl::Status SerializeMessageVectorToROS(const RuntimeHandle& runtime,
                                                ::phaser::BufferOffset header,
                                                ::phaser::ROSBuffer& buffer) {
  return ForEachMessageVectorElement(
      runtime->pb, header, [&](::phaser::BufferOffset element) {
        return SerializeToROSAt<T>(runtime, element, buffer);
      });
}

template <typename T>
inline size_t MessageVectorROSSize(const RuntimeHandle& runtime,
                                   ::phaser::BufferOffset header) {
  if constexpr (T::PhaserROSFixedSize() != ::phaser::kROSVariableSize) {
    return size_t{VectorSizeAt(runtime->pb, header)} * T::PhaserROSFixedSize();
  } else {
    size_t size = 0;
    ForEachMessageVectorElement(runtime->pb, header,
                                [&](::phaser::BufferOffset element) {
                                  size += ROSSerializedSizeAt<T>(runtime,
                                                                 element);
                                });
    return size;
  }
}

}  // namespace phaser::internal
