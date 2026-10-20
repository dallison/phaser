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
#include <new>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/payload_buffer.h"
#include "phaser/runtime/ros_wireformat.h"
#include "phaser/runtime/vectors.h"

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

// Those of a fixed ROS size made of scalars also have
// PhaserSerializeToROSUncheckedAt, writing to output known to have room.
template <typename T, typename = void>
struct HasROSUncheckedSerializeAt : std::false_type {};

template <typename T>
struct HasROSUncheckedSerializeAt<
    T, std::void_t<decltype(&T::PhaserSerializeToROSUncheckedAt)>>
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
  if constexpr (HasROSUncheckedSerializeAt<T>::value) {
    absl::StatusOr<char*> output = buffer.Extend(T::PhaserROSFixedSize());
    if (!output.ok()) {
      return output.status();
    }
    T::PhaserSerializeToROSUncheckedAt(
        reinterpret_cast<const char*>(runtime->pb) + offset, *output);
    return absl::OkStatus();
  } else if constexpr (HasROSAt<T>::value) {
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
  if constexpr (HasROSUncheckedSerializeAt<T>::value) {
    constexpr size_t kSize = T::PhaserROSFixedSize();
    const ::phaser::PayloadBuffer* pb = runtime->pb;
    absl::StatusOr<char*> output =
        buffer.Extend(size_t{VectorSizeAt(pb, header)} * kSize);
    if (!output.ok()) {
      return output.status();
    }
    const char* base = reinterpret_cast<const char*>(pb);
    char* out = *output;
    ForEachMessageVectorElement(pb, header,
                                [base, &out](::phaser::BufferOffset element) {
                                  T::PhaserSerializeToROSUncheckedAt(
                                      base + element, out);
                                  out += kSize;
                                });
    return absl::OkStatus();
  } else {
    return ForEachMessageVectorElement(
        runtime->pb, header, [&](::phaser::BufferOffset element) {
          return SerializeToROSAt<T>(runtime, element, buffer);
        });
  }
}

// Generated messages whose fields can all be parsed straight into the
// payload have static PhaserDeserializeFromROSAt and PhaserROSMinSize
// functions.
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

// Reads a T from 'reader' into the empty message at 'offset' in the mutable
// buffer of 'runtime'.
template <typename T>
inline absl::Status DeserializeFromROSAt(const RuntimeHandle& runtime,
                                         ::phaser::BufferOffset offset,
                                         ::phaser::ROSReader& reader) {
  if constexpr (HasROSParseAt<T>::value) {
    return T::PhaserDeserializeFromROSAt(runtime, offset, reader);
  } else {
    return T(BorrowedRuntime(runtime), offset)
        .DeserializeFromROSUncleared(reader);
  }
}

// Resizes the empty vector of T whose VectorHeader is at 'header' in the
// mutable buffer of 'runtime' to 'n' elements, as the field's resize() does,
// and returns them.
template <typename T>
inline T* ResizeVectorAt(const RuntimeHandle& runtime,
                         ::phaser::BufferOffset header, uint32_t n) {
  if (n == 0) {
    return nullptr;
  }
  if (runtime->pb->template UncheckedAddress<::phaser::VectorHeader>(header)
          ->data == 0) {
    if (size_t{n} * sizeof(T) > UINT32_MAX) {
      throw std::bad_alloc();
    }
    void* data = ::phaser::PayloadBuffer::Allocate(
        &runtime->pb, static_cast<uint32_t>(size_t{n} * sizeof(T)));
    if (data == nullptr) {
      throw std::bad_alloc();
    }
    // Allocating can move the buffer.
    auto* vector_header =
        runtime->pb->template UncheckedAddress<::phaser::VectorHeader>(header);
    vector_header->data = runtime->pb->UncheckedOffset(data);
    vector_header->num_elements = n;
    return static_cast<T*>(data);
  }
  ::phaser::PayloadBuffer::VectorResize<T>(
      &runtime->pb,
      runtime->pb->template UncheckedAddress<::phaser::VectorHeader>(header),
      n);
  T* data = runtime->pb->template UncheckedAddress<T>(
      runtime->pb->template UncheckedAddress<::phaser::VectorHeader>(header)
          ->data);
  if (data == nullptr && n != 0) {
    throw std::bad_alloc();
  }
  return data;
}

// Appends 'count' elements to the vector of T whose VectorHeader is at
// 'header' in the mutable buffer of 'runtime', reading each from 'reader'.
template <typename T>
inline absl::Status DeserializeMessageVectorFromROSAt(
    const RuntimeHandle& runtime, ::phaser::BufferOffset header,
    uint32_t count, ::phaser::ROSReader& reader) {
  using Vector = ::phaser::MessageVectorField<T>;
  if (count == 0) {
    return absl::OkStatus();
  }
  if constexpr (ROSParseMinSize<T>() > 0) {
    // The input bounds the elements this allocates.  A count it cannot
    // hold is truncated input, which the loop below reports.
    if (count <= reader.Remaining() / ROSParseMinSize<T>()) {
      if constexpr (HasROSUncheckedParseAt<T>::value) {
        // Every element is ROSParseMinSize<T>() bytes, so all are in the
        // input.
        constexpr size_t kSize = ROSParseMinSize<T>();
        const char* input = reader.ReadRawUnchecked(size_t{count} * kSize);
        Vector::AddElementsInPlaceAt(
            runtime, header, count, [input](char* element, uint32_t i) {
              T::PhaserDeserializeFromROSUncheckedAt(
                  element, input + size_t{i} * kSize);
            });
        return absl::OkStatus();
      } else {
        return Vector::AddElementsAt(
            runtime, header, count, [&](::phaser::BufferOffset element) {
              return T::PhaserDeserializeFromROSAt(runtime, element, reader);
            });
      }
    }
  }
  // A hostile count can only reserve one element per four bytes left in the
  // input.  Smaller elements still fit by growing.
  Vector::ReserveAt(runtime, header,
                    std::min<size_t>(count, reader.Remaining() / 4));
  for (uint32_t i = 0; i < count; ++i) {
    if (absl::Status status = DeserializeFromROSAt<T>(
            runtime, Vector::AddElementAt(runtime, header), reader);
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

// Appends 'count' elements to 'vector', a vector of T in a message of
// 'runtime', reading each from 'reader'.
template <typename T, typename Vector>
inline absl::Status DeserializeMessageVectorFromROS(
    const RuntimeHandle& runtime, Vector& vector, uint32_t count,
    ::phaser::ROSReader& reader) {
  if (runtime->IsMutable()) {
    return DeserializeMessageVectorFromROSAt<T>(
        runtime, vector.VectorHeaderOffset(), count, reader);
  }
  // Adding to a readonly message throws.
  vector.reserve(std::min<size_t>(count, reader.Remaining() / 4));
  for (uint32_t i = 0; i < count; ++i) {
    if (absl::Status status = vector.Add().DeserializeFromROSUncleared(reader);
        !status.ok()) {
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
