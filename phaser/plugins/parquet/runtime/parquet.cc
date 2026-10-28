// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/parquet/parquet.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "phaser/plugins/arrow/arrow_abi.h"

namespace phaser {
namespace parquet {
namespace {

constexpr int kTypeBoolean = 0;
constexpr int kTypeInt32 = 1;
constexpr int kTypeInt64 = 2;
constexpr int kTypeFloat = 4;
constexpr int kTypeDouble = 5;
constexpr int kTypeByteArray = 6;

constexpr int kRequired = 0;
constexpr int kOptional = 1;
constexpr int kRepeated = 2;

constexpr int kUtf8 = 0;
constexpr int kMap = 1;
constexpr int kMapKeyValue = 2;
constexpr int kList = 3;
constexpr int kUint32 = 13;
constexpr int kUint64 = 14;
constexpr int kInt32 = 17;
constexpr int kInt64 = 18;

constexpr int kPlain = 0;
constexpr int kRle = 3;

constexpr int kCtBooleanTrue = 1;
constexpr int kCtBooleanFalse = 2;
constexpr int kCtByte = 3;
constexpr int kCtI32 = 5;
constexpr int kCtI64 = 6;
constexpr int kCtBinary = 8;
constexpr int kCtList = 9;
constexpr int kCtStruct = 12;

enum class Format {
  kOther,
  kNull,
  kBool,
  kInt32,
  kUInt32,
  kInt64,
  kUInt64,
  kFloat,
  kDouble,
  kUtf8,
  kBinary,
  kStruct,
  kList,
  kMap,
  kFixed,
  kUnion,
};

enum class Physical { kBool, kInt32, kInt64, kFloat, kDouble, kByteArray };

enum class Logical { kNone, kString, kList, kMap, kInt32, kInt64, kUint32, kUint64 };

enum class NodeKind { kGroup, kList, kMap, kUnion, kLeaf };

struct Node {
  NodeKind kind = NodeKind::kGroup;
  std::string name;
  bool optional = false;
  bool repeated = false;
  int converted = -1;
  Logical logical = Logical::kNone;
  Physical physical = Physical::kBool;
  bool bytes_as_string = false;
  int arrow_type = -1;
  int max_def = 0;
  int max_rep = 0;
  int leaf_id = -1;
  std::vector<Node> children;
};

struct Column {
  Physical physical = Physical::kBool;
  Logical logical = Logical::kNone;
  bool bytes_as_string = false;
  int max_def = 0;
  int max_rep = 0;
  int64_t value_count = 0;
  std::vector<std::string> path;
  std::vector<uint8_t> defs;
  std::vector<uint8_t> reps;
  std::string values;
  std::vector<uint8_t> bools;
};

Format Classify(std::string_view format) {
  if (format == "n") return Format::kNull;
  if (format == "b") return Format::kBool;
  if (format == "i") return Format::kInt32;
  if (format == "I") return Format::kUInt32;
  if (format == "l") return Format::kInt64;
  if (format == "L") return Format::kUInt64;
  if (format == "f") return Format::kFloat;
  if (format == "g") return Format::kDouble;
  if (format == "u") return Format::kUtf8;
  if (format == "z") return Format::kBinary;
  if (format == "+s") return Format::kStruct;
  if (format == "+l") return Format::kList;
  if (format == "+m") return Format::kMap;
  if (format.size() > 3 && format.substr(0, 3) == "+w:") return Format::kFixed;
  if (format.size() > 4 && format.substr(0, 4) == "+ud:") return Format::kUnion;
  return Format::kOther;
}

bool Nullable(const struct ArrowSchema* schema) {
  return schema != nullptr && (schema->flags & ARROW_FLAG_NULLABLE) != 0;
}

std::string ArrowName(const struct ArrowSchema* schema, std::string_view fallback) {
  if (schema == nullptr || schema->name == nullptr || schema->name[0] == '\0') {
    return std::string(fallback);
  }
  return schema->name;
}

absl::Status CheckSchema(const struct ArrowSchema* schema) {
  if (schema == nullptr || schema->format == nullptr) {
    return absl::InvalidArgumentError("Arrow schema is missing");
  }
  if (schema->dictionary != nullptr) {
    return absl::InvalidArgumentError("Arrow dictionaries are not supported");
  }
  return absl::OkStatus();
}

void AppendLe32(std::string* out, uint32_t value) {
  char bytes[4] = {static_cast<char>(value & 0xffU),
                   static_cast<char>((value >> 8) & 0xffU),
                   static_cast<char>((value >> 16) & 0xffU),
                   static_cast<char>((value >> 24) & 0xffU)};
  out->append(bytes, 4);
}

void AppendLe64(std::string* out, uint64_t value) {
  char bytes[8];
  for (int i = 0; i < 8; ++i) {
    bytes[i] = static_cast<char>((value >> (8 * i)) & 0xffU);
  }
  out->append(bytes, 8);
}

absl::StatusOr<Node> Build(const struct ArrowSchema* schema);

absl::StatusOr<Node> BuildLeaf(const struct ArrowSchema* schema, Physical physical,
                               Logical logical, bool bytes_as_string) {
  Node node;
  node.kind = NodeKind::kLeaf;
  node.name = ArrowName(schema, "item");
  node.optional = Nullable(schema);
  node.physical = physical;
  node.logical = logical;
  node.bytes_as_string = bytes_as_string;
  if (logical == Logical::kString) node.converted = kUtf8;
  if (logical == Logical::kUint32) node.converted = kUint32;
  if (logical == Logical::kUint64) node.converted = kUint64;
  if (logical == Logical::kInt32) node.converted = kInt32;
  if (logical == Logical::kInt64) node.converted = kInt64;
  return node;
}

absl::StatusOr<Node> BuildList(const struct ArrowSchema* schema) {
  if (schema->n_children != 1 || schema->children == nullptr ||
      schema->children[0] == nullptr) {
    return absl::InvalidArgumentError("Arrow list is missing its item");
  }
  absl::StatusOr<Node> item = Build(schema->children[0]);
  if (!item.ok()) return item.status();
  Node repeated;
  repeated.kind = NodeKind::kGroup;
  repeated.name = "list";
  repeated.repeated = true;
  repeated.children.push_back(std::move(*item));
  Node node;
  node.kind = NodeKind::kList;
  node.name = ArrowName(schema, "list");
  node.optional = Nullable(schema);
  node.converted = kList;
  node.logical = Logical::kList;
  node.children.push_back(std::move(repeated));
  return node;
}

absl::StatusOr<Node> BuildMap(const struct ArrowSchema* schema) {
  if (schema->n_children != 1 || schema->children == nullptr ||
      schema->children[0] == nullptr) {
    return absl::InvalidArgumentError("Arrow map is missing its entries");
  }
  const struct ArrowSchema* entries = schema->children[0];
  if (Classify(entries->format) != Format::kStruct || entries->n_children != 2 ||
      entries->children == nullptr) {
    return absl::InvalidArgumentError("Arrow map entries are not a key/value struct");
  }
  absl::StatusOr<Node> key = Build(entries->children[0]);
  absl::StatusOr<Node> value = Build(entries->children[1]);
  if (!key.ok()) return key.status();
  if (!value.ok()) return value.status();
  Node repeated;
  repeated.kind = NodeKind::kGroup;
  repeated.name = "key_value";
  repeated.repeated = true;
  repeated.converted = kMapKeyValue;
  repeated.children.push_back(std::move(*key));
  repeated.children.push_back(std::move(*value));
  Node node;
  node.kind = NodeKind::kMap;
  node.name = ArrowName(schema, "map");
  node.optional = Nullable(schema);
  node.converted = kMap;
  node.logical = Logical::kMap;
  node.children.push_back(std::move(repeated));
  return node;
}

absl::StatusOr<Node> BuildUnion(const struct ArrowSchema* schema) {
  if (schema->n_children <= 0 || schema->children == nullptr) {
    return absl::InvalidArgumentError("Arrow union is missing its arms");
  }
  Node type_id;
  type_id.kind = NodeKind::kLeaf;
  type_id.name = "type_id";
  type_id.physical = Physical::kInt32;
  type_id.logical = Logical::kInt32;
  type_id.converted = kInt32;
  Node node;
  node.kind = NodeKind::kUnion;
  node.name = ArrowName(schema, "union");
  node.optional = Nullable(schema);
  node.children.push_back(std::move(type_id));
  for (int64_t i = 0; i < schema->n_children; ++i) {
    const struct ArrowSchema* child = schema->children[i];
    if (Classify(child->format) == Format::kNull) {
      continue;
    }
    absl::StatusOr<Node> arm = Build(child);
    if (!arm.ok()) return arm.status();
    arm->optional = true;
    arm->arrow_type = static_cast<int>(i);
    node.children.push_back(std::move(*arm));
  }
  return node;
}

absl::StatusOr<Node> Build(const struct ArrowSchema* schema) {
  if (absl::Status status = CheckSchema(schema); !status.ok()) return status;
  switch (Classify(schema->format)) {
    case Format::kBool:
      return BuildLeaf(schema, Physical::kBool, Logical::kNone, false);
    case Format::kInt32:
      return BuildLeaf(schema, Physical::kInt32, Logical::kInt32, false);
    case Format::kUInt32:
      return BuildLeaf(schema, Physical::kInt32, Logical::kUint32, false);
    case Format::kInt64:
      return BuildLeaf(schema, Physical::kInt64, Logical::kInt64, false);
    case Format::kUInt64:
      return BuildLeaf(schema, Physical::kInt64, Logical::kUint64, false);
    case Format::kFloat:
      return BuildLeaf(schema, Physical::kFloat, Logical::kNone, false);
    case Format::kDouble:
      return BuildLeaf(schema, Physical::kDouble, Logical::kNone, false);
    case Format::kUtf8:
      return BuildLeaf(schema, Physical::kByteArray, Logical::kString, true);
    case Format::kBinary:
      return BuildLeaf(schema, Physical::kByteArray, Logical::kNone, false);
    case Format::kList:
    case Format::kFixed:
      return BuildList(schema);
    case Format::kMap:
      return BuildMap(schema);
    case Format::kUnion:
      return BuildUnion(schema);
    case Format::kStruct: {
      if (schema->n_children < 0 ||
          (schema->n_children > 0 && schema->children == nullptr)) {
        return absl::InvalidArgumentError("Arrow struct is missing its children");
      }
      Node node;
      node.kind = NodeKind::kGroup;
      node.name = ArrowName(schema, "schema");
      node.optional = Nullable(schema);
      node.children.reserve(static_cast<size_t>(schema->n_children));
      for (int64_t i = 0; i < schema->n_children; ++i) {
        absl::StatusOr<Node> child = Build(schema->children[i]);
        if (!child.ok()) return child.status();
        node.children.push_back(std::move(*child));
      }
      return node;
    }
    case Format::kNull:
      return absl::InvalidArgumentError("Arrow null columns cannot be written as Parquet");
    case Format::kOther:
      return absl::InvalidArgumentError(absl::StrFormat(
          "Arrow type '%s' cannot be written as Parquet", schema->format));
  }
  return absl::InternalError("unhandled Arrow type");
}

void AssignLevels(Node* node, int def, int rep) {
  if (node->optional) {
    ++def;
  }
  if (node->repeated) {
    ++def;
    ++rep;
  }
  node->max_def = def;
  node->max_rep = rep;
  for (Node& child : node->children) {
    AssignLevels(&child, def, rep);
  }
}

int NumberLeaves(Node* node, int next) {
  if (node->kind == NodeKind::kLeaf) {
    node->leaf_id = next;
    return next + 1;
  }
  for (Node& child : node->children) {
    next = NumberLeaves(&child, next);
  }
  return next;
}

void FillColumns(const Node& node, std::vector<std::string>* path,
                 std::vector<Column>* columns) {
  if (node.kind == NodeKind::kLeaf) {
    Column& column = (*columns)[static_cast<size_t>(node.leaf_id)];
    column.physical = node.physical;
    column.logical = node.logical;
    column.bytes_as_string = node.bytes_as_string;
    column.max_def = node.max_def;
    column.max_rep = node.max_rep;
    column.path = *path;
    return;
  }
  for (const Node& child : node.children) {
    path->push_back(child.name);
    FillColumns(child, path, columns);
    path->pop_back();
  }
}

void AddLevels(Column* column, int rep, int def) {
  column->reps.push_back(static_cast<uint8_t>(rep));
  column->defs.push_back(static_cast<uint8_t>(def));
}

void EmitAbsent(const Node& node, std::vector<Column>* columns, int rep, int def) {
  if (node.kind == NodeKind::kLeaf) {
    AddLevels(&(*columns)[static_cast<size_t>(node.leaf_id)], rep, def);
    return;
  }
  for (const Node& child : node.children) {
    EmitAbsent(child, columns, rep, def);
  }
}


void AppendRaw(std::string* out, const void* data, size_t size) {
  out->append(static_cast<const char*>(data), size);
}

absl::Status AppendValue(Column* column, const ::phaser::arrow::FieldReader& reader) {
  switch (column->physical) {
    case Physical::kBool: {
      absl::StatusOr<bool> value = reader.Bool();
      if (!value.ok()) return value.status();
      column->bools.push_back(*value ? 1 : 0);
      return absl::OkStatus();
    }
    case Physical::kInt32: {
      uint32_t bits = 0;
      if (column->logical == Logical::kUint32) {
        absl::StatusOr<uint32_t> value = reader.UInt32();
        if (!value.ok()) return value.status();
        bits = *value;
      } else {
        absl::StatusOr<int32_t> value = reader.Int32();
        if (!value.ok()) return value.status();
        bits = static_cast<uint32_t>(*value);
      }
      AppendLe32(&column->values, bits);
      return absl::OkStatus();
    }
    case Physical::kInt64: {
      uint64_t bits = 0;
      if (column->logical == Logical::kUint64) {
        absl::StatusOr<uint64_t> value = reader.UInt64();
        if (!value.ok()) return value.status();
        bits = *value;
      } else {
        absl::StatusOr<int64_t> value = reader.Int64();
        if (!value.ok()) return value.status();
        bits = static_cast<uint64_t>(*value);
      }
      AppendLe64(&column->values, bits);
      return absl::OkStatus();
    }
    case Physical::kFloat: {
      absl::StatusOr<float> value = reader.Float();
      if (!value.ok()) return value.status();
      static_assert(sizeof(float) == 4, "float is 4 bytes");
      AppendRaw(&column->values, &*value, sizeof(float));
      return absl::OkStatus();
    }
    case Physical::kDouble: {
      absl::StatusOr<double> value = reader.Double();
      if (!value.ok()) return value.status();
      static_assert(sizeof(double) == 8, "double is 8 bytes");
      AppendRaw(&column->values, &*value, sizeof(double));
      return absl::OkStatus();
    }
    case Physical::kByteArray: {
      absl::StatusOr<std::string_view> value =
          column->bytes_as_string ? reader.String() : reader.Binary();
      if (!value.ok()) return value.status();
      if (value->size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return absl::InvalidArgumentError("Parquet byte array is larger than int32");
      }
      AppendLe32(&column->values, static_cast<uint32_t>(value->size()));
      column->values.append(value->data(), value->size());
      return absl::OkStatus();
    }
  }
  return absl::InternalError("unhandled Parquet physical type");
}

absl::Status WriteNode(const Node& node, const ::phaser::arrow::FieldReader& reader,
                       int rep, int def, std::vector<Column>* columns);

absl::Status WriteLeaf(const Node& node, const ::phaser::arrow::FieldReader& reader,
                       int rep, int def, std::vector<Column>* columns) {
  Column* column = &(*columns)[static_cast<size_t>(node.leaf_id)];
  if (node.optional && reader.IsNull()) {
    AddLevels(column, rep, def);
    return absl::OkStatus();
  }
  AddLevels(column, rep, def + (node.optional ? 1 : 0));
  return AppendValue(column, reader);
}

absl::Status WriteNode(const Node& node, const ::phaser::arrow::FieldReader& reader,
                       int rep, int def, std::vector<Column>* columns) {
  if (absl::Status status = reader.status(); !status.ok()) return status;
  switch (node.kind) {
    case NodeKind::kLeaf:
      return WriteLeaf(node, reader, rep, def, columns);
    case NodeKind::kGroup: {
      if (node.optional && reader.IsNull()) {
        EmitAbsent(node, columns, rep, def);
        return absl::OkStatus();
      }
      const int child_def = def + (node.optional ? 1 : 0);
      for (size_t i = 0; i < node.children.size(); ++i) {
        if (absl::Status status = WriteNode(node.children[i],
                                            reader.Child(static_cast<int>(i)),
                                            rep, child_def, columns);
            !status.ok()) {
          return status;
        }
      }
      return absl::OkStatus();
    }
    case NodeKind::kList: {
      if (node.optional && reader.IsNull()) {
        EmitAbsent(node, columns, rep, def);
        return absl::OkStatus();
      }
      const int present = def + (node.optional ? 1 : 0);
      const Node& repeated = node.children[0];
      const int64_t count = reader.length();
      if (count == 0) {
        EmitAbsent(repeated, columns, rep, present);
        return absl::OkStatus();
      }
      for (int64_t i = 0; i < count; ++i) {
        const int elem_rep = i == 0 ? rep : repeated.max_rep;
        if (absl::Status status = WriteNode(repeated.children[0], reader.Element(i),
                                            elem_rep, present + 1, columns);
            !status.ok()) {
          return status;
        }
      }
      return absl::OkStatus();
    }
    case NodeKind::kMap: {
      if (node.optional && reader.IsNull()) {
        EmitAbsent(node, columns, rep, def);
        return absl::OkStatus();
      }
      const int present = def + (node.optional ? 1 : 0);
      const Node& repeated = node.children[0];
      const int64_t count = reader.length();
      if (count == 0) {
        EmitAbsent(repeated, columns, rep, present);
        return absl::OkStatus();
      }
      for (int64_t i = 0; i < count; ++i) {
        const int elem_rep = i == 0 ? rep : repeated.max_rep;
        if (absl::Status status = WriteNode(repeated, reader.Element(i), elem_rep,
                                            present + 1, columns);
            !status.ok()) {
          return status;
        }
      }
      return absl::OkStatus();
    }
    case NodeKind::kUnion: {
      if (node.optional && reader.IsNull()) {
        EmitAbsent(node, columns, rep, def);
        return absl::OkStatus();
      }
      const int present = def + (node.optional ? 1 : 0);
      const int type = reader.UnionType();
      if (type < 0) {
        return absl::InvalidArgumentError("Arrow union arm is out of range");
      }
      Column* type_id = &(*columns)[static_cast<size_t>(node.children[0].leaf_id)];
      AddLevels(type_id, rep, present);
      AppendLe32(&type_id->values, static_cast<uint32_t>(type));
      for (size_t arm = 1; arm < node.children.size(); ++arm) {
        const Node& child = node.children[arm];
        if (type == child.arrow_type) {
          if (absl::Status status =
                  WriteNode(child, reader.UnionValue(), rep, present, columns);
              !status.ok()) {
            return status;
          }
        } else {
          EmitAbsent(child, columns, rep, present);
        }
      }
      return absl::OkStatus();
    }
  }
  return absl::InternalError("unhandled Parquet node");
}

void WriteUVarint(std::string* out, uint64_t value) {
  while (value > 0x7f) {
    out->push_back(static_cast<char>((value & 0x7fU) | 0x80U));
    value >>= 7;
  }
  out->push_back(static_cast<char>(value));
}

void WriteZigZag(std::string* out, int64_t value) {
  const auto bits = static_cast<uint64_t>(value);
  WriteUVarint(out, (bits << 1) ^ static_cast<uint64_t>(value >> 63));
}

class Compact {
 public:
  explicit Compact(std::string* out) : out_(out) { last_ids_.push_back(0); }

  void I32(int id, int32_t value) {
    Header(id, kCtI32);
    WriteZigZag(out_, value);
  }
  void I64(int id, int64_t value) {
    Header(id, kCtI64);
    WriteZigZag(out_, value);
  }
  void Byte(int id, uint8_t value) {
    Header(id, kCtByte);
    out_->push_back(static_cast<char>(value));
  }
  void Bool(int id, bool value) { Header(id, value ? kCtBooleanTrue : kCtBooleanFalse); }
  void Binary(int id, std::string_view value) {
    Header(id, kCtBinary);
    WriteZigZag(out_, static_cast<int64_t>(value.size()));
    out_->append(value.data(), value.size());
  }
  void BeginStruct(int id) {
    Header(id, kCtStruct);
    last_ids_.push_back(0);
  }
  void BeginElement() { last_ids_.push_back(0); }
  void EndStruct() {
    out_->push_back(0);
    last_ids_.pop_back();
  }
  void BeginList(int id, int element, size_t count) {
    Header(id, kCtList);
    if (count == 0) {
      out_->push_back(0);
      return;
    }
    if (count <= 14) {
      out_->push_back(
          static_cast<char>((count << 4) | static_cast<unsigned>(element)));
      return;
    }
    out_->push_back(static_cast<char>(0xF0 | element));
    WriteUVarint(out_, count);
  }
  void RawI32(int32_t value) { WriteZigZag(out_, value); }
  void RawBinary(std::string_view value) {
    WriteZigZag(out_, static_cast<int64_t>(value.size()));
    out_->append(value.data(), value.size());
  }

 private:
  void Header(int id, int type) {
    int& last = last_ids_.back();
    const int delta = id - last;
    if (delta > 0 && delta <= 15) {
      out_->push_back(static_cast<char>((delta << 4) | type));
    } else {
      out_->push_back(static_cast<char>(type));
      WriteZigZag(out_, id);
    }
    last = id;
  }

  std::string* out_;
  std::vector<int> last_ids_;
};

int BitWidth(int max_level) {
  int width = 0;
  while (max_level > 0) {
    ++width;
    max_level >>= 1;
  }
  return width;
}

void EncodeLevels(const std::vector<uint8_t>& levels, int bit_width, std::string* out) {
  if (bit_width <= 0) return;
  std::string body;
  const int nbytes = (bit_width + 7) / 8;
  size_t index = 0;
  while (index < levels.size()) {
    size_t end = index + 1;
    while (end < levels.size() && levels[end] == levels[index]) ++end;
    WriteUVarint(&body, static_cast<uint64_t>(end - index) << 1);
    uint32_t value = levels[index];
    for (int byte = 0; byte < nbytes; ++byte) {
      body.push_back(static_cast<char>((value >> (8 * byte)) & 0xffU));
    }
    index = end;
  }
  AppendLe32(out, static_cast<uint32_t>(body.size()));
  out->append(body);
}

void PackBools(const std::vector<uint8_t>& bools, std::string* out) {
  uint8_t byte = 0;
  int used = 0;
  for (uint8_t value : bools) {
    if (value != 0) byte = static_cast<uint8_t>(byte | static_cast<uint8_t>(1U << used));
    ++used;
    if (used == 8) {
      out->push_back(static_cast<char>(byte));
      byte = 0;
      used = 0;
    }
  }
  if (used != 0) out->push_back(static_cast<char>(byte));
}

int PhysicalType(Physical physical) {
  switch (physical) {
    case Physical::kBool:
      return kTypeBoolean;
    case Physical::kInt32:
      return kTypeInt32;
    case Physical::kInt64:
      return kTypeInt64;
    case Physical::kFloat:
      return kTypeFloat;
    case Physical::kDouble:
      return kTypeDouble;
    case Physical::kByteArray:
      return kTypeByteArray;
  }
  return kTypeBoolean;
}

void WriteLogical(Compact* compact, Logical logical) {
  switch (logical) {
    case Logical::kNone:
      return;
    case Logical::kString:
      compact->BeginStruct(10);
      compact->BeginStruct(1);
      compact->EndStruct();
      compact->EndStruct();
      return;
    case Logical::kMap:
      compact->BeginStruct(10);
      compact->BeginStruct(2);
      compact->EndStruct();
      compact->EndStruct();
      return;
    case Logical::kList:
      compact->BeginStruct(10);
      compact->BeginStruct(3);
      compact->EndStruct();
      compact->EndStruct();
      return;
    case Logical::kInt32:
    case Logical::kUint32:
    case Logical::kInt64:
    case Logical::kUint64: {
      const bool wide = logical == Logical::kInt64 || logical == Logical::kUint64;
      const bool is_signed = logical == Logical::kInt32 || logical == Logical::kInt64;
      compact->BeginStruct(10);
      compact->BeginStruct(10);
      compact->Byte(1, static_cast<uint8_t>(wide ? 64 : 32));
      compact->Bool(2, is_signed);
      compact->EndStruct();
      compact->EndStruct();
      return;
    }
  }
}

int CountSchema(const Node& node) {
  int count = 1;
  for (const Node& child : node.children) count += CountSchema(child);
  return count;
}

void WriteSchemaElement(Compact* compact, const Node& node, bool root) {
  compact->BeginElement();
  if (node.kind == NodeKind::kLeaf) compact->I32(1, PhysicalType(node.physical));
  const int repetition =
      root ? kRequired : (node.repeated ? kRepeated : (node.optional ? kOptional : kRequired));
  compact->I32(3, repetition);
  compact->Binary(4, node.name);
  if (node.kind != NodeKind::kLeaf) {
    compact->I32(5, static_cast<int32_t>(node.children.size()));
  }
  if (node.converted >= 0) compact->I32(6, node.converted);
  WriteLogical(compact, node.logical);
  compact->EndStruct();
  for (const Node& child : node.children) WriteSchemaElement(compact, child, false);
}

void CollectLeaves(const Node& node, std::vector<const Node*>* leaves) {
  if (node.kind == NodeKind::kLeaf) {
    leaves->push_back(&node);
    return;
  }
  for (const Node& child : node.children) CollectLeaves(child, leaves);
}

absl::Status WritePage(const Column& column, std::string* out) {
  std::string body;
  EncodeLevels(column.reps, BitWidth(column.max_rep), &body);
  EncodeLevels(column.defs, BitWidth(column.max_def), &body);
  if (column.physical == Physical::kBool) {
    PackBools(column.bools, &body);
  } else {
    body.append(column.values);
  }
  if (body.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    return absl::InvalidArgumentError("Parquet data page is larger than int32");
  }
  const auto page_size = static_cast<int32_t>(body.size());
  std::string header;
  Compact compact(&header);
  compact.I32(1, 0);
  compact.I32(2, page_size);
  compact.I32(3, page_size);
  compact.BeginStruct(4);
  compact.I32(1, static_cast<int32_t>(column.value_count));
  compact.I32(2, kPlain);
  compact.I32(3, kRle);
  compact.I32(4, kRle);
  compact.EndStruct();
  compact.EndStruct();
  out->append(header);
  out->append(body);
  return absl::OkStatus();
}

struct Chunk {
  size_t offset = 0;
  size_t size = 0;
  const Column* column = nullptr;
};

absl::Status WriteImpl(const struct ArrowSchema* schema, const struct ArrowArray* array,
                       std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Parquet output string is null");
  }
  if (schema == nullptr || array == nullptr) {
    return absl::InvalidArgumentError("Arrow batch is missing");
  }
  if (Classify(schema->format) != Format::kStruct) {
    return absl::InvalidArgumentError("Parquet file root must be a struct");
  }
  absl::StatusOr<Node> root_or = Build(schema);
  if (!root_or.ok()) return root_or.status();
  Node root = std::move(*root_or);
  root.optional = false;
  root.name = ArrowName(schema, "schema");
  AssignLevels(&root, 0, 0);
  const int leaf_count = NumberLeaves(&root, 0);
  std::vector<Column> columns(static_cast<size_t>(leaf_count));
  std::vector<std::string> path;
  FillColumns(root, &path, &columns);
  absl::StatusOr<::phaser::arrow::BatchReader> rows =
      ::phaser::arrow::BatchReader::Open(schema, array);
  if (!rows.ok()) return rows.status();
  for (int64_t row = 0; row < rows->length(); ++row) {
    if (absl::Status status = WriteNode(root, rows->Row(row), 0, 0, &columns);
        !status.ok()) {
      return status;
    }
  }
  for (Column& column : columns) {
    if (column.max_def > 255 || column.max_rep > 255) {
      return absl::InvalidArgumentError("Parquet nesting is deeper than 255 levels");
    }
    column.value_count = static_cast<int64_t>(column.defs.size());
    if (column.max_def == 0) column.defs.clear();
    if (column.max_rep == 0) column.reps.clear();
  }

  std::string file("PAR1", 4);
  std::vector<const Node*> leaf_nodes;
  CollectLeaves(root, &leaf_nodes);
  std::vector<Chunk> chunks(static_cast<size_t>(leaf_count));
  for (const Node* leaf : leaf_nodes) {
    Chunk& chunk = chunks[static_cast<size_t>(leaf->leaf_id)];
    chunk.column = &columns[static_cast<size_t>(leaf->leaf_id)];
    chunk.offset = file.size();
    if (absl::Status status = WritePage(*chunk.column, &file); !status.ok()) return status;
    chunk.size = file.size() - chunk.offset;
  }

  const size_t footer_at = file.size();
  Compact footer(&file);
  footer.I32(1, 1);
  footer.BeginList(2, kCtStruct, static_cast<size_t>(CountSchema(root)));
  WriteSchemaElement(&footer, root, true);
  footer.I64(3, rows->length());
  footer.BeginList(4, kCtStruct, 1);
  footer.BeginElement();
  footer.BeginList(1, kCtStruct, chunks.size());
  int64_t total = 0;
  for (const Chunk& chunk : chunks) {
    footer.BeginElement();
    footer.I64(2, static_cast<int64_t>(chunk.offset));
    footer.BeginStruct(3);
    footer.I32(1, PhysicalType(chunk.column->physical));
    const bool levels = chunk.column->max_def > 0 || chunk.column->max_rep > 0;
    footer.BeginList(2, kCtI32, levels ? 2 : 1);
    footer.RawI32(kPlain);
    if (levels) footer.RawI32(kRle);
    footer.BeginList(3, kCtBinary, chunk.column->path.size());
    for (const std::string& part : chunk.column->path) footer.RawBinary(part);
    footer.I32(4, 0);
    footer.I64(5, chunk.column->value_count);
    footer.I64(6, static_cast<int64_t>(chunk.size));
    footer.I64(7, static_cast<int64_t>(chunk.size));
    footer.I64(9, static_cast<int64_t>(chunk.offset));
    footer.EndStruct();
    footer.EndStruct();
    total += static_cast<int64_t>(chunk.size);
  }
  footer.I64(2, total);
  footer.I64(3, rows->length());
  footer.EndStruct();
  footer.Binary(6, "phaser");
  footer.EndStruct();
  AppendLe32(&file, static_cast<uint32_t>(file.size() - footer_at));
  file.append("PAR1", 4);
  *out = std::move(file);
  return absl::OkStatus();
}

}  // namespace

absl::Status WriteFile(const struct ArrowSchema* schema, const struct ArrowArray* array,
                       std::string* out) {
  return WriteImpl(schema, array, out);
}

}  // namespace parquet
}  // namespace phaser
