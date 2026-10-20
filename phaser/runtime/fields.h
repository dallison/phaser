// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Single value fields.

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/wireformat.h"
#include "phaser/runtime/payload_buffer.h"

namespace phaser {

template <typename T>
constexpr size_t AlignedOffset(size_t offset) {
  return (offset + sizeof(T) - 1) & ~(sizeof(T) - 1);
}

class Field {
 public:
  Field() = default;
  Field(int id, int number) : id_(id), number_(number) {}
  // 'relative_binary_offset' is where the field is in its message's compiled
  // layout.
  Field(int id, int number, uint32_t /*source_offset*/,
        uint32_t relative_binary_offset)
      : id_(id),
        number_(number),
        compiled_offset_(static_cast<int32_t>(relative_binary_offset)) {}
  Field(const Field&) = default;
  Field& operator=(const Field&) = default;
  // Fields are only destroyed as members of their message, so the
  // destructor is not virtual.
  ~Field() = default;

  // The presence bit is in a set of words immediately after
  // the metadata at the start of the message.
  void SetPresence(::phaser::PayloadBuffer* buffer, uint32_t binary_offset) {
    buffer->SetPresenceBit(static_cast<uint32_t>(id_), binary_offset);
  }

  void ClearPresence(::phaser::PayloadBuffer* buffer,
                     uint32_t binary_offset) {
    buffer->ClearPresenceBit(static_cast<uint32_t>(id_), binary_offset);
  }

  bool IsPresent(uint32_t field_id, const void* field, uint32_t source_offset,
                 uint32_t binary_offset) const {
    if (field_id == static_cast<uint32_t>(-1)) {
      return false;
    }
    return Message::GetRuntime(field, source_offset)
        ->IsPresent(field_id, binary_offset);
  }

  int Id() const { return id_; }
  int Number() const { return number_; }

  // A message whose fields are not at their compiled locations was written
  // with another version of its schema, so its fields are looked up in its
  // metadata on each access.
  int32_t FindFieldOffset(uint32_t source_offset) const {
    const Message* message = Message::GetMessage(this, source_offset);
    if (message->FieldLocationsKnown()) {
      return compiled_offset_;
    }
    return message->FindField(static_cast<uint32_t>(number_)).offset;
  }

  int32_t FindFieldId(uint32_t source_offset) const {
    const Message* message = Message::GetMessage(this, source_offset);
    if (message->FieldLocationsKnown()) {
      return id_;
    }
    return message->FindField(static_cast<uint32_t>(number_)).id;
  }

  // For printing.
  void Indent(int indent) const { indent_ += indent; }
  void PrintIndent(std::ostream& os) const {
    for (int i = 0; i < indent_; i++) {
      os << " ";
    }
  }

  int GetIndent() const { return indent_; }

 protected:
  void RequireMutable(uint32_t source_offset) const {
    (void)MutableBuffer(source_offset);
  }

  // The buffer of the message holding this field, which must be mutable.
  ::phaser::PayloadBuffer* MutableBuffer(uint32_t source_offset) const {
    const MessageRuntime& runtime = *Message::GetRuntime(this, source_offset);
    if (ABSL_PREDICT_FALSE(!runtime.IsMutable())) {
      ThrowReadonlyMessage();
    }
    return runtime.pb;
  }

  [[noreturn]] ABSL_ATTRIBUTE_NOINLINE static void ThrowReadonlyMessage() {
    throw std::logic_error("cannot mutate a readonly Phaser message");
  }

  // Writes 'v' at the field's compiled location, where it is in every mutable
  // message, and marks the field present.  Everything is read before the
  // payload is written, which the compiler must assume can change the field.
  template <typename T>
  void StoreValue(uint32_t source_offset, T v) {
    const Message* message = Message::GetMessage(this, source_offset);
    ::phaser::PayloadBuffer* pb = MutableBuffer(source_offset);
    const ::phaser::BufferOffset start = message->absolute_binary_offset;
    const uint32_t id = static_cast<uint32_t>(id_);
    pb->Set(start + static_cast<uint32_t>(compiled_offset_), v);
    pb->SetPresenceBit(id, start + kPresenceMaskOffset);
  }

  void ClearValue(uint32_t source_offset) {
    const Message* message = Message::GetMessage(this, source_offset);
    MutableBuffer(source_offset)
        ->ClearPresenceBit(static_cast<uint32_t>(id_),
                           message->absolute_binary_offset +
                               kPresenceMaskOffset);
  }

 public:
  // The presence mask follows the metadata offset at the start of a message.
  static constexpr uint32_t kPresenceMaskOffset = 4;

 protected:
  int id_ = 0;
  int number_ = 0;
  int32_t compiled_offset_ = -1;
  mutable int indent_ = 0;
};

// Common std::string-like API for payload-backed string fields. Read operations
// remain zero-copy. Operations that can change the string length construct the
// new value outside the payload first so aliases into a relocating payload stay
// valid until the mutation is ready to commit.
template <typename Derived>
class StringFieldFacade {
 public:
  using value_type = char;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using const_reference = const char&;
  using const_pointer = const char*;
  using const_iterator = std::string_view::const_iterator;
  using const_reverse_iterator = std::string_view::const_reverse_iterator;

  static constexpr size_type npos = std::string_view::npos;

  bool empty() const { return View().empty(); }
  size_type length() const { return View().length(); }
  size_type max_size() const { return std::string{}.max_size(); }
  size_type capacity() const { return View().size(); }

  char operator[](size_type pos) const { return View()[pos]; }
  char at(size_type pos) const { return View().at(pos); }
  char front() const { return View().front(); }
  char back() const { return View().back(); }

  const_iterator begin() const { return View().begin(); }
  const_iterator end() const { return View().end(); }
  const_iterator cbegin() const { return View().cbegin(); }
  const_iterator cend() const { return View().cend(); }
  const_reverse_iterator rbegin() const { return View().rbegin(); }
  const_reverse_iterator rend() const { return View().rend(); }
  const_reverse_iterator crbegin() const { return View().crbegin(); }
  const_reverse_iterator crend() const { return View().crend(); }

  int compare(std::string_view other) const { return View().compare(other); }
  int compare(size_type pos, size_type count, std::string_view other) const {
    return View().compare(pos, count, other);
  }
  int compare(size_type pos, size_type count, std::string_view other,
              size_type other_pos, size_type other_count = npos) const {
    return View()
        .substr(pos, count)
        .compare(other.substr(other_pos, other_count));
  }

  bool starts_with(std::string_view prefix) const {
    const std::string_view value = View();
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
  }
  bool starts_with(char prefix) const {
    const std::string_view value = View();
    return !value.empty() && value.front() == prefix;
  }
  bool ends_with(std::string_view suffix) const {
    const std::string_view value = View();
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
  }
  bool ends_with(char suffix) const {
    const std::string_view value = View();
    return !value.empty() && value.back() == suffix;
  }
  bool contains(std::string_view value) const { return find(value) != npos; }
  bool contains(char value) const { return find(value) != npos; }

  size_type find(std::string_view value, size_type pos = 0) const {
    return View().find(value, pos);
  }
  size_type find(char value, size_type pos = 0) const {
    return View().find(value, pos);
  }
  size_type find(const char* value, size_type pos, size_type count) const {
    return View().find(std::string_view(value, count), pos);
  }
  size_type rfind(std::string_view value, size_type pos = npos) const {
    return View().rfind(value, pos);
  }
  size_type rfind(char value, size_type pos = npos) const {
    return View().rfind(value, pos);
  }
  size_type rfind(const char* value, size_type pos, size_type count) const {
    return View().rfind(std::string_view(value, count), pos);
  }
  size_type find_first_of(std::string_view value, size_type pos = 0) const {
    return View().find_first_of(value, pos);
  }
  size_type find_first_of(char value, size_type pos = 0) const {
    return View().find_first_of(value, pos);
  }
  size_type find_last_of(std::string_view value, size_type pos = npos) const {
    return View().find_last_of(value, pos);
  }
  size_type find_last_of(char value, size_type pos = npos) const {
    return View().find_last_of(value, pos);
  }
  size_type find_first_not_of(std::string_view value, size_type pos = 0) const {
    return View().find_first_not_of(value, pos);
  }
  size_type find_first_not_of(char value, size_type pos = 0) const {
    return View().find_first_not_of(value, pos);
  }
  size_type find_last_not_of(std::string_view value,
                             size_type pos = npos) const {
    return View().find_last_not_of(value, pos);
  }
  size_type find_last_not_of(char value, size_type pos = npos) const {
    return View().find_last_not_of(value, pos);
  }

  std::string substr(size_type pos = 0, size_type count = npos) const {
    return std::string(View().substr(pos, count));
  }
  size_type copy(char* destination, size_type count, size_type pos = 0) const {
    return View().copy(destination, count, pos);
  }

  Derived& assign(std::string_view value) {
    const std::string value_copy(value);
    Mutable().Set(value_copy);
    return Mutable();
  }
  Derived& assign(const char* value, size_type count) {
    return assign(std::string_view(value, count));
  }
  Derived& assign(size_type count, char value) {
    Mutable().Set(std::string(count, value));
    return Mutable();
  }
  Derived& append(std::string_view value) {
    return Mutate([value](std::string& current) { current.append(value); });
  }
  Derived& append(const char* value, size_type count) {
    return append(std::string_view(value, count));
  }
  Derived& append(size_type count, char value) {
    return Mutate(
        [count, value](std::string& current) { current.append(count, value); });
  }
  Derived& operator+=(std::string_view value) { return append(value); }
  Derived& operator+=(const char* value) { return append(value); }
  Derived& operator+=(char value) {
    push_back(value);
    return Mutable();
  }
  void push_back(char value) {
    Mutate([value](std::string& current) { current.push_back(value); });
  }
  void pop_back() {
    Mutate([](std::string& current) { current.pop_back(); });
  }
  void resize(size_type count, char value = char()) {
    Mutate(
        [count, value](std::string& current) { current.resize(count, value); });
  }
  Derived& erase(size_type pos = 0, size_type count = npos) {
    return Mutate(
        [pos, count](std::string& current) { current.erase(pos, count); });
  }
  Derived& insert(size_type pos, std::string_view value) {
    return Mutate(
        [pos, value](std::string& current) { current.insert(pos, value); });
  }
  Derived& insert(size_type pos, size_type count, char value) {
    return Mutate([pos, count, value](std::string& current) {
      current.insert(pos, count, value);
    });
  }
  Derived& replace(size_type pos, size_type count, std::string_view value) {
    return Mutate([pos, count, value](std::string& current) {
      current.replace(pos, count, value);
    });
  }
  void clear() { Mutable().Clear(); }
  void swap(Derived& other) {
    if (&Mutable() == &other) {
      return;
    }
    const std::string mine(View());
    const std::string theirs(other.Get());
    Mutable().Set(theirs);
    other.Set(mine);
  }

  bool operator==(const Derived& other) const { return View() == other.Get(); }
  bool operator!=(const Derived& other) const { return View() != other.Get(); }
  bool operator==(std::string_view other) const { return View() == other; }
  bool operator!=(std::string_view other) const { return View() != other; }
  bool operator<(std::string_view other) const { return View() < other; }
  bool operator<=(std::string_view other) const { return View() <= other; }
  bool operator>(std::string_view other) const { return View() > other; }
  bool operator>=(std::string_view other) const { return View() >= other; }

  friend bool operator==(std::string_view lhs, const StringFieldFacade& rhs) {
    return lhs == rhs.View();
  }
  friend bool operator!=(std::string_view lhs, const StringFieldFacade& rhs) {
    return lhs != rhs.View();
  }
  friend bool operator<(std::string_view lhs, const StringFieldFacade& rhs) {
    return lhs < rhs.View();
  }
  friend bool operator<=(std::string_view lhs, const StringFieldFacade& rhs) {
    return lhs <= rhs.View();
  }
  friend bool operator>(std::string_view lhs, const StringFieldFacade& rhs) {
    return lhs > rhs.View();
  }
  friend bool operator>=(std::string_view lhs, const StringFieldFacade& rhs) {
    return lhs >= rhs.View();
  }
  friend std::string operator+(const StringFieldFacade& lhs,
                               std::string_view rhs) {
    std::string result(lhs.View());
    result.append(rhs);
    return result;
  }
  friend std::string operator+(std::string lhs, const StringFieldFacade& rhs) {
    lhs.append(rhs.View());
    return lhs;
  }

 private:
  const Derived& Const() const { return static_cast<const Derived&>(*this); }
  Derived& Mutable() { return static_cast<Derived&>(*this); }
  std::string_view View() const { return Const().Get(); }

  template <typename Mutator>
  Derived& Mutate(Mutator mutator) {
    std::string value(View());
    mutator(value);
    Mutable().Set(value);
    return Mutable();
  }
};

#define DEFINE_PRIMITIVE_FIELD(cname, type)                                   \
  template <bool FixedSize = false, bool Signed = false>                      \
  class cname##Field : public Field {                                         \
   public:                                                                    \
    cname##Field() = default;                                                 \
    explicit cname##Field(uint32_t boff, uint32_t offset, int id, int number) \
        : Field(id, number, boff, offset),                                    \
          source_offset_(boff),                                               \
          relative_binary_offset_(offset) {}                                  \
    cname##Field(const cname##Field&) = default;                              \
    cname##Field(cname##Field&&) = default;                                   \
    cname##Field& operator=(const cname##Field& other) {                      \
      if (this == &other) {                                                   \
        return *this;                                                         \
      }                                                                       \
      if (other.IsPresent()) {                                                \
        Set(other.Get());                                                     \
      } else {                                                                \
        Clear();                                                              \
      }                                                                       \
      return *this;                                                           \
    }                                                                         \
    cname##Field& operator=(cname##Field&& other) noexcept {                  \
      return operator=(static_cast<const cname##Field&>(other));              \
    }                                                                         \
    operator type() const { return Get(); }                                   \
    cname##Field& operator=(type v) {                                         \
      Set(v);                                                                 \
      return *this;                                                           \
    }                                                                         \
    type Get() const {                                                        \
      int32_t offset = FindFieldOffset(source_offset_);                       \
      if (offset < 0) {                                                       \
        return type();                                                        \
      }                                                                       \
      const type* _phaser_addr =                                              \
          Message::GetRuntime(this, source_offset_)                          \
              ->template ToAddress<const type>(                              \
                  GetMessageBinaryStart() +                                  \
                  static_cast<::phaser::BufferOffset>(offset));            \
      if (_phaser_addr == nullptr) {                                          \
        return type();                                                        \
      }                                                                       \
      return *_phaser_addr;                                                   \
    }                                                                         \
    type GetForPrinting() const { return Get(); }                             \
    bool IsPresent() const {                                                  \
      return Field::IsPresent(                                                \
          static_cast<uint32_t>(FindFieldId(source_offset_)), this,            \
          source_offset_, GetPresenceMaskStart());                            \
    }                                                                         \
                                                                              \
    void Set(type v) { StoreValue(source_offset_, v); }                       \
    void Clear() { ClearValue(source_offset_); }                              \
    bool operator==(const cname##Field& other) const {                        \
      return Get() == other.Get();                                            \
    }                                                                         \
    bool operator!=(const cname##Field& other) const {                        \
      return !(*this == other);                                               \
    }                                                                         \
    size_t SerializedSize() const {                                           \
      if constexpr (FixedSize) {                                              \
        return ProtoBuffer::TagSize(Number(),                                 \
                                    ProtoBuffer::FixedWireType<type>()) +     \
               sizeof(type);                                                  \
      } else {                                                                \
        return ProtoBuffer::TagSize(Number(), WireType::kVarint) +            \
               ProtoBuffer::VarintSize<type, Signed>(Get());                  \
      }                                                                       \
    }                                                                         \
    absl::Status Serialize(ProtoBuffer& buffer) const {                       \
      if constexpr (FixedSize) {                                              \
        return buffer.SerializeFixed<type>(Number(), Get());                  \
      } else {                                                                \
        return buffer.SerializeVarint<type, Signed>(Number(), Get());         \
      }                                                                       \
    }                                                                         \
                                                                              \
    absl::Status Deserialize(ProtoBuffer& buffer) {                           \
      absl::StatusOr<type> v;                                                 \
      if constexpr (FixedSize) {                                              \
        v = buffer.DeserializeFixed<type>();                                  \
      } else {                                                                \
        v = buffer.DeserializeVarint<type, Signed>();                         \
      }                                                                       \
      if (!v.ok()) {                                                          \
        return v.status();                                                    \
      }                                                                       \
      Set(*v);                                                                \
      return absl::OkStatus();                                                \
    }                                                                         \
                                                                              \
   private:                                                                   \
    ::phaser::PayloadBuffer* GetBuffer() const {                            \
      return Message::GetBuffer(this, source_offset_);                        \
    }                                                                         \
    ::phaser::BufferOffset GetMessageBinaryStart() const {                  \
      return Message::GetMessageBinaryStart(this, source_offset_);            \
    }                                                                         \
    ::phaser::BufferOffset GetPresenceMaskStart() const {                   \
      return Message::GetMessageBinaryStart(this, source_offset_) + 4;        \
    }                                                                         \
    uint32_t source_offset_;                                                  \
    ::phaser::BufferOffset relative_binary_offset_;                         \
  };

DEFINE_PRIMITIVE_FIELD(Int32, int32_t)
DEFINE_PRIMITIVE_FIELD(Uint32, uint32_t)
DEFINE_PRIMITIVE_FIELD(Int64, int64_t)
DEFINE_PRIMITIVE_FIELD(Uint64, uint64_t)
DEFINE_PRIMITIVE_FIELD(Double, double)
DEFINE_PRIMITIVE_FIELD(Float, float)
DEFINE_PRIMITIVE_FIELD(Bool, bool)

#undef DEFINE_PRIMITIVE_FIELD

namespace internal {
template <bool F, bool S>
struct FieldRelease<Int32Field<F, S>> : NoFieldRelease {};
template <bool F, bool S>
struct FieldRelease<Uint32Field<F, S>> : NoFieldRelease {};
template <bool F, bool S>
struct FieldRelease<Int64Field<F, S>> : NoFieldRelease {};
template <bool F, bool S>
struct FieldRelease<Uint64Field<F, S>> : NoFieldRelease {};
template <bool F, bool S>
struct FieldRelease<DoubleField<F, S>> : NoFieldRelease {};
template <bool F, bool S>
struct FieldRelease<FloatField<F, S>> : NoFieldRelease {};
template <bool F, bool S>
struct FieldRelease<BoolField<F, S>> : NoFieldRelease {};
}  // namespace internal

struct InternalIntStringizer {
  std::string operator()(int i) const { return std::to_string(i); }
};

struct InternalIntParser {
  int operator()(const std::string& s) const { return std::stoi(s); }
};

template <typename Enum = int, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser>
class EnumField : public Field {
 public:
  using T = typename std::underlying_type<Enum>::type;
  EnumField() = default;
  explicit EnumField(uint32_t boff, uint32_t offset, int id, int number)
      : Field(id, number, boff, offset),
        source_offset_(boff),
        relative_binary_offset_(offset) {}
  EnumField(const EnumField&) = default;
  EnumField(EnumField&&) = default;
  EnumField& operator=(const EnumField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPresent()) {
      Set(other.Get());
    } else {
      Clear();
    }
    return *this;
  }
  EnumField& operator=(EnumField&& other) noexcept {
    return operator=(static_cast<const EnumField&>(other));
  }
  operator Enum() const { return Get(); }
  operator T() const { return GetUnderlying(); }
  EnumField& operator=(Enum e) {
    Set(e);
    return *this;
  }
  EnumField& operator=(T e) {
    Set(e);
    return *this;
  }

  Enum Get() const { return static_cast<Enum>(GetUnderlying()); }

  std::string GetForPrinting() const { return ToString(); }

  bool IsPresent() const {
    return Field::IsPresent(static_cast<uint32_t>(FindFieldId(source_offset_)),
                            this, source_offset_, GetPresenceMaskStart());
  }

  std::string ToString() const { return Stringizer()(Get()); }

  Enum ParseFromString(const std::string& s) { Set(Parser(s)); }

  T GetUnderlying() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    const T* addr =
        Message::GetRuntime(this, source_offset_)
            ->template ToAddress<const T>(
                GetMessageBinaryStart() +
                static_cast<::phaser::BufferOffset>(offset));
    if (addr == nullptr) {
      return 0;
    }
    return *addr;
  }

  void Set(Enum e) {
    StoreValue(source_offset_,
               static_cast<typename std::underlying_type<Enum>::type>(e));
  }

  void Set(T e) { StoreValue(source_offset_, e); }

  void Clear() { ClearValue(source_offset_); }

  bool operator==(const EnumField& other) const {
    return static_cast<Enum>(*this) == static_cast<Enum>(other);
  }
  bool operator!=(const EnumField& other) const { return !(*this == other); }

  size_t SerializedSize() const {
    return ProtoBuffer::TagSize(Number(), WireType::kVarint) +
           ProtoBuffer::VarintSize<int32_t, false>(
               static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    return buffer.SerializeVarint<int32_t, false>(
        Number(), static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<T> v = buffer.DeserializeVarint<T, false>();
    if (!v.ok()) {
      return v.status();
    }
    Set(*v);
    return absl::OkStatus();
  }

 private:
  ::phaser::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }
  ::phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }
  ::phaser::BufferOffset GetPresenceMaskStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_) + 4;
  }

  uint32_t source_offset_;
  ::phaser::BufferOffset relative_binary_offset_;
};

namespace internal {
template <typename Enum, typename Stringizer, typename Parser>
struct FieldRelease<EnumField<Enum, Stringizer, Parser>> : NoFieldRelease {};
}  // namespace internal

// String field with an offset inline in the message.
class StringField : public Field, public StringFieldFacade<StringField> {
 public:
  StringField() = default;
  explicit StringField(uint32_t source_offset, uint32_t relative_binary_offset,
                       int id, int number)
      : Field(id, number, source_offset, relative_binary_offset),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset) {}
  StringField(const StringField&) = default;
  StringField(StringField&&) = default;
  StringField& operator=(const StringField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPresent()) {
      Set(other.Get());
    } else {
      Clear();
    }
    return *this;
  }
  StringField& operator=(StringField&& other) noexcept {
    return operator=(static_cast<const StringField&>(other));
  }
  operator std::string_view() const { return Get(); }
  StringField& operator=(const std::string& s) {
    Set(s);
    return *this;
  }
  StringField& operator=(std::string_view s) {
    Set(s);
    return *this;
  }
  StringField& operator=(const char* s) {
    ::phaser::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(s, std::strlen(s)),
        StringHeaderOffset());
    return *this;
  }

  std::string_view Get() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return std::string_view();
    }
    return GetRuntime()->GetStringView(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(offset));
  }

  bool IsPresent() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return false;
    }
    const ::phaser::BufferOffset* addr =
        GetRuntime()->ToAddress<const ::phaser::BufferOffset>(
            GetMessageBinaryStart() +
            static_cast<::phaser::BufferOffset>(offset));
    return addr != nullptr && *addr != 0;
  }

  template <typename Str>
  void Set(Str s) {
    ::phaser::PayloadBuffer::SetString(GetBufferAddr(), s,
                                         StringHeaderOffset());
  }

  void Set(const char* data, size_t size) {
    ::phaser::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(data, size), StringHeaderOffset());
  }

  void SetNoCopy(const void* data) {
    ::phaser::StringHeader* header =
        GetRuntime()->ToAddress<::phaser::StringHeader>(StringHeaderOffset());
    *header = GetRuntime()->ToOffset(data);
  }

  void Clear() {
    ::phaser::PayloadBuffer::ClearString(GetBufferAddr(), StringHeaderOffset());
  }

  // Allocate space for the given size for the string and return the
  // starting address.
  absl::Span<char> Allocate(size_t size, bool clear = false) {
    return ::phaser::PayloadBuffer::AllocateString(GetBufferAddr(), size,
                                                    StringHeaderOffset(),
                                                    clear);
  }

  size_t size() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return 0;
    }
    return GetRuntime()->StringSize(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(offset));
  }

  const char* data() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return nullptr;
    }
    return GetRuntime()->StringData(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(offset));
  }

  size_t SerializedSize() const {
    size_t s = size();
    return ProtoBuffer::LengthDelimitedSize(Number(), s);
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    size_t s = size();
    return buffer.ProtoBuffer::SerializeLengthDelimited(Number(), data(), s);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<std::string_view> s = buffer.DeserializeString();
    if (!s.ok()) {
      return s.status();
    }
    ::phaser::PayloadBuffer::SetString(GetBufferAddr(), *s,
                                         StringHeaderOffset());
    return absl::OkStatus();
  }

 private:
  template <size_t N>
  friend class StringArrayField;

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::phaser::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  ::phaser::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }
  ::phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  ::phaser::BufferOffset StringHeaderOffset() const {
    int32_t offset = FindFieldOffset(source_offset_);
    if (offset < 0) {
      return GetMessageBinaryStart() + relative_binary_offset_;
    }
    return GetMessageBinaryStart() +
           static_cast<::phaser::BufferOffset>(offset);
  }

  uint32_t source_offset_;
  ::phaser::BufferOffset relative_binary_offset_;
};

// This is a string field that is not embedded inside a message. They
// store the std::shared_ptr to the phaser::Runtime pointer instead of
// an offset to the start of the message.
class NonEmbeddedStringField
    : public StringFieldFacade<NonEmbeddedStringField> {
 public:
  NonEmbeddedStringField() = default;
  explicit NonEmbeddedStringField(const Message* msg,
                                  uint32_t absolute_binary_offset)
      : msg_(msg), absolute_binary_offset_(absolute_binary_offset) {}
  NonEmbeddedStringField(const NonEmbeddedStringField&) = default;
  NonEmbeddedStringField(NonEmbeddedStringField&&) = default;
  NonEmbeddedStringField& operator=(const NonEmbeddedStringField& other) & {
    return AssignValue(other);
  }
  NonEmbeddedStringField& operator=(const NonEmbeddedStringField& other) && {
    return AssignValue(other);
  }
  NonEmbeddedStringField& operator=(NonEmbeddedStringField&& other) & noexcept {
    msg_ = other.msg_;
    absolute_binary_offset_ = other.absolute_binary_offset_;
    return *this;
  }
  NonEmbeddedStringField& operator=(NonEmbeddedStringField&& other) && {
    return AssignValue(other);
  }

 private:
  NonEmbeddedStringField& AssignValue(const NonEmbeddedStringField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPlaceholder()) {
      return *this;
    }
    std::string_view value = other.Get();
    if (GetBuffer() != other.GetBuffer()) {
      Set(value);
      return *this;
    }
    if (value.empty()) {
      Set(value);
      return *this;
    }
    // Preserve an offset rather than an address: allocating the destination
    // may relocate a dynamic payload, but offsets remain valid.
    const ::phaser::BufferOffset source =
        GetBuffer()->ToOffset(const_cast<char*>(value.data()));
    absl::Span<char> destination = ::phaser::PayloadBuffer::AllocateString(
        GetBufferAddr(), value.size(), absolute_binary_offset_, false);
    memmove(destination.data(), GetBuffer()->ToAddress<char>(source),
            value.size());
    return *this;
  }

 public:
  operator std::string_view() const { return Get(); }
  NonEmbeddedStringField& operator=(const std::string& s) {
    Set(s);
    return *this;
  }
  NonEmbeddedStringField& operator=(std::string_view s) {
    Set(s);
    return *this;
  }
  NonEmbeddedStringField& operator=(const char* s) {
    ::phaser::PayloadBuffer::SetString(GetBufferAddr(),
                                         std::string_view(s, std::strlen(s)),
                                         absolute_binary_offset_);
    return *this;
  }

  std::string_view Get() const {
    if (IsPlaceholder()) {
      return {};
    }
    return msg_->runtime->GetStringView(absolute_binary_offset_);
  }

  template <typename Str>
  void Set(Str s) {
    if (IsPlaceholder()) {
      return;
    }
    ::phaser::PayloadBuffer::SetString(GetBufferAddr(), s,
                                         absolute_binary_offset_);
  }

  void Clear() {
    if (IsPlaceholder()) {
      return;
    }
    ::phaser::PayloadBuffer::ClearString(GetBufferAddr(),
                                           absolute_binary_offset_);
  }

  size_t size() const {
    if (IsPlaceholder()) {
      return 0;
    }
    return msg_->runtime->StringSize(absolute_binary_offset_);
  }

  const char* data() const {
    if (IsPlaceholder()) {
      return "";
    }
    return msg_->runtime->StringData(absolute_binary_offset_);
  }
  bool IsPlaceholder() const { return msg_ == nullptr; }

  // Number of bytes the raw string occupies on the wire (not including any
  // field tag or length prefix). Repeated-string serialization is handled by
  // StringVectorField, which writes the tag/length and bytes directly, so this
  // type intentionally has no standalone Serialize() of its own.
  size_t SerializedSize() const { return size(); }

 private:
  ::phaser::PayloadBuffer* GetBuffer() const { return msg_->runtime->pb; }

  ::phaser::PayloadBuffer** GetBufferAddr() const {
    if (!msg_->runtime->IsMutable()) {
      throw std::logic_error("cannot mutate a readonly Phaser string");
    }
    return &msg_->runtime->pb;
  }

  const Message* msg_ = nullptr;
  ::phaser::BufferOffset absolute_binary_offset_ =
      0;  // Offset into
          // ::phaser::PayloadBuffer of
          // ::phaser::StringHeader
};

// A singular message field.  It is a buffer offset containing the absolute
// offset of a message in the payload buffer:
//
//    In the message:
//    +------------+
//    |  Indirect  |------->+------------+
//    +------------+        |  Message   |
//                          |            |
//                          |            |
//                          |            |
//                          |            |
//                          +------------+
//
// or, if 'kInline' is true, the message itself, which is there when its
// metadata word is not zero.  The field metadata says which, so a message
// written with another version of the schema can be read either way.

template <typename MessageType, bool kInline = false>
class IndirectMessageField : public Field {
 public:
  IndirectMessageField() = default;
  explicit IndirectMessageField(uint32_t source_offset,
                                uint32_t relative_binary_offset, int id,
                                int number)
      : Field(id, number, source_offset, relative_binary_offset),
        source_offset_(source_offset),
        relative_binary_offset_(relative_binary_offset),
        msg_(InternalDefault{}) {}
  IndirectMessageField(const IndirectMessageField&) = default;
  IndirectMessageField(IndirectMessageField&&) = default;
  IndirectMessageField& operator=(const IndirectMessageField& other) {
    if (this == &other) {
      return *this;
    }
    if (other.IsPresent()) {
      if (absl::Status s = Mutable()->CloneFrom(other.Get()); !s.ok()) {
        return *this;
      }
    } else {
      Clear();
    }
    return *this;
  }
  IndirectMessageField& operator=(IndirectMessageField&& other) noexcept {
    return operator=(static_cast<const IndirectMessageField&>(other));
  }
  operator const MessageType&() const { return Get(); }
  const MessageType& operator*() const { return Get(); }
  const MessageType* operator->() const { return &Get(); }
  operator MessageType&() { return *Mutable(); }
  MessageType& operator*() { return *Mutable(); }
  MessageType* operator->() { return Mutable(); }

  const MessageType& Msg() const { return Get(); }
  MessageType& MutableMsg() { return *Mutable(); }

  const MessageType& Get() const {
    const ::phaser::BufferOffset message = MessageAt(Locate());
    if (message == 0) {
      return DefaultMessage();
    }
    msg_.template BindBorrowed<MessageType>(GetRuntime(), message);
    return msg_;
  }

  bool IsPresent() const { return MessageAt(Locate()) != 0; }

  MessageType* Mutable() {
    if constexpr (kInline) {
      const ::phaser::BufferOffset message =
          GetMessageBinaryStart() + relative_binary_offset_;
      if (*MutableBuffer(source_offset_)
               ->template AddressOf<::phaser::BufferOffset>(message) == 0) {
        // Finding the metadata can move the buffer.
        const ::phaser::BufferOffset metadata =
            Message::MetadataFor<MessageType>(GetRuntime());
        *GetRuntime()->pb->template AddressOf<::phaser::BufferOffset>(
            message) = metadata;
      }
      msg_.template BindBorrowed<MessageType>(GetRuntime(), message);
      return &msg_;
    }
    ::phaser::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    if (addr != nullptr && *addr != 0) {
      // Already allocated.
      msg_.template BindBorrowed<MessageType>(GetRuntime(), *addr);
      return &msg_;
    }
    // Allocate a new message.
    void* msg_addr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize());
    ::phaser::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    msg_.template BindBorrowed<MessageType>(GetRuntime(), msg_offset);

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(relative_binary_offset_);
    *addr = msg_offset;  // Put message field offset into message.

    // Install the metadata into the binary message.
    msg_.template InstallMetadata<MessageType>();
    return &msg_;
  }

  void SetOffset(::phaser::BufferOffset offset) {
    static_assert(!kInline, "an inline message is not at an offset");
    ::phaser::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    if (addr == nullptr) {
      return;
    }
    if (*addr != 0) {
      // Already set, clear the exising message
      Clear();
    }
    *addr = offset;
    msg_.template BindBorrowed<MessageType>(GetRuntime(), offset);
  }

  void Clear() {
    if constexpr (kInline) {
      const ::phaser::BufferOffset message =
          GetMessageBinaryStart() + relative_binary_offset_;
      if (*MutableBuffer(source_offset_)
               ->template AddressOf<::phaser::BufferOffset>(message) == 0) {
        return;
      }
      internal::ReleaseMessageContents<MessageType>(GetRuntime(), message);
      // Releasing can move the buffer.
      memset(GetRuntime()->pb->template AddressOf<char>(message), 0,
             MessageType::BinarySize());
      return;
    }
    ::phaser::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    if (addr == nullptr || *addr == 0) {
      return;
    }
    const ::phaser::BufferOffset old_offset = *addr;
    if constexpr (!internal::ClearReleasesNothing<MessageType>::value) {
      // Clear the message.
      msg_.template BindBorrowed<MessageType>(GetRuntime(), old_offset);
      msg_.Clear();
    }
    // Delete the memory in the payload buffer.
    GetBuffer()->Free(GetRuntime()->ToAddress(old_offset));
    // Zero out the offset to the message.
    addr = GetIndirectAddress(relative_binary_offset_);
    if (addr != nullptr) {
      *addr = 0;
    }
  }

  bool operator==(const IndirectMessageField& other) const {
    return Get() == other.Get();
  }
  bool operator!=(const IndirectMessageField& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    const Location location = Locate();
    if (location.offset < 0) {
      return 0;
    }
    if (const ::phaser::BufferOffset message = MessageAt(location);
        message != 0) {
      msg_.template BindBorrowed<MessageType>(GetRuntime(), message);
    }
    return ProtoBuffer::LengthDelimitedSize(Number(), msg_.SerializedSize());
  }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    const Location location = Locate();
    if (location.offset < 0) {
      return absl::OkStatus();
    }
    if (const ::phaser::BufferOffset message = MessageAt(location);
        message != 0) {
      msg_.template BindBorrowed<MessageType>(GetRuntime(), message);
    }

    size_t size = msg_.SerializedSize();
    if (absl::Status status =
            buffer.SerializeLengthDelimitedHeader(Number(), size);
        !status.ok()) {
      return status;
    }

    return msg_.Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    if constexpr (kInline) {
      MessageType* message = Mutable();
      ProtoBuffer sub_buffer(s.value());
      return message->Deserialize(sub_buffer);
    }
    // Allocate a new message.
    void* msg_addr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize());
    ::phaser::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    msg_.template BindBorrowed<MessageType>(GetRuntime(), msg_offset);

    // Buffer might have moved, get address of indirect again.
    ::phaser::BufferOffset* addr =
        GetIndirectAddress(relative_binary_offset_);
    *addr = msg_offset;  // Put message field offset into message.

    // Install the metadata into the binary message.
    msg_.template InstallMetadata<MessageType>();

    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

  void SyncToPayload() const {
    if constexpr (internal::HasSyncToPayloadWork<MessageType>::value) {
      if (IsPresent()) {
        Get().SyncToPayload();
      }
    }
  }

  void Indent(int indent) const {
    Field::Indent(indent);
    msg_.Indent(indent);
  }

 protected:
  static const MessageType& DefaultMessage() {
    static const MessageType message(InternalDefault{});
    return message;
  }

  ::phaser::PayloadBuffer* GetBuffer() const {
    return Message::GetBuffer(this, source_offset_);
  }

  // Where the field is in its message and whether the message it holds is
  // stored there, which depend on the schema the message was written with.
  struct Location {
    int32_t offset;
    bool is_inline;
  };
  Location Locate() const {
    const Message* message = Message::GetMessage(this, source_offset_);
    if (message->FieldLocationsKnown()) {
      return {compiled_offset_, kInline};
    }
    const FieldLocation location =
        message->FindField(static_cast<uint32_t>(number_));
    return {location.offset, location.id == kInlineMessageFieldId};
  }

  // The start of the message held by the field at 'location', or 0 if it
  // holds none.
  ::phaser::BufferOffset MessageAt(Location location) const {
    if (location.offset < 0) {
      return 0;
    }
    const ::phaser::BufferOffset field =
        GetMessageBinaryStart() + static_cast<uint32_t>(location.offset);
    const ::phaser::BufferOffset* word =
        GetRuntime()->template ToAddress<::phaser::BufferOffset>(field);
    if (word == nullptr || *word == 0) {
      return 0;
    }
    return location.is_inline ? field : *word;
  }

  ::phaser::BufferOffset* GetIndirectAddress(uint32_t abs_offset) const {
    return GetRuntime()->template ToAddress<::phaser::BufferOffset>(
        GetMessageBinaryStart() + abs_offset);
  }

  ::phaser::PayloadBuffer** GetBufferAddr() const {
    return Message::GetBufferAddr(this, source_offset_);
  }

  const std::shared_ptr<MessageRuntime>& GetRuntime() const {
    return Message::GetRuntime(this, source_offset_);
  }

  ::phaser::BufferOffset GetMessageBinaryStart() const {
    return Message::GetMessageBinaryStart(this, source_offset_);
  }

  uint32_t source_offset_;
  ::phaser::BufferOffset relative_binary_offset_;
  mutable MessageType msg_;
};

namespace internal {
template <>
struct FieldRelease<StringField> : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    runtime->pb->FreeAt(OffsetAt(runtime->pb, field));
  }
};

template <typename MessageType>
struct FieldRelease<IndirectMessageField<MessageType, false>>
    : IndirectMessageRelease<MessageType> {};

template <typename MessageType>
struct FieldRelease<IndirectMessageField<MessageType, true>>
    : InlineMessageRelease<MessageType> {};
}  // namespace internal

template <typename MessageType>
using InlineMessageField = IndirectMessageField<MessageType, true>;

template <typename MessageType>
class MessageObject {
 public:
  MessageObject() : msg_(InternalDefault{}) {}
  explicit MessageObject(std::shared_ptr<MessageRuntime> runtime,
                         uint32_t absolute_binary_offset)
      : msg_(runtime, absolute_binary_offset) {}
  MessageObject(const MessageObject& other)
      : msg_(other.msg_.runtime, other.msg_.absolute_binary_offset),
        indent_(other.indent_) {}
  MessageObject(MessageObject&& other) noexcept
      : msg_(other.msg_.runtime, other.msg_.absolute_binary_offset),
        indent_(other.indent_) {}
  MessageObject& operator=(const MessageObject& other) {
    if (this == &other) {
      return *this;
    }
    this->~MessageObject();
    new (this) MessageObject(other);
    return *this;
  }
  MessageObject& operator=(MessageObject&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    this->~MessageObject();
    new (this) MessageObject(std::move(other));
    return *this;
  }

  const MessageType& Get() const { return msg_; }

  const MessageType& operator*() const { return msg_; }
  MessageType& operator*() { return msg_; }
  const MessageType* operator->() const { return &msg_; }
  MessageType* operator->() { return &msg_; }

  MessageType* Mutable() { return &msg_; }

  bool operator==(const MessageObject<MessageType>& other) const {
    return msg_ == other.msg_;
  }
  bool operator!=(const MessageObject<MessageType>& other) const {
    return !(*this == other);
  }

  bool empty() const { return msg_.runtime == nullptr; }

  void InstallMetadata() { msg_.template InstallMetadata<MessageType>(); }

  bool IsPlaceholder() const { return msg_.runtime == nullptr; }

  void Clear() {
    if (msg_.runtime == nullptr) {
      return;
    }
    msg_.Clear();
  }

  const MessageType& Msg() const { return msg_; }
  MessageType& MutableMsg() { return msg_; }

  template <typename T>
  absl::Status CloneFrom(const T& other) {
    return msg_.CloneFrom(other.msg_);
  }

  size_t SerializedSize() const { return msg_.SerializedSize(); }

  absl::Status Serialize(ProtoBuffer& buffer) const {
    return msg_.Serialize(buffer);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<absl::Span<char>> s = buffer.DeserializeLengthDelimited();
    if (!s.ok()) {
      return s.status();
    }
    ProtoBuffer sub_buffer(s.value());
    return msg_.Deserialize(sub_buffer);
  }

  void Indent(int indent) const {
    indent_ += indent;
    msg_.Indent(indent);
  }

  void PrintIndent(std::ostream& os) const {
    for (int i = 0; i < indent_; i++) {
      os << " ";
    }
  }

 private:
  mutable MessageType msg_;
  mutable int indent_ = 0;
};

}  // namespace phaser
