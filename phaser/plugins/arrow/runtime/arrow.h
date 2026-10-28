// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Builds and reads Arrow C Data Interface arrays. Generated messages append
// one row at a time; this is the shared buffer layer, not a schema interpreter.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "phaser/plugins/arrow/arrow_abi.h"
#include "phaser/runtime/payload_buffer.h"

namespace phaser {

class AnyMessage;
struct MessageRuntime;

namespace arrow {

// Protobuf full names of Any payloads seen while walking a batch. The order
// is the order of first appearance and becomes the dense-union child order,
// after the leading null arm.
class TypeSet {
 public:
  void Add(std::string_view full_name);
  int IndexOf(std::string_view full_name) const;
  const std::vector<std::string>& types() const { return order_; }

  // False when this payload offset was already walked. Stops an Any that
  // contains its own message from recursing forever.
  bool Visit(const void* buffer, uint32_t offset);

 private:
  struct VisitKey {
    const void* buffer = nullptr;
    uint32_t offset = 0;
    friend bool operator==(VisitKey lhs, VisitKey rhs) {
      return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset;
    }
    template <typename H>
    friend H AbslHashValue(H hash, VisitKey key) {
      return H::combine(std::move(hash), key.buffer, key.offset);
    }
  };

  std::vector<std::string> order_;
  absl::flat_hash_map<std::string, int> index_;
  absl::flat_hash_set<VisitKey> seen_;
};

class SchemaBuilder {
 public:
  SchemaBuilder();
  ~SchemaBuilder();
  SchemaBuilder(SchemaBuilder&&) noexcept;
  SchemaBuilder& operator=(SchemaBuilder&&) noexcept;
  SchemaBuilder(const SchemaBuilder&) = delete;
  SchemaBuilder& operator=(const SchemaBuilder&) = delete;

  // Pushes the message type being emitted. A second push of the same type
  // means the schema would be recursive, which Arrow's tree cannot hold.
  class TypeGuard {
   public:
    TypeGuard(SchemaBuilder& builder, std::string_view full_name);
    ~TypeGuard();
    TypeGuard(const TypeGuard&) = delete;
    TypeGuard& operator=(const TypeGuard&) = delete;
    bool cycle() const { return cycle_; }

   private:
    SchemaBuilder* builder_ = nullptr;
    bool cycle_ = false;
  };

  void BeginStruct(std::string_view name, bool nullable);
  void EndStruct();
  void BeginList(std::string_view name, bool nullable);
  void EndList();
  void BeginFixedList(std::string_view name, int32_t size);
  void EndFixedList();
  void BeginMap(std::string_view name, bool nullable);
  void EndMap();
  // Adds a leading null arm named "none". Further children are the union
  // alternatives, in order, with type ids 1, 2, ...
  void BeginUnion(std::string_view name, bool nullable);
  void EndUnion();

  void AddInt32(std::string_view name, bool nullable);
  void AddUInt32(std::string_view name, bool nullable);
  void AddInt64(std::string_view name, bool nullable);
  void AddUInt64(std::string_view name, bool nullable);
  void AddFloat(std::string_view name, bool nullable);
  void AddDouble(std::string_view name, bool nullable);
  void AddBool(std::string_view name, bool nullable);
  void AddString(std::string_view name, bool nullable);
  void AddBinary(std::string_view name, bool nullable);

  absl::Status status() const;
  absl::Status Finish(struct ArrowSchema* schema);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct ArrowNode;

class FieldWriter {
 public:
  absl::Status status() const;

  absl::Status Int32(int32_t value);
  absl::Status UInt32(uint32_t value);
  absl::Status Int64(int64_t value);
  absl::Status UInt64(uint64_t value);
  absl::Status Float(float value);
  absl::Status Double(double value);
  absl::Status Bool(bool value);
  absl::Status String(std::string_view value);
  absl::Status Binary(std::string_view value);
  absl::Status Null();

  FieldWriter Child(int index);
  absl::Status FinishStruct();
  absl::Status NullStruct();

  FieldWriter Values();
  absl::Status FinishList(int64_t count);
  absl::Status FinishFixed(int32_t count);

  absl::Status NullUnion();
  FieldWriter UnionChild(int child_index);

 private:
  friend class BatchWriter;
  explicit FieldWriter(ArrowNode* node) : node_(node) {}
  ArrowNode* node_ = nullptr;
};

class BatchWriter {
 public:
  static absl::StatusOr<BatchWriter> Open(const struct ArrowSchema* schema);
  FieldWriter Root();
  absl::Status Finish(struct ArrowArray* array);

  BatchWriter(BatchWriter&&) noexcept;
  BatchWriter& operator=(BatchWriter&&) noexcept;
  ~BatchWriter();

 private:
  explicit BatchWriter(std::unique_ptr<ArrowNode> root);
  std::unique_ptr<ArrowNode> root_;
};

class FieldReader {
 public:
  bool IsNull() const;
  absl::Status status() const;
  int64_t length() const;

  absl::StatusOr<int32_t> Int32() const;
  absl::StatusOr<uint32_t> UInt32() const;
  absl::StatusOr<int64_t> Int64() const;
  absl::StatusOr<uint64_t> UInt64() const;
  absl::StatusOr<float> Float() const;
  absl::StatusOr<double> Double() const;
  absl::StatusOr<bool> Bool() const;
  absl::StatusOr<std::string_view> String() const;
  absl::StatusOr<std::string_view> Binary() const;

  FieldReader Child(int index) const;
  FieldReader Element(int64_t index) const;

  int UnionType() const;
  absl::StatusOr<std::string_view> UnionChildName() const;
  FieldReader UnionValue() const;

 private:
  friend class BatchReader;
  const struct ArrowSchema* schema_ = nullptr;
  const struct ArrowArray* array_ = nullptr;
  int64_t row_ = 0;
  absl::Status status_;
};

class BatchReader {
 public:
  static absl::StatusOr<BatchReader> Open(const struct ArrowSchema* schema,
                                          const struct ArrowArray* array);
  int64_t length() const;
  FieldReader Row(int64_t row) const;

 private:
  const struct ArrowSchema* schema_ = nullptr;
  const struct ArrowArray* array_ = nullptr;
};

inline absl::Status WriteTimestamp(FieldWriter destination, int64_t seconds,
                                   int32_t nanos) {
  if (absl::Status status = destination.Child(0).Int64(seconds); !status.ok()) {
    return status;
  }
  if (absl::Status status = destination.Child(1).Int32(nanos); !status.ok()) {
    return status;
  }
  return destination.FinishStruct();
}

inline absl::Status ReadTimestamp(FieldReader source, int64_t* seconds,
                                  int32_t* nanos) {
  if (absl::Status status = source.status(); !status.ok()) {
    return status;
  }
  if (source.IsNull()) {
    *seconds = 0;
    *nanos = 0;
    return absl::OkStatus();
  }
  const FieldReader sec = source.Child(0);
  const FieldReader nano = source.Child(1);
  if (sec.IsNull()) {
    *seconds = 0;
  } else {
    absl::StatusOr<int64_t> value = sec.Int64();
    if (!value.ok()) {
      return value.status();
    }
    *seconds = *value;
  }
  if (nano.IsNull()) {
    *nanos = 0;
  } else {
    absl::StatusOr<int32_t> value = nano.Int32();
    if (!value.ok()) {
      return value.status();
    }
    *nanos = *value;
  }
  return absl::OkStatus();
}

inline absl::Status WriteHeader(FieldWriter destination, uint32_t seq,
                                int64_t stamp_seconds, int32_t stamp_nanos,
                                std::string_view frame_id) {
  if (absl::Status status = destination.Child(0).UInt32(seq); !status.ok()) {
    return status;
  }
  if (absl::Status status =
          WriteTimestamp(destination.Child(1), stamp_seconds, stamp_nanos);
      !status.ok()) {
    return status;
  }
  if (absl::Status status = destination.Child(2).String(frame_id);
      !status.ok()) {
    return status;
  }
  return destination.FinishStruct();
}

absl::Status CollectAny(const AnyMessage& any, TypeSet* types);
absl::Status WriteAny(const AnyMessage& any, FieldWriter& writer,
                      const TypeSet& types);
absl::Status ReadAny(AnyMessage* any, const FieldReader& row);

template <typename Message>
absl::Status MessagesToArrow(absl::Span<const Message> messages,
                             struct ArrowSchema* schema,
                             struct ArrowArray* array) {
  TypeSet types;
  for (const Message& message : messages) {
    absl::Status status = message.CollectArrowTypes(&types);
    if (!status.ok()) {
      return status;
    }
  }
  SchemaBuilder builder;
  if (absl::Status status = Message::AppendArrowSchema(
          builder, Message::FullName(), false, types);
      !status.ok()) {
    return status;
  }
  if (absl::Status status = builder.Finish(schema); !status.ok()) {
    return status;
  }
  absl::StatusOr<BatchWriter> writer = BatchWriter::Open(schema);
  if (!writer.ok()) {
    if (schema->release != nullptr) {
      schema->release(schema);
    }
    return writer.status();
  }
  for (const Message& message : messages) {
    if (absl::Status status = message.WriteArrow(writer->Root(), types);
        !status.ok()) {
      if (schema->release != nullptr) {
        schema->release(schema);
      }
      return status;
    }
  }
  if (absl::Status status = writer->Finish(array); !status.ok()) {
    if (schema->release != nullptr) {
      schema->release(schema);
    }
    return status;
  }
  return absl::OkStatus();
}

template <typename Message>
absl::Status MessagesFromArrow(const struct ArrowSchema* schema,
                               const struct ArrowArray* array,
                               std::vector<Message>* out) {
  absl::StatusOr<BatchReader> reader = BatchReader::Open(schema, array);
  if (!reader.ok()) {
    return reader.status();
  }
  out->clear();
  out->reserve(static_cast<size_t>(reader->length()));
  for (int64_t row = 0; row < reader->length(); ++row) {
    Message message;
    const FieldReader view = reader->Row(row);
    if (!view.IsNull()) {
      if (absl::Status status = message.ReadArrow(view); !status.ok()) {
        return status;
      }
    }
    out->push_back(std::move(message));
  }
  return absl::OkStatus();
}

// Arguments for an Arrow plugin operation on one message at a payload offset.
// Each operation reads the fields it needs and ignores the rest.
struct ArrowMessageArgs {
  MessageRuntime* runtime = nullptr;
  BufferOffset offset = 0;
  TypeSet* types = nullptr;
  FieldWriter* writer = nullptr;
  const TypeSet* schema_types = nullptr;
  const FieldReader* reader = nullptr;
  SchemaBuilder* schema_builder = nullptr;
  std::string_view field_name;
  bool nullable = false;
};

using ArrowPluginFn = absl::Status (*)(std::string_view full_name,
                                       const ArrowMessageArgs& args);

// One named operation. `name` and `invoke` stay valid for the process.
struct ArrowPluginOp {
  const char* name;
  ArrowPluginFn invoke;
};

inline constexpr std::string_view kArrowPluginCollectTypes = "collect-types";
inline constexpr std::string_view kArrowPluginWrite = "write";
inline constexpr std::string_view kArrowPluginRead = "read";
inline constexpr std::string_view kArrowPluginAppendSchema = "append-schema";

// Adds operations addressed by name. A second registration of the same name
// fails. The built-in collect, write, read, and append-schema operations are
// installed on the first call.
absl::Status RegisterArrowPlugin(absl::Span<const ArrowPluginOp> operations);

// Looks up `operation` and runs it for the message named `full_name`.
absl::Status CallArrowPlugin(std::string_view operation,
                             std::string_view full_name,
                             const ArrowMessageArgs& args);

// Registered operation names (including built-ins after first use).
std::vector<std::string> ListPluginOperations();

}  // namespace arrow
}  // namespace phaser
