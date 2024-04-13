#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "phaser/runtime/fields.h"

namespace phaser {

// Provides a statically sized or dynamic buffer used for serialization
// of zeros messages.
//
// This is used by zeros to serialize a zero-copy message into ROS
// message format for portable tranmission or storage.  Deserialization
// is also supported.

class ProtoBuffer {
 public:
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
      : owned_(false),
        start_(addr),
        size_(size),
        addr_(addr),
        end_(addr + size) {}

  ~ProtoBuffer() {
    if (owned_) {
      free(start_);
    }
  }

  size_t Size() const { return addr_ - start_; }

  size_t size() const { return Size(); }

  template <typename T>
  T *Data() {
    return reinterpret_cast<T *>(start_);
  }

  char *data() { return Data<char>(); }

  std::string AsString() const { return std::string(start_, addr_ - start_); }

  template <typename T>
  absl::Span<const T> AsSpan() const {
    return absl::Span<T>(reinterpret_cast<const T *>(start_), addr_ - start_);
  }

  void Clear() {
    addr_ = start_;
    end_ = start_;
  }

// absl::Status Write(const Int8Field &v) {
//   if (absl::Status status = HasSpaceFor(v.SerializedSize()); !status.ok()) {
//     return status;
//   }
//   int8_t tv = v.Get();
//   memcpy(addr_, &tv, v.SerializedSize());
//   addr_ += v.SerializedSize();
//   return absl::OkStatus();
// }

// absl::Status Read(Int8Field &v) {
//   if (absl::Status status = Check(v.SerializedSize()); !status.ok()) {
//     return status;
//   }
//   v.Set(*reinterpret_cast<int8_t *>(addr_));
//   addr_ += v.SerializedSize();
//   return absl::OkStatus();
// }

#define PRIMITIVE_FUNCS(field_type, type)                                      \
  absl::Status Write(const field_type##Field &v) {                             \
    if (absl::Status status = HasSpaceFor(v.SerializedSize()); !status.ok()) { \
      return status;                                                           \
    }                                                                          \
    type tv = v.Get();                                                         \
    memcpy(addr_, &tv, v.SerializedSize());                                    \
    addr_ += v.SerializedSize();                                               \
    return absl::OkStatus();                                                   \
  }                                                                            \
                                                                               \
  absl::Status Read(field_type##Field &v) {                                    \
    if (absl::Status status = Check(v.SerializedSize()); !status.ok()) {       \
      return status;                                                           \
    }                                                                          \
    v.Set(*reinterpret_cast<type *>(addr_));                                   \
    addr_ += v.SerializedSize();                                               \
    return absl::OkStatus();                                                   \
  }                                                                            \
                                                                               \
  absl::Status Write(type v) {                                                 \
    if (absl::Status status = HasSpaceFor(sizeof(type)); !status.ok()) {       \
      return status;                                                           \
    }                                                                          \
    memcpy(addr_, &v, sizeof(type));                                           \
    addr_ += sizeof(type);                                                     \
    return absl::OkStatus();                                                   \
  }                                                                            \
                                                                               \
  absl::Status Read(type &v) {                                                 \
    if (absl::Status status = Check(sizeof(type)); !status.ok()) {             \
      return status;                                                           \
    }                                                                          \
    v = *reinterpret_cast<type *>(addr_);                                      \
    addr_ += sizeof(type);                                                     \
    return absl::OkStatus();                                                   \
  }


  PRIMITIVE_FUNCS(Int32, int32_t)
  PRIMITIVE_FUNCS(Uint32, uint32_t)
  PRIMITIVE_FUNCS(Int64, int64_t)
  PRIMITIVE_FUNCS(Uint64, uint64_t)
  PRIMITIVE_FUNCS(Float, float)
  PRIMITIVE_FUNCS(Double, double)
  PRIMITIVE_FUNCS(Bool, bool)


#undef PRIMITIVE_FUNCS

  absl::Status Write(const StringField &v) {
    if (absl::Status status = HasSpaceFor(v.SerializedSize()); !status.ok()) {
      return status;
    }
    uint32_t size = static_cast<uint32_t>(v.size());
    memcpy(addr_, &size, sizeof(size));
    memcpy(addr_ + 4, v.data(), v.size());
    addr_ += v.SerializedSize();
    return absl::OkStatus();
  }

  absl::Status Read(StringField &v) {
    if (absl::Status status = Check(4); !status.ok()) {
      return status;
    }
    uint32_t size = 0;
    memcpy(&size, addr_, sizeof(size));
    if (absl::Status status = Check(size_t(size)); !status.ok()) {
      return status;
    }
    std::string s;
    s.resize(size);
    memcpy(s.data(), addr_ + 4, size);
    addr_ += 4 + s.size();
    v = s;
    return absl::OkStatus();
  }

  absl::Status Write(const NonEmbeddedStringField &v) {
    if (absl::Status status = HasSpaceFor(v.SerializedSize()); !status.ok()) {
      return status;
    }

    uint32_t size = static_cast<uint32_t>(v.size());
    memcpy(addr_, &size, sizeof(size));
    memcpy(addr_ + 4, v.data(), v.size());
    addr_ += v.SerializedSize();
    return absl::OkStatus();
  }

  absl::Status Read(NonEmbeddedStringField &v) {
    if (absl::Status status = Check(4); !status.ok()) {
      return status;
    }
    uint32_t size = 0;
    memcpy(&size, addr_, sizeof(size));
    if (absl::Status status = Check(size_t(size)); !status.ok()) {
      return status;
    }
    std::string s;
    s.resize(size);
    memcpy(s.data(), addr_ + 4, size);
    addr_ += 4 + s.size();
    v = s;
    return absl::OkStatus();
  }

  template <typename Enum>
  absl::Status Write(const EnumField<Enum> &v) {
    if (absl::Status status = HasSpaceFor(v.SerializedSize()); !status.ok()) {
      return status;
    }
    using Type = typename EnumField<Enum>::T;
    Type tv = v;
    memcpy(addr_, &tv, v.SerializedSize());
    addr_ += v.SerializedSize();
    return absl::OkStatus();
  }

  template <typename Enum>
  absl::Status Read(EnumField<Enum> &v) {
    if (absl::Status status = Check(v.SerializedSize()); !status.ok()) {
      return status;
    }
    v.Set(*reinterpret_cast<typename EnumField<Enum>::T *>(addr_));
    addr_ += v.SerializedSize();
    return absl::OkStatus();
  }

  template <typename T>
  absl::Status Write(const MessageField<T> &msg) {
    return msg.Get().SerializeToBuffer(*this);
  }

  template <typename T>
  absl::Status Read(MessageField<T> &msg) {
    return msg.Get().DeserializeFromBuffer(*this);
  }

  template <typename T>
  absl::Status Write(const PrimitiveVectorField<T> &vec) {
    if (absl::Status status = HasSpaceFor(vec.SerializedSize()); !status.ok()) {
      return status;
    }
    uint32_t size = static_cast<uint32_t>(vec.size());
    memcpy(addr_, &size, sizeof(size));
    memcpy(addr_ + 4, vec.data(), size * sizeof(T));
    addr_ += vec.SerializedSize();
    return absl::OkStatus();
  }

  template <typename T>
  absl::Status Read(PrimitiveVectorField<T> &vec) {
    if (absl::Status status = Check(4); !status.ok()) {
      return status;
    }
    uint32_t size = 0;
    memcpy(&size, addr_, sizeof(size));
    addr_ += 4;
    vec.resize(size);
    memcpy(vec.data(), addr_, size * sizeof(T));
    addr_ += size * sizeof(T);
    return absl::OkStatus();
  }

  template <typename Enum>
  absl::Status Write(const EnumVectorField<Enum> &vec) {
    if (absl::Status status = HasSpaceFor(vec.SerializedSize()); !status.ok()) {
      return status;
    }
    uint32_t size = static_cast<uint32_t>(vec.size());
    memcpy(addr_, &size, sizeof(size));
    memcpy(addr_ + 4, vec.data(), size * sizeof(EnumVectorField<Enum>::T));
    addr_ += vec.SerializedSize();
    return absl::OkStatus();
  }

  template <typename Enum>
  absl::Status Read(EnumVectorField<Enum> &vec) {
    if (absl::Status status = Check(4); !status.ok()) {
      return status;
    }
    uint32_t size = 0;
    memcpy(&size, addr_, sizeof(size));
    addr_ += 4;
    vec.resize(size);
    memcpy(vec.data(), addr_, size * sizeof(EnumVectorField<Enum>::T));
    addr_ += size * sizeof(EnumVectorField<Enum>::T);
    return absl::OkStatus();
  }

  absl::Status Write(const StringVectorField &vec) {
    if (absl::Status status = HasSpaceFor(vec.SerializedSize()); !status.ok()) {
      return status;
    }
    uint32_t size = uint32_t(vec.size());
    memcpy(addr_, &size, sizeof(size));
    addr_ += sizeof(size);
    for (auto &s : vec) {
      if (absl::Status status = Write(s); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  absl::Status Read(StringVectorField &vec) {
    if (absl::Status status = Check(4); !status.ok()) {
      return status;
    }
    uint32_t size = 0;
    memcpy(&size, addr_, sizeof(size));
    addr_ += 4;
    vec.resize(size);

    for (auto &s : vec) {
      if (absl::Status status = Read(s); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

 private:
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

  bool owned_ = false;     // Memory is owned by this buffer.
  char *start_ = nullptr;  //
  size_t size_ = 0;
  char *addr_ = nullptr;
  char *end_ = nullptr;
};  // namespace davros::zeros

}  // namespace davros::zeros
