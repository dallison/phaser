// Copyright 2024-2026 David Allison
// All Rights Reserved.
// See LICENSE file for licensing information.

#pragma once

// ROS serialization and parsing of messages in a mutable buffer, straight
// from and to the payload without a handle per message.  This process built
// the buffer, so every message in it has its compiled layout and no offset
// needs checking.

#include <stdint.h>
#include <string.h>

#include <algorithm>
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

// Generated messages whose fields are all scalars, enums or strings have
// static PhaserDeserializeFromROSAt and PhaserROSMinSize functions.
template <typename T, typename = void>
struct HasROSParseAt : std::false_type {};

template <typename T>
struct HasROSParseAt<T,
                     std::void_t<decltype(&T::PhaserDeserializeFromROSAt)>>
    : std::true_type {};

// Those of them of a fixed ROS size also have PhaserDeserializeFromROSUncheckedAt
// for input known to hold them.
template <typename T, typename = void>
struct HasROSUncheckedParseAt : std::false_type {};

template <typename T>
struct HasROSUncheckedParseAt<
    T, std::void_t<decltype(&T::PhaserDeserializeFromROSUncheckedAt)>>
    : std::true_type {};

// The fewest ROS bytes a T read by PhaserDeserializeFromROSAt takes, or 0.
template <typename T>
constexpr size_t ROSParseMinSize() {
  if constexpr (HasROSParseAt<T>::value) {
    return T::PhaserROSMinSize();
  } else {
    return 0;
  }
}

// Appends 'count' elements to 'vector', a vector of T in a message of
// 'runtime', reading each from 'reader'.
template <typename T, typename Vector>
inline absl::Status DeserializeMessageVectorFromROS(
    const RuntimeHandle& runtime, Vector& vector, uint32_t count,
    ::phaser::ROSReader& reader) {
  [[maybe_unused]] const bool direct =
      HasROSParseAt<T>::value && runtime->IsMutable();
  if constexpr (ROSParseMinSize<T>() > 0) {
    // The input bounds the elements this allocates.  A count it cannot
    // hold is truncated input, which the loop below reports.
    if (direct && count <= reader.Remaining() / ROSParseMinSize<T>()) {
      if constexpr (HasROSUncheckedParseAt<T>::value) {
        // Every element is ROSParseMinSize<T>() bytes, so all are in the
        // input.
        constexpr size_t kSize = ROSParseMinSize<T>();
        const char* input = reader.ReadRawUnchecked(size_t{count} * kSize);
        vector.AddElementsInPlace(count, [input](char* element, uint32_t i) {
          T::PhaserDeserializeFromROSUncheckedAt(element,
                                                 input + size_t{i} * kSize);
        });
        return absl::OkStatus();
      } else {
        return vector.AddElements(count, [&](::phaser::BufferOffset element) {
          return T::PhaserDeserializeFromROSAt(runtime, element, reader);
        });
      }
    }
  }
  // A hostile count can only reserve one element per four bytes left in the
  // input.  Smaller elements still fit by growing.
  vector.reserve(std::min<size_t>(count, reader.Remaining() / 4));
  for (uint32_t i = 0; i < count; ++i) {
    absl::Status status;
    if constexpr (HasROSParseAt<T>::value) {
      if (direct) {
        status = T::PhaserDeserializeFromROSAt(runtime, vector.AddElement(),
                                               reader);
      } else {
        status = vector.Add().DeserializeFromROSUncleared(reader);
      }
    } else {
      status = vector.Add().DeserializeFromROSUncleared(reader);
    }
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
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
