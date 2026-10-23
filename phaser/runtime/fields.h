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
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "phaser/runtime/iterators.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/payload_buffer.h"
#include "phaser/runtime/wireformat.h"

namespace phaser {

template <typename T>
constexpr size_t AlignedOffset(size_t offset) {
  return (offset + sizeof(T) - 1) & ~(sizeof(T) - 1);
}

// What is common to fields wherever they are.
class Field {
 public:
  // The presence mask follows the metadata offset at the start of a message.
  static constexpr uint32_t kPresenceMaskOffset = 4;

  [[noreturn]] ABSL_ATTRIBUTE_NOINLINE static void ThrowReadonlyMessage() {
    PHASER_THROW(std::logic_error("cannot mutate a readonly Phaser message"));
  }
};

// The offset of a field in the handle of the message holding it.
struct SourceOffset {
  uint32_t value;
};

// Where a field is: its message's handle, and its offset, presence id and
// number in the message's compiled layout.
//
// A field of a generated message has the last three in its type.  It then
// holds nothing and is at the start of its message's handle (FieldSpec), or
// if it holds values of its own it holds only its offset in the handle
// (FieldSpecAt).  A field of a hand-written message holds all of them
// (StoredFieldLoc).
template <uint32_t kOffset, int kId, int kNumber>
class FieldSpec {
 public:
  static constexpr bool kAtMessageStart = true;

  FieldSpec() = default;
  explicit FieldSpec(SourceOffset /*source*/) {}

  static constexpr uint32_t Offset() { return kOffset; }
  static constexpr int Id() { return kId; }
  static constexpr int Number() { return kNumber; }

 protected:
  const Message* Parent() const {
    return reinterpret_cast<const Message*>(this);
  }
};

template <uint32_t kOffset, int kId, int kNumber>
class FieldSpecAt {
 public:
  static constexpr bool kAtMessageStart = false;

  FieldSpecAt() = default;
  explicit FieldSpecAt(SourceOffset source) : source_offset_(source.value) {}

  static constexpr uint32_t Offset() { return kOffset; }
  static constexpr int Id() { return kId; }
  static constexpr int Number() { return kNumber; }

 protected:
  const Message* Parent() const {
    return Message::GetMessage(this, source_offset_);
  }

 private:
  uint32_t source_offset_ = 0;
};

class StoredFieldLoc {
 public:
  static constexpr bool kAtMessageStart = false;

  StoredFieldLoc() = default;
  explicit StoredFieldLoc(uint32_t source_offset, uint32_t offset, int id,
                          int number)
      : source_offset_(source_offset),
        offset_(offset),
        id_(id),
        number_(number) {}

  uint32_t Offset() const { return offset_; }
  int Id() const { return id_; }
  int Number() const { return number_; }

 protected:
  const Message* Parent() const {
    return Message::GetMessage(this, source_offset_);
  }

 private:
  uint32_t source_offset_ = 0;
  uint32_t offset_ = static_cast<uint32_t>(-1);
  int id_ = 0;
  int number_ = 0;
};

namespace internal {
// The location of a field holding values of its own, given where it is.
template <typename Loc>
struct StatefulLoc {
  using type = Loc;
};
template <uint32_t kOffset, int kId, int kNumber>
struct StatefulLoc<FieldSpec<kOffset, kId, kNumber>> {
  using type = FieldSpecAt<kOffset, kId, kNumber>;
};
}  // namespace internal

// The members of a field that depend on where it is.  Loc is first in the
// field so that its address is the field's.
template <typename Loc>
class FieldBase : public Loc {
 public:
  using Loc::Id;
  using Loc::Loc;
  using Loc::Number;

  // A message whose fields are not at their compiled locations was written
  // with another version of its schema, so its fields are looked up in its
  // metadata on each access.
  int32_t FindFieldOffset() const {
    const Message* message = Parent();
    if (message->FieldLocationsKnown()) {
      return static_cast<int32_t>(Offset());
    }
    return message->FindField(static_cast<uint32_t>(Number())).offset;
  }

  int32_t FindFieldId() const {
    const Message* message = Parent();
    if (message->FieldLocationsKnown()) {
      return Id();
    }
    return message->FindField(static_cast<uint32_t>(Number())).id;
  }

  // For printing, at the indent of the message holding the field.
  void Indent(int indent) const { Parent()->Indent(indent); }
  void PrintIndent(std::ostream& os) const { Parent()->PrintIndent(os); }
  int GetIndent() const { return Parent()->GetIndent(); }

 protected:
  using Loc::Offset;
  using Loc::Parent;

  const RuntimeHandle& GetRuntime() const { return Parent()->runtime; }

  ::phaser::PayloadBuffer* GetBuffer() const { return Parent()->runtime->pb; }

  ::phaser::PayloadBuffer** GetBufferAddr() const {
    const Message* message = Parent();
    if (ABSL_PREDICT_FALSE(!message->IsMutable())) {
      Field::ThrowReadonlyMessage();
    }
    return &message->runtime->pb;
  }

  ::phaser::BufferOffset GetMessageBinaryStart() const {
    return Parent()->absolute_binary_offset;
  }

  bool IsPresentBit(uint32_t field_id) const {
    if (field_id == static_cast<uint32_t>(-1)) {
      return false;
    }
    return GetRuntime()->IsPresent(
        field_id, GetMessageBinaryStart() + Field::kPresenceMaskOffset);
  }

  void RequireMutable() const { (void)MutableBuffer(); }

  // The buffer of the message holding this field, which must be mutable.
  ::phaser::PayloadBuffer* MutableBuffer() const { return *GetBufferAddr(); }

  // Writes 'v' at the field's compiled location, where it is in every mutable
  // message, and marks the field present.  Everything is read before the
  // payload is written, which the compiler must assume can change the field.
  template <typename T>
  void StoreValue(T v) {
    const Message* message = Parent();
    ::phaser::PayloadBuffer* pb = MutableBuffer();
    const ::phaser::BufferOffset start = message->absolute_binary_offset;
    const uint32_t id = static_cast<uint32_t>(Id());
    pb->Set(start + Offset(), v);
    pb->SetPresenceBit(id, start + Field::kPresenceMaskOffset);
  }

  void ClearValue() {
    const Message* message = Parent();
    MutableBuffer()->ClearPresenceBit(
        static_cast<uint32_t>(Id()),
        message->absolute_binary_offset + Field::kPresenceMaskOffset);
  }
};

// Names the members of FieldBase in a field class template, which does not
// otherwise see them in its dependent base.  Leaves the access public.
#define PHASER_FIELD_BASE_MEMBERS(Base) \
 public:                                \
  using Base::Id;                       \
  using Base::Number;                   \
  using Base::FindFieldOffset;          \
  using Base::FindFieldId;              \
  using Base::Indent;                   \
  using Base::PrintIndent;              \
  using Base::GetIndent;                \
                                        \
 protected:                             \
  using Base::Offset;                   \
  using Base::Parent;                   \
  using Base::GetRuntime;               \
  using Base::GetBuffer;                \
  using Base::GetBufferAddr;            \
  using Base::GetMessageBinaryStart;    \
  using Base::IsPresentBit;             \
  using Base::RequireMutable;           \
  using Base::MutableBuffer;            \
  using Base::StoreValue;               \
  using Base::ClearValue;               \
                                        \
 public:

namespace internal {
// Stores 'v' at 'offset' in the message binary at 'binary' and marks field
// 'id' present, as the field's setter does.
template <typename T>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void StoreScalarAt(char* binary,
                                                       uint32_t offset,
                                                       uint32_t id, T v) {
  memcpy(binary + offset, &v, sizeof(v));
  reinterpret_cast<uint8_t*>(binary)[Field::kPresenceMaskOffset + id / 8] |=
      uint8_t(1U << (id % 8));
}
}  // namespace internal

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

#define DEFINE_PRIMITIVE_FIELD(cname, type)                                    \
  template <bool FixedSize = false, bool Signed = false,                       \
            typename Loc = StoredFieldLoc>                                     \
  class cname##Field : public FieldBase<Loc> {                                 \
    PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)                                  \
    using FieldBase<Loc>::FieldBase;                                           \
    cname##Field() = default;                                                  \
    cname##Field(const cname##Field&) = default;                               \
    cname##Field(cname##Field&&) = default;                                    \
    cname##Field& operator=(const cname##Field& other) {                       \
      return Assign(other);                                                    \
    }                                                                          \
    cname##Field& operator=(cname##Field&& other) noexcept {                   \
      return Assign(other);                                                    \
    }                                                                          \
    template <typename OtherLoc>                                               \
    cname##Field& operator=(                                                   \
        const cname##Field<FixedSize, Signed, OtherLoc>& other) {              \
      return Assign(other);                                                    \
    }                                                                          \
    operator type() const { return Get(); }                                    \
    cname##Field& operator=(type v) {                                          \
      Set(v);                                                                  \
      return *this;                                                            \
    }                                                                          \
    type Get() const {                                                         \
      int32_t offset = FindFieldOffset();                                      \
      if (offset < 0) {                                                        \
        return type();                                                         \
      }                                                                        \
      const type* _phaser_addr = GetRuntime()->template ToAddress<const type>( \
          GetMessageBinaryStart() +                                            \
          static_cast<::phaser::BufferOffset>(offset));                        \
      if (_phaser_addr == nullptr) {                                           \
        return type();                                                         \
      }                                                                        \
      return *_phaser_addr;                                                    \
    }                                                                          \
    type GetForPrinting() const { return Get(); }                              \
    bool IsPresent() const {                                                   \
      return IsPresentBit(static_cast<uint32_t>(FindFieldId()));               \
    }                                                                          \
                                                                               \
    void Set(type v) { StoreValue(v); }                                        \
    void Clear() { ClearValue(); }                                             \
    template <typename OtherLoc>                                               \
    bool operator==(                                                           \
        const cname##Field<FixedSize, Signed, OtherLoc>& other) const {        \
      return Get() == other.Get();                                             \
    }                                                                          \
    template <typename OtherLoc>                                               \
    bool operator!=(                                                           \
        const cname##Field<FixedSize, Signed, OtherLoc>& other) const {        \
      return !(*this == other);                                                \
    }                                                                          \
    size_t SerializedSize() const {                                            \
      if constexpr (FixedSize) {                                               \
        return ProtoBuffer::TagSize(Number(),                                  \
                                    ProtoBuffer::FixedWireType<type>()) +      \
               sizeof(type);                                                   \
      } else {                                                                 \
        return ProtoBuffer::TagSize(Number(), WireType::kVarint) +             \
               ProtoBuffer::VarintSize<type, Signed>(Get());                   \
      }                                                                        \
    }                                                                          \
    absl::Status Serialize(ProtoMessageWriter buffer) const {                  \
      if constexpr (FixedSize) {                                               \
        return buffer.SerializeFixed<type>(Number(), Get());                   \
      } else {                                                                 \
        return buffer.SerializeVarint<type, Signed>(Number(), Get());          \
      }                                                                        \
    }                                                                          \
                                                                               \
    absl::Status Deserialize(ProtoBuffer& buffer) {                            \
      absl::StatusOr<type> v;                                                  \
      if constexpr (FixedSize) {                                               \
        v = buffer.DeserializeFixed<type>();                                   \
      } else {                                                                 \
        v = buffer.DeserializeVarint<type, Signed>();                          \
      }                                                                        \
      if (!v.ok()) {                                                           \
        return v.status();                                                     \
      }                                                                        \
      Set(*v);                                                                 \
      return absl::OkStatus();                                                 \
    }                                                                          \
                                                                               \
   private:                                                                    \
    template <typename Other>                                                  \
    cname##Field& Assign(const Other& other) {                                 \
      if (other.IsPresent()) {                                                 \
        Set(other.Get());                                                      \
      } else {                                                                 \
        Clear();                                                               \
      }                                                                        \
      return *this;                                                            \
    }                                                                          \
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
template <bool F, bool S, typename L>
struct FieldRelease<Int32Field<F, S, L>> : NoFieldRelease {};
template <bool F, bool S, typename L>
struct FieldRelease<Uint32Field<F, S, L>> : NoFieldRelease {};
template <bool F, bool S, typename L>
struct FieldRelease<Int64Field<F, S, L>> : NoFieldRelease {};
template <bool F, bool S, typename L>
struct FieldRelease<Uint64Field<F, S, L>> : NoFieldRelease {};
template <bool F, bool S, typename L>
struct FieldRelease<DoubleField<F, S, L>> : NoFieldRelease {};
template <bool F, bool S, typename L>
struct FieldRelease<FloatField<F, S, L>> : NoFieldRelease {};
template <bool F, bool S, typename L>
struct FieldRelease<BoolField<F, S, L>> : NoFieldRelease {};

template <bool F, bool S, typename L>
struct FieldResetClear<Int32Field<F, S, L>> : NoResetClear {};
template <bool F, bool S, typename L>
struct FieldResetClear<Uint32Field<F, S, L>> : NoResetClear {};
template <bool F, bool S, typename L>
struct FieldResetClear<Int64Field<F, S, L>> : NoResetClear {};
template <bool F, bool S, typename L>
struct FieldResetClear<Uint64Field<F, S, L>> : NoResetClear {};
template <bool F, bool S, typename L>
struct FieldResetClear<DoubleField<F, S, L>> : NoResetClear {};
template <bool F, bool S, typename L>
struct FieldResetClear<FloatField<F, S, L>> : NoResetClear {};
template <bool F, bool S, typename L>
struct FieldResetClear<BoolField<F, S, L>> : NoResetClear {};
}  // namespace internal

struct InternalIntStringizer {
  std::string operator()(int i) const { return std::to_string(i); }
};

struct InternalIntParser {
  int operator()(const std::string& s) const { return std::stoi(s); }
};

template <typename Enum = int, typename Stringizer = InternalIntStringizer,
          typename Parser = InternalIntParser, typename Loc = StoredFieldLoc>
class EnumField : public FieldBase<Loc> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  using T = typename std::underlying_type<Enum>::type;
  using FieldBase<Loc>::FieldBase;
  EnumField() = default;
  EnumField(const EnumField&) = default;
  EnumField(EnumField&&) = default;
  EnumField& operator=(const EnumField& other) { return Assign(other); }
  EnumField& operator=(EnumField&& other) noexcept { return Assign(other); }
  template <typename OtherLoc>
  EnumField& operator=(
      const EnumField<Enum, Stringizer, Parser, OtherLoc>& other) {
    return Assign(other);
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
    return IsPresentBit(static_cast<uint32_t>(FindFieldId()));
  }

  std::string ToString() const { return Stringizer()(Get()); }

  Enum ParseFromString(const std::string& s) { Set(Parser(s)); }

  T GetUnderlying() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    const T* addr = GetRuntime()->template ToAddress<const T>(
        GetMessageBinaryStart() + static_cast<::phaser::BufferOffset>(offset));
    if (addr == nullptr) {
      return 0;
    }
    return *addr;
  }

  void Set(Enum e) {
    StoreValue(static_cast<typename std::underlying_type<Enum>::type>(e));
  }

  void Set(T e) { StoreValue(e); }

  void Clear() { ClearValue(); }

  template <typename OtherLoc>
  bool operator==(
      const EnumField<Enum, Stringizer, Parser, OtherLoc>& other) const {
    return Get() == other.Get();
  }
  template <typename OtherLoc>
  bool operator!=(
      const EnumField<Enum, Stringizer, Parser, OtherLoc>& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    return ProtoBuffer::TagSize(Number(), WireType::kVarint) +
           ProtoBuffer::VarintSize<int32_t, false>(
               static_cast<int32_t>(GetUnderlying()));
  }

  absl::Status Serialize(ProtoMessageWriter buffer) const {
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
  template <typename Other>
  EnumField& Assign(const Other& other) {
    if (other.IsPresent()) {
      Set(other.Get());
    } else {
      Clear();
    }
    return *this;
  }
};

namespace internal {
template <typename Enum, typename Stringizer, typename Parser, typename L>
struct FieldRelease<EnumField<Enum, Stringizer, Parser, L>> : NoFieldRelease {};
template <typename Enum, typename Stringizer, typename Parser, typename L>
struct FieldResetClear<EnumField<Enum, Stringizer, Parser, L>> : NoResetClear {
};
}  // namespace internal

// String field with an offset inline in the message.
template <typename Loc>
class BasicStringField : public FieldBase<Loc>,
                         public StringFieldFacade<BasicStringField<Loc>> {
  PHASER_FIELD_BASE_MEMBERS(FieldBase<Loc>)
  using FieldBase<Loc>::FieldBase;
  BasicStringField() = default;
  BasicStringField(const BasicStringField&) = default;
  BasicStringField(BasicStringField&&) = default;
  BasicStringField& operator=(const BasicStringField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  BasicStringField& operator=(BasicStringField&& other) noexcept {
    return operator=(static_cast<const BasicStringField&>(other));
  }
  template <typename OtherLoc>
  BasicStringField& operator=(const BasicStringField<OtherLoc>& other) {
    Assign(other);
    return *this;
  }
  using StringFieldFacade<BasicStringField<Loc>>::operator==;
  using StringFieldFacade<BasicStringField<Loc>>::operator!=;
  template <typename OtherLoc>
  bool operator==(const BasicStringField<OtherLoc>& other) const {
    return Get() == other.Get();
  }
  template <typename OtherLoc>
  bool operator!=(const BasicStringField<OtherLoc>& other) const {
    return Get() != other.Get();
  }
  operator std::string_view() const { return Get(); }
  BasicStringField& operator=(const std::string& s) {
    Set(s);
    return *this;
  }
  BasicStringField& operator=(std::string_view s) {
    Set(s);
    return *this;
  }
  BasicStringField& operator=(const char* s) {
    ::phaser::PayloadBuffer::SetString(
        GetBufferAddr(), std::string_view(s, std::strlen(s)),
        StringHeaderOffset());
    return *this;
  }

  std::string_view Get() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return std::string_view();
    }
    return GetRuntime()->GetStringView(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(offset));
  }

  bool IsPresent() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return false;
    }
    const ::phaser::BufferOffset* addr =
        GetRuntime()->template ToAddress<const ::phaser::BufferOffset>(
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
        GetRuntime()->template ToAddress<::phaser::StringHeader>(
            StringHeaderOffset());
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
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return 0;
    }
    return GetRuntime()->StringSize(
        GetMessageBinaryStart() +
        static_cast<::phaser::BufferOffset>(offset));
  }

  const char* data() const {
    int32_t offset = FindFieldOffset();
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

  absl::Status Serialize(ProtoMessageWriter buffer) const {
    size_t s = size();
    return buffer.SerializeLengthDelimited(Number(), data(), s);
  }

  absl::Status Deserialize(ProtoBuffer& buffer) {
    absl::StatusOr<std::string_view> s = buffer.DeserializeString();
    if (!s.ok()) {
      return s.status();
    }
    if (::phaser::PayloadBuffer::SetString(GetBufferAddr(), *s,
                                           StringHeaderOffset()) == nullptr) {
      return internal::AllocationFailedError();
    }
    return absl::OkStatus();
  }

 private:
  template <typename Other>
  void Assign(const Other& other) {
    if (other.IsPresent()) {
      Set(other.Get());
    } else {
      Clear();
    }
  }

  ::phaser::BufferOffset StringHeaderOffset() const {
    int32_t offset = FindFieldOffset();
    if (offset < 0) {
      return GetMessageBinaryStart() + Offset();
    }
    return GetMessageBinaryStart() +
           static_cast<::phaser::BufferOffset>(offset);
  }
};

using StringField = BasicStringField<StoredFieldLoc>;

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
    if (IsPlaceholder() || other.IsPlaceholder()) {
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
    if (destination.data() == nullptr) {
      return *this;
    }
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
    Set(std::string_view(s, std::strlen(s)));
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
      PHASER_THROW(std::logic_error("cannot mutate a readonly Phaser string"));
    }
    return &msg_->runtime->pb;
  }

  const Message* msg_ = nullptr;
  ::phaser::BufferOffset absolute_binary_offset_ =
      0;  // Offset into
          // ::phaser::PayloadBuffer of
          // ::phaser::StringHeader
};

namespace internal {
// The handle of a message field, made when first used.  Unlike
// std::optional::emplace, Make() has no path that destroys a handle already
// there, which keeps it small enough to inline.
template <typename MessageType>
class HandleSlot {
 public:
  HandleSlot() {}
  HandleSlot(const HandleSlot& other) {
    if (other.made_) {
      new (&handle_) MessageType(other.handle_);
      made_ = true;
    }
  }
  HandleSlot(HandleSlot&& other) noexcept(
      std::is_nothrow_move_constructible<MessageType>::value) {
    if (other.made_) {
      new (&handle_) MessageType(std::move(other.handle_));
      made_ = true;
    }
  }
  HandleSlot& operator=(const HandleSlot&) = delete;
  ~HandleSlot() {
    if (made_) {
      handle_.~MessageType();
    }
  }

  bool has_value() const { return made_; }

  // Requires !has_value().
  MessageType& Make() {
    new (&handle_) MessageType(InternalDefault{});
    made_ = true;
    return handle_;
  }

  MessageType& operator*() { return handle_; }
  MessageType* operator->() { return &handle_; }

 private:
  union {
    MessageType handle_;
  };
  bool made_ = false;
};
}  // namespace internal

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

template <typename MessageType, bool kInline = false,
          typename Loc = StoredFieldLoc>
class IndirectMessageField
    : public FieldBase<typename internal::StatefulLoc<Loc>::type> {
  using Base = FieldBase<typename internal::StatefulLoc<Loc>::type>;
  PHASER_FIELD_BASE_MEMBERS(Base)
  using Base::Base;
  IndirectMessageField() = default;
  IndirectMessageField(const IndirectMessageField&) = default;
  IndirectMessageField(IndirectMessageField&&) = default;
  IndirectMessageField& operator=(const IndirectMessageField& other) {
    if (this != &other) {
      Assign(other);
    }
    return *this;
  }
  IndirectMessageField& operator=(IndirectMessageField&& other) noexcept {
    return operator=(static_cast<const IndirectMessageField&>(other));
  }
  template <typename OtherLoc>
  IndirectMessageField& operator=(
      const IndirectMessageField<MessageType, kInline, OtherLoc>& other) {
    Assign(other);
    return *this;
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
    return Bind(message);
  }

  bool IsPresent() const { return MessageAt(Locate()) != 0; }

  MessageType* Mutable() {
    if (ABSL_PREDICT_FALSE(!Parent()->IsMutable())) {
      return ReadonlyMutable();
    }
    if constexpr (kInline) {
      return &BindMutable(InstallInline());
    }
    ::phaser::BufferOffset* addr = GetIndirectAddress(Offset());
    if (addr != nullptr && *addr != 0) {
      // Already allocated.
      return &BindMutable(*addr);
    }
    // Allocate a new message.
    void* msg_addr = ::phaser::PayloadBuffer::Allocate(
        GetBufferAddr(), MessageType::BinarySize());
    if (msg_addr == nullptr) {
      MessageType& failed = Handle();
      internal::BindToAllocationFailureSink(failed);
      return &failed;
    }
    ::phaser::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    MessageType& handle = BindMutable(msg_offset);

    // Buffer might have moved, get address of indirect again.
    addr = GetIndirectAddress(Offset());
    *addr = msg_offset;  // Put message field offset into message.

    // Install the metadata into the binary message.
    handle.template InstallMetadata<MessageType>();
    return &handle;
  }

  void SetOffset(::phaser::BufferOffset offset) {
    static_assert(!kInline, "an inline message is not at an offset");
    ::phaser::BufferOffset* addr = GetIndirectAddress(Offset());
    if (addr == nullptr) {
      return;
    }
    if (*addr != 0) {
      // Already set, clear the exising message
      Clear();
    }
    *addr = offset;
    Bind(offset);
  }

  void Clear() {
    if constexpr (kInline) {
      const ::phaser::BufferOffset message = GetMessageBinaryStart() + Offset();
      if (*MutableBuffer()->template AddressOf<::phaser::BufferOffset>(
              message) == 0) {
        return;
      }
      if constexpr (internal::HasSyncToPayloadWork<MessageType>::value) {
        // The handle holds values of the message's fields too.
        Bind(message).Clear();
      } else {
        internal::ReleaseMessageContents<MessageType>(GetRuntime(), message);
      }
      // Releasing can move the buffer.
      memset(GetRuntime()->pb->template AddressOf<char>(message), 0,
             MessageType::BinarySize());
      return;
    }
    ::phaser::BufferOffset* addr = GetIndirectAddress(Offset());
    if (addr == nullptr || *addr == 0) {
      return;
    }
    const ::phaser::BufferOffset old_offset = *addr;
    if constexpr (!internal::ClearReleasesNothing<MessageType>::value ||
                  internal::HasSyncToPayloadWork<MessageType>::value) {
      // Clear the message.
      Bind(old_offset).Clear();
    }
    // Delete the memory in the payload buffer.
    GetBuffer()->Free(GetRuntime()->ToAddress(old_offset));
    // Zero out the offset to the message.
    addr = GetIndirectAddress(Offset());
    if (addr != nullptr) {
      *addr = 0;
    }
  }

  template <typename OtherLoc>
  bool operator==(
      const IndirectMessageField<MessageType, kInline, OtherLoc>& other) const {
    return Get() == other.Get();
  }
  template <typename OtherLoc>
  bool operator!=(
      const IndirectMessageField<MessageType, kInline, OtherLoc>& other) const {
    return !(*this == other);
  }

  size_t SerializedSize() const {
    const Location location = Locate();
    if (location.offset < 0) {
      return 0;
    }
    if (const ::phaser::BufferOffset message = MessageAt(location);
        message != 0) {
      Bind(message);
    }
    return ProtoBuffer::LengthDelimitedSize(Number(),
                                            Handle().SerializedSize());
  }

  absl::Status Serialize(ProtoMessageWriter buffer) const {
    const Location location = Locate();
    if (location.offset < 0) {
      return absl::OkStatus();
    }
    if (const ::phaser::BufferOffset message = MessageAt(location);
        message != 0) {
      Bind(message);
    }

    size_t size = Handle().SerializedSize();
    if (absl::Status status =
            buffer.SerializeLengthDelimitedHeader(Number(), size);
        !status.ok()) {
      return status;
    }

    return Handle().PhaserSerialize(buffer);
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
    if (msg_addr == nullptr) {
      return internal::AllocationFailedError();
    }
    ::phaser::BufferOffset msg_offset = GetRuntime()->ToOffset(msg_addr);
    // Assign to the message.
    MessageType& handle = BindMutable(msg_offset);

    // Buffer might have moved, get address of indirect again.
    ::phaser::BufferOffset* addr = GetIndirectAddress(Offset());
    *addr = msg_offset;  // Put message field offset into message.

    // Install the metadata into the binary message.
    if (!handle.template InstallMetadata<MessageType>()) {
      return internal::AllocationFailedError();
    }

    ProtoBuffer sub_buffer(s.value());
    return handle.Deserialize(sub_buffer);
  }

  // Values waiting to reach the payload are held only by the handle, so
  // there is nothing to write until the handle has been made.
  void SyncToPayload() const {
    if constexpr (internal::HasSyncToPayloadWork<MessageType>::value) {
      if (!SyncPending()) {
        return;
      }
      const ::phaser::BufferOffset message = MessageAt(Locate());
      if (message == 0) {
        return;
      }
      MessageType& handle = Handle();
      if (Parent()->IsMutable()) {
        handle.BindBorrowedMutable(GetRuntime(), message);
      } else {
        handle.template BindBorrowed<MessageType>(GetRuntime(), message);
      }
      handle.MessageType::SyncToPayload();
    }
  }

  // Whether the handle holds values SyncToPayload() would write.
  bool SyncPending() const {
    if constexpr (internal::HasSyncToPayloadWork<MessageType>::value) {
      return msg_.has_value() && internal::SyncPending(*msg_);
    } else {
      return false;
    }
  }

  // Clear() of the handle alone, for when the buffer holding the message is
  // reset.
  void ClearHandle() {
    if (msg_.has_value()) {
      msg_->PhaserClearHandles();
    }
  }

  // Prints the message the field holds, 'extra' more indented than the
  // field.
  void PrintMessage(std::ostream& os, int extra) const {
    const ::phaser::BufferOffset message = MessageAt(Locate());
    if (message == 0) {
      return;
    }
    const MessageType& handle = Bind(message);
    handle.SetIndent(GetIndent() + extra);
    os << handle;
  }

 protected:
  template <typename Other>
  void Assign(const Other& other) {
    if (other.IsPresent()) {
      (void)Mutable()->CloneFrom(other.Get());
    } else {
      Clear();
    }
  }

  static const MessageType& DefaultMessage() {
    static const MessageType message(InternalDefault{});
    return message;
  }

  bool InMutableMessage() const { return Parent()->IsMutable(); }

  // Sets every field of the inline message, which is in a mutable message,
  // from the PhaserROSMinSize() bytes of its ROS encoding at 'input', as
  // their setters would.
  void StoreFromROSUnchecked(const char* input) {
    static_assert(kInline);
    const ::phaser::BufferOffset message = InstallInline();
    MessageType::PhaserDeserializeFromROSUncheckedAt(
        GetRuntime()->pb->template AddressOf<char>(message), input);
  }

  // Where the field is in its message and whether the message it holds is
  // stored there, which depend on the schema the message was written with.
  struct Location {
    int32_t offset;
    bool is_inline;
  };
  // Mutable() of a readonly message still reaches a message that is there,
  // through a readonly handle.  Only making one needs a mutable message.
  MessageType* ReadonlyMutable() {
    const ::phaser::BufferOffset message = MessageAt(Locate());
    if (message == 0) {
      Field::ThrowReadonlyMessage();
    }
    return &Bind(message);
  }

  // The inline message in a mutable message, given its metadata if it has
  // none yet.
  ABSL_ATTRIBUTE_ALWAYS_INLINE ::phaser::BufferOffset InstallInline() {
    const ::phaser::BufferOffset message = GetMessageBinaryStart() + Offset();
    if (*GetRuntime()->pb->template AddressOf<::phaser::BufferOffset>(
            message) == 0) {
      // Finding the metadata can move the buffer.
      const ::phaser::BufferOffset metadata =
          Message::MetadataFor<MessageType>(GetRuntime());
      *GetRuntime()->pb->template AddressOf<::phaser::BufferOffset>(message) =
          metadata;
    }
    return message;
  }

  // Making the handle makes those of every message field inside it, so it is
  // made when first used.
  MessageType& Handle() const {
    if (!msg_.has_value()) {
      return msg_.Make();
    }
    return *msg_;
  }

  MessageType& Bind(::phaser::BufferOffset message) const {
    MessageType& handle = Handle();
    handle.template BindBorrowed<MessageType>(GetRuntime(), message);
    return handle;
  }

  // Bind() in a mutable message.
  MessageType& BindMutable(::phaser::BufferOffset message) {
    MessageType& handle = Handle();
    handle.BindBorrowedMutable(GetRuntime(), message);
    return handle;
  }

  Location Locate() const {
    const Message* message = Parent();
    if (message->FieldLocationsKnown()) {
      return {static_cast<int32_t>(Offset()), kInline};
    }
    const FieldLocation location =
        message->FindField(static_cast<uint32_t>(Number()));
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

  mutable internal::HandleSlot<MessageType> msg_;
};

namespace internal {
template <typename L>
struct FieldRelease<BasicStringField<L>> : std::true_type {
  static void Release(const RuntimeHandle& runtime,
                      ::phaser::BufferOffset field) {
    runtime->pb->FreeAt(OffsetAt(runtime->pb, field));
  }
};

template <typename MessageType, typename L>
struct FieldRelease<IndirectMessageField<MessageType, false, L>>
    : IndirectMessageRelease<MessageType> {};

template <typename MessageType, typename L>
struct FieldRelease<IndirectMessageField<MessageType, true, L>>
    : InlineMessageRelease<MessageType> {};

template <typename L>
struct FieldResetClear<BasicStringField<L>> : NoResetClear {};

// Clear() clears the handle of a message whose fields' handles hold values
// from outside the payload.  The reset frees everything else.
template <typename MessageType, bool kInline, typename L>
struct FieldResetClear<IndirectMessageField<MessageType, kInline, L>>
    : std::integral_constant<
          ResetClear,
          !HasSyncToPayloadWork<MessageType>::value ? ResetClear::kNone
          : ClearsHandlesAlone<MessageType>::value  ? ResetClear::kHandle
                                                    : ResetClear::kBefore> {};
}  // namespace internal

template <typename MessageType, typename Loc = StoredFieldLoc>
using InlineMessageField = IndirectMessageField<MessageType, true, Loc>;

template <typename MessageType>
class MessageObject {
 public:
  MessageObject() : msg_(InternalDefault{}) {}
  explicit MessageObject(RuntimeHandle runtime, uint32_t absolute_binary_offset)
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

  absl::Status Serialize(ProtoMessageWriter buffer) const {
    return msg_.PhaserSerialize(buffer);
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
