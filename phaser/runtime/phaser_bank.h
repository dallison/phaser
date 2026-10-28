// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/message.h"
#include "phaser/runtime/ros_wireformat.h"
#include "phaser/runtime/wireformat.h"

namespace phaser {

namespace arrow {
class SchemaBuilder;
class TypeSet;
class FieldWriter;
class FieldReader;
}  // namespace arrow

struct BankInfo {
  void (*stream_to)(const Message& msg, std::ostream& os, int indent);
  absl::Status (*serialize_to_buffer)(const Message& msg, ProtoBuffer& buffer);
  absl::Status (*deserialize_from_buffer)(Message& msg, ProtoBuffer& buffer);
  size_t (*serialized_size)(const Message& msg);
  absl::Status (*serialize_at_offset)(::phaser::RuntimeHandle runtime,
                                      ::phaser::BufferOffset offset,
                                      ProtoBuffer& buffer);
  absl::Status (*deserialize_at_offset)(::phaser::RuntimeHandle runtime,
                                        ::phaser::BufferOffset offset,
                                        ProtoBuffer& buffer);
  size_t (*serialized_size_at_offset)(::phaser::RuntimeHandle runtime,
                                      ::phaser::BufferOffset offset);
  Message* (*allocate_at_offset)(::phaser::RuntimeHandle runtime,
                                 ::phaser::BufferOffset offset);
  std::pair<Message*, ::phaser::BufferOffset> (*allocate)(
      ::phaser::RuntimeHandle runtime);
  void (*clear)(Message& msg);
  absl::Status (*copy)(const Message& src, Message& dst);
  const Message* (*make_existing)(::phaser::RuntimeHandle runtime,
                                  const void* data);
  size_t (*binary_size)();
  const MessageInfo* (*message_info)();
  bool (*has_field)(const Message& msg, int number);
  void* (*get_field_by_name)(Message& msg, const std::string& name);
  void* (*get_field_by_number)(Message& msg, int number);
  absl::Status (*protobuf_to_ros)(std::string_view bytes, ROSBuffer& output);
  absl::Status (*phaser_to_ros)(absl::Span<const char> bytes,
                                ROSBuffer& output);
  std::string_view (*ros_data_type)();
  std::string (*ros_md5)();
  std::string_view (*ros_definition)();
  absl::Status (*ros_to_protobuf)(absl::Span<const char> bytes,
                                  ProtoBuffer& output);
  absl::Status (*phaser_to_protobuf)(absl::Span<const char> bytes,
                                     ProtoBuffer& output);
  // Protobuf full name (package.Outer.Inner) and unqualified message name.
  std::string_view (*protobuf_full_name)();
  std::string_view (*protobuf_name)();
  // Arrow conversion. Null when the message was registered without it.
  absl::Status (*append_arrow_schema)(::phaser::arrow::SchemaBuilder* builder,
                                      std::string_view name, bool nullable,
                                      const ::phaser::arrow::TypeSet* types);
  absl::Status (*collect_arrow_types_at_offset)(::phaser::RuntimeHandle runtime,
                                                ::phaser::BufferOffset offset,
                                                ::phaser::arrow::TypeSet* types);
  absl::Status (*write_arrow_at_offset)(::phaser::RuntimeHandle runtime,
                                        ::phaser::BufferOffset offset,
                                        ::phaser::arrow::FieldWriter* writer,
                                        const ::phaser::arrow::TypeSet* types);
  absl::Status (*read_arrow_at_offset)(::phaser::RuntimeHandle runtime,
                                       ::phaser::BufferOffset offset,
                                       const ::phaser::arrow::FieldReader* reader);
  // One message, phaser or protobuf wire, written as Avro or Parquet.
  // Null when the message was registered without these conversions.
  absl::Status (*protobuf_to_avro)(std::string_view bytes, std::string* out);
  absl::Status (*phaser_to_avro)(absl::Span<const char> bytes, std::string* out);
  absl::Status (*protobuf_to_parquet)(std::string_view bytes, std::string* out);
  absl::Status (*phaser_to_parquet)(absl::Span<const char> bytes,
                                    std::string* out);
};

extern std::unique_ptr<absl::flat_hash_map<std::string, BankInfo>>
    phaser_banks_;

absl::StatusOr<BankInfo*> GetPhaserBankInfo(std::string_view message_type);

void PhaserBankRegisterMessage(std::string_view name, const BankInfo& info);

absl::Status PhaserStreamTo(const std::string& message_type, const Message& msg,
                            std::ostream& os, int indent);
absl::StatusOr<std::string> PhaserBankDebugString(
    const std::string& message_type, const Message& msg);

absl::Status PhaserBankSerializeToBuffer(const std::string& message_type,
                                         const Message& msg,
                                         ProtoBuffer& buffer);
absl::Status PhaserBankDeserializeFromBuffer(const std::string& message_type,
                                             Message& msg, ProtoBuffer& buffer);
absl::StatusOr<size_t> PhaserBankSerializedSize(const std::string& message_type,
                                                const Message& msg);
absl::Status PhaserBankSerializeAtOffset(std::string_view message_type,
                                         ::phaser::RuntimeHandle runtime,
                                         ::phaser::BufferOffset offset,
                                         ProtoBuffer& buffer);
absl::Status PhaserBankDeserializeAtOffset(std::string_view message_type,
                                           ::phaser::RuntimeHandle runtime,
                                           ::phaser::BufferOffset offset,
                                           ProtoBuffer& buffer);
absl::StatusOr<size_t> PhaserBankSerializedSizeAtOffset(
    std::string_view message_type, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset);

// This allocates a message from the heap (using new) with its storage in the
// payload buffer.  The ownership of the heap memory is passed back to the
// caller, so you can make a std::unique_ptr<Message> from the result or
// otherwise manage the memory.
absl::StatusOr<Message*> PhaserBankAllocateAtOffset(
    const std::string& message_type, ::phaser::RuntimeHandle runtime,
    ::phaser::BufferOffset offset);

template <typename T>
inline absl::StatusOr<std::pair<T*, ::phaser::BufferOffset>> PhaserBankAllocate(
    const std::string& message_type, ::phaser::RuntimeHandle runtime) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  auto [msg, offset] = (*bank_info)->allocate(runtime);
  if (msg == nullptr) {
    return internal::AllocationFailedError();
  }
  return std::make_pair(static_cast<T*>(msg), offset);
}

template <>
inline absl::StatusOr<std::pair<Message*, ::phaser::BufferOffset>>
PhaserBankAllocate(const std::string& message_type,
                   ::phaser::RuntimeHandle runtime) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  std::pair<Message*, ::phaser::BufferOffset> result =
      (*bank_info)->allocate(runtime);
  if (result.first == nullptr) {
    return internal::AllocationFailedError();
  }
  return result;
}

absl::Status PhaserBankClear(const std::string& message_type, Message& msg);

absl::Status PhaserBankCopy(const std::string& message_type, const Message& src,
                            Message& dst);

absl::StatusOr<const Message*> PhaserBankMakeExisting(
    const std::string& message_type, ::phaser::RuntimeHandle runtime,
    const void* data);

absl::StatusOr<size_t> PhaserBankBinarySize(std::string_view message_type);

absl::StatusOr<const MessageInfo*> PhaserBankMessageInfo(
    const std::string& message_type);

absl::StatusOr<bool> PhaserBankHasField(const std::string& message_type,
                                        const Message& msg, int number);

// Which ways a plugin conversion runs. `from` and `to` on BankConversion name
// the forward direction.
enum class BankConversionDirection {
  kForward,
  kReverse,
  kBoth,
};

using BankConvertFn = absl::Status (*)(std::string_view full_name,
                                       absl::Span<const char> bytes,
                                       std::string* out, const void* options);

// One pair of formats. `forward` converts `from` to `to`. `reverse` converts
// `to` to `from`. The direction selects which of those functions are installed.
struct BankConversion {
  const char* from;
  const char* to;
  BankConversionDirection direction;
  BankConvertFn forward;
  BankConvertFn reverse;
};

// A conversion plugin. The plugin registers itself from a static initializer
// by calling PhaserBankRegisterConversionPlugin. `formats` is the names this
// plugin accepts. Registration fails when a conversion uses any other name,
// when `direction` does not match the function pointers, or when an edge is
// already registered. The library that contains the initializer has to be
// linked whole, or the linker drops an object that nothing calls.
struct BankConversionPlugin {
  const char* name;
  absl::Span<const char* const> formats;
  absl::Span<const BankConversion> conversions;
};

absl::Status PhaserBankRegisterConversionPlugin(
    const BankConversionPlugin& plugin);

// Dispatches to the registered plugin edge for `from` and `to`. `bytes` is one
// message in `from`. `out` receives one message in `to`. `options` is data for
// the plugin that owns the edge. A pair with no registered edge returns an
// error.
absl::Status PhaserBankConvert(std::string_view full_name, std::string_view from,
                               absl::Span<const char> bytes, std::string_view to,
                               std::string* out, const void* options = nullptr);

// Format names that appear in at least one registered conversion edge.
std::vector<std::string> PhaserBankRegisteredFormats();

bool PhaserBankHasConversion(std::string_view from, std::string_view to);

// All registered conversion edges as (from_format, to_format) pairs.
std::vector<std::pair<std::string, std::string>> PhaserBankConversionEdges();

template <typename Field>
absl::StatusOr<Field*> inline PhaserBankGetFieldByName(
    const std::string& message_type, Message& msg, const std::string& name) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  Field* result =
      reinterpret_cast<Field*>((*bank_info)->get_field_by_name(msg, name));
  return result;
}

template <typename Field>
absl::StatusOr<Field*> inline PhaserBankGetFieldByNumber(
    const std::string& message_type, Message& msg, int number) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(message_type);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  Field* result =
      reinterpret_cast<Field*>((*bank_info)->get_field_by_number(msg, number));
  return result;
}

}  // namespace phaser
