// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/avro.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "phaser/runtime/arrow_abi.h"

namespace phaser {
namespace avro {
namespace {

constexpr char kSync[] = "phaser.avro.sync";

void AppendByte(std::string* out, unsigned char byte) {
  out->push_back(static_cast<char>(byte));
}

void WriteUVarint(std::string* out, uint64_t value) {
  while (value > 0x7f) {
    AppendByte(out, static_cast<unsigned char>((value & 0x7fU) | 0x80U));
    value >>= 7;
  }
  AppendByte(out, static_cast<unsigned char>(value));
}

void WriteLong(std::string* out, int64_t value) {
  const auto bits = static_cast<uint64_t>(value);
  WriteUVarint(out, (bits << 1) ^ static_cast<uint64_t>(value >> 63));
}

void AppendRaw(std::string* out, const void* data, size_t size) {
  const auto* bytes = static_cast<const char*>(data);
  out->append(bytes, size);
}

void JsonEscape(std::string* out, std::string_view text) {
  out->push_back('"');
  for (unsigned char c : text) {
    switch (c) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        if (c < 0x20) {
          out->append(absl::StrFormat("\\u%04x", c));
        } else {
          AppendByte(out, c);
        }
        break;
    }
  }
  out->push_back('"');
}

enum class Kind {
  kNull,
  kBool,
  kInt,
  kLong,
  kFloat,
  kDouble,
  kString,
  kBytes,
  kFixed,
  kRecord,
  kArray,
  kMap,
  kUnion,
};

struct Type {
  Kind kind = Kind::kNull;
  bool dense = false;
  bool uint32 = false;
  int fixed_size = 0;
  std::string name;
  std::string name_space;
  std::vector<std::string> field_names;
  std::vector<int> children;
};

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

std::string_view ArrowName(const struct ArrowSchema* schema) {
  if (schema == nullptr || schema->name == nullptr) {
    return {};
  }
  return schema->name;
}

std::string Sanitize(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('_');
    }
  }
  if (out.empty() ||
      !((out[0] >= 'A' && out[0] <= 'Z') || (out[0] >= 'a' && out[0] <= 'z') ||
        out[0] == '_')) {
    out.insert(out.begin(), '_');
  }
  return out;
}

void SplitName(std::string_view full, std::string* name_space, std::string* name) {
  const size_t dot = full.rfind('.');
  if (dot == std::string_view::npos) {
    name_space->clear();
    *name = Sanitize(full);
    return;
  }
  *name_space = std::string(full.substr(0, dot));
  *name = Sanitize(full.substr(dot + 1));
}

struct Schema {
  std::vector<Type> types;
  int fixed_ids = 0;

  int Add(Type type) {
    types.push_back(std::move(type));
    return static_cast<int>(types.size()) - 1;
  }

  absl::Status Check(const struct ArrowSchema* schema) const {
    if (schema == nullptr || schema->format == nullptr) {
      return absl::InvalidArgumentError("Arrow schema is missing");
    }
    if (schema->dictionary != nullptr) {
      return absl::InvalidArgumentError("Arrow dictionaries are not supported");
    }
    return absl::OkStatus();
  }

  absl::StatusOr<int> Build(const struct ArrowSchema* schema,
                            std::string_view parent_ns) {
    if (absl::Status status = Check(schema); !status.ok()) {
      return status;
    }
    const Format format = Classify(schema->format);
    if (Nullable(schema) && format != Format::kUnion && format != Format::kNull) {
      absl::StatusOr<int> inner = BuildBody(schema, parent_ns);
      if (!inner.ok()) {
        return inner.status();
      }
      Type wrapped;
      wrapped.kind = Kind::kUnion;
      wrapped.children.push_back(Add(Type{Kind::kNull}));
      wrapped.children.push_back(*inner);
      return Add(std::move(wrapped));
    }
    return BuildBody(schema, parent_ns);
  }

  absl::StatusOr<int> BuildBody(const struct ArrowSchema* schema,
                                std::string_view parent_ns) {
    const Format format = Classify(schema->format);
    Type type;
    switch (format) {
      case Format::kNull:
        type.kind = Kind::kNull;
        return Add(std::move(type));
      case Format::kBool:
        type.kind = Kind::kBool;
        return Add(std::move(type));
      case Format::kInt32:
        type.kind = Kind::kInt;
        return Add(std::move(type));
      case Format::kUInt32:
        type.kind = Kind::kLong;
        type.uint32 = true;
        return Add(std::move(type));
      case Format::kInt64:
        type.kind = Kind::kLong;
        return Add(std::move(type));
      case Format::kUInt64:
        type.kind = Kind::kFixed;
        type.fixed_size = 8;
        type.name = absl::StrCat("uint64_", ++fixed_ids);
        return Add(std::move(type));
      case Format::kFloat:
        type.kind = Kind::kFloat;
        return Add(std::move(type));
      case Format::kDouble:
        type.kind = Kind::kDouble;
        return Add(std::move(type));
      case Format::kUtf8:
        type.kind = Kind::kString;
        return Add(std::move(type));
      case Format::kBinary:
        type.kind = Kind::kBytes;
        return Add(std::move(type));
      case Format::kStruct:
        return BuildRecord(schema, parent_ns);
      case Format::kList:
      case Format::kFixed:
        return BuildArray(schema, parent_ns);
      case Format::kMap:
        return BuildMap(schema, parent_ns);
      case Format::kUnion:
        return BuildUnion(schema, parent_ns);
      case Format::kOther:
        return absl::InvalidArgumentError(absl::StrFormat(
            "Arrow type '%s' cannot be written as Avro", schema->format));
    }
    return absl::InternalError("unhandled Arrow type");
  }

  absl::StatusOr<int> BuildRecord(const struct ArrowSchema* schema,
                                  std::string_view parent_ns) {
    if (schema->n_children < 0 ||
        (schema->n_children > 0 && schema->children == nullptr)) {
      return absl::InvalidArgumentError("Arrow struct is missing its children");
    }
    Type type;
    type.kind = Kind::kRecord;
    const std::string_view raw = ArrowName(schema);
    if (raw.find('.') != std::string_view::npos) {
      SplitName(raw, &type.name_space, &type.name);
    } else {
      type.name_space = std::string(parent_ns);
      type.name = Sanitize(raw.empty() ? std::string_view("record") : raw);
    }
    const std::string child_ns = type.name_space.empty()
                                     ? type.name
                                     : type.name_space + "." + type.name;
    type.field_names.reserve(static_cast<size_t>(schema->n_children));
    type.children.reserve(static_cast<size_t>(schema->n_children));
    for (int64_t i = 0; i < schema->n_children; ++i) {
      const struct ArrowSchema* child = schema->children[i];
      type.field_names.emplace_back(ArrowName(child));
      absl::StatusOr<int> built = Build(child, child_ns);
      if (!built.ok()) {
        return built.status();
      }
      type.children.push_back(*built);
    }
    return Add(std::move(type));
  }

  absl::StatusOr<int> BuildArray(const struct ArrowSchema* schema,
                                 std::string_view parent_ns) {
    if (schema->n_children != 1 || schema->children == nullptr ||
        schema->children[0] == nullptr) {
      return absl::InvalidArgumentError("Arrow list is missing its item");
    }
    absl::StatusOr<int> item = Build(schema->children[0], parent_ns);
    if (!item.ok()) {
      return item.status();
    }
    Type type;
    type.kind = Kind::kArray;
    type.children.push_back(*item);
    return Add(std::move(type));
  }

  absl::StatusOr<int> BuildMap(const struct ArrowSchema* schema,
                               std::string_view parent_ns) {
    if (schema->n_children != 1 || schema->children == nullptr ||
        schema->children[0] == nullptr) {
      return absl::InvalidArgumentError("Arrow map is missing its entries");
    }
    const struct ArrowSchema* entries = schema->children[0];
    if (Classify(entries->format) != Format::kStruct ||
        entries->n_children != 2 || entries->children == nullptr) {
      return absl::InvalidArgumentError("Arrow map entries are not a key/value struct");
    }
    const struct ArrowSchema* key = entries->children[0];
    const struct ArrowSchema* value = entries->children[1];
    const std::string_view map_name = ArrowName(schema);
    const std::string map_ns =
        parent_ns.empty()
            ? Sanitize(map_name.empty() ? std::string_view("map") : map_name)
            : std::string(parent_ns) + "." +
                  Sanitize(map_name.empty() ? std::string_view("map") : map_name);
    if (Classify(key->format) == Format::kUtf8 && !Nullable(key)) {
      absl::StatusOr<int> built = Build(value, map_ns);
      if (!built.ok()) {
        return built.status();
      }
      Type type;
      type.kind = Kind::kMap;
      type.children.push_back(*built);
      return Add(std::move(type));
    }
    return MakeArrayOf(entries, map_ns);
  }

  absl::StatusOr<int> MakeArrayOf(const struct ArrowSchema* entries,
                                  std::string_view parent_ns) {
    absl::StatusOr<int> record = BuildRecord(entries, parent_ns);
    if (!record.ok()) {
      return record.status();
    }
    Type type;
    type.kind = Kind::kArray;
    type.children.push_back(*record);
    return Add(std::move(type));
  }

  absl::StatusOr<int> BuildUnion(const struct ArrowSchema* schema,
                                 std::string_view parent_ns) {
    if (schema->n_children <= 0 || schema->children == nullptr) {
      return absl::InvalidArgumentError("Arrow union is missing its arms");
    }
    Type type;
    type.kind = Kind::kUnion;
    type.dense = true;
    for (int64_t i = 0; i < schema->n_children; ++i) {
      absl::StatusOr<int> arm = Build(schema->children[i], parent_ns);
      if (!arm.ok()) {
        return arm.status();
      }
      type.children.push_back(*arm);
    }
    return Add(std::move(type));
  }

  void WriteSchema(int index, std::string* out) const {
    const Type& type = types[static_cast<size_t>(index)];
    switch (type.kind) {
      case Kind::kNull:
        out->append("\"null\"");
        break;
      case Kind::kBool:
        out->append("\"boolean\"");
        break;
      case Kind::kInt:
        out->append("\"int\"");
        break;
      case Kind::kLong:
        out->append("\"long\"");
        break;
      case Kind::kFloat:
        out->append("\"float\"");
        break;
      case Kind::kDouble:
        out->append("\"double\"");
        break;
      case Kind::kString:
        out->append("\"string\"");
        break;
      case Kind::kBytes:
        out->append("\"bytes\"");
        break;
      case Kind::kFixed:
        out->append("{\"type\":\"fixed\",\"name\":");
        JsonEscape(out, type.name);
        out->append(",\"size\":");
        out->append(absl::StrCat(type.fixed_size));
        out->push_back('}');
        break;
      case Kind::kRecord:
        out->append("{\"type\":\"record\",\"name\":");
        JsonEscape(out, type.name);
        if (!type.name_space.empty()) {
          out->append(",\"namespace\":");
          JsonEscape(out, type.name_space);
        }
        out->append(",\"fields\":[");
        for (size_t i = 0; i < type.children.size(); ++i) {
          if (i != 0) {
            out->push_back(',');
          }
          out->append("{\"name\":");
          JsonEscape(out, type.field_names[i]);
          out->append(",\"type\":");
          WriteSchema(type.children[i], out);
          if (types[static_cast<size_t>(type.children[i])].kind == Kind::kUnion &&
              !types[static_cast<size_t>(type.children[i])].dense) {
            out->append(",\"default\":null");
          }
          out->push_back('}');
        }
        out->append("]}");
        break;
      case Kind::kArray:
        out->append("{\"type\":\"array\",\"items\":");
        WriteSchema(type.children[0], out);
        out->push_back('}');
        break;
      case Kind::kMap:
        out->append("{\"type\":\"map\",\"values\":");
        WriteSchema(type.children[0], out);
        out->push_back('}');
        break;
      case Kind::kUnion:
        out->push_back('[');
        for (size_t i = 0; i < type.children.size(); ++i) {
          if (i != 0) {
            out->push_back(',');
          }
          WriteSchema(type.children[i], out);
        }
        out->push_back(']');
        break;
    }
  }
};

absl::Status WriteValue(const Schema& schema, int index,
                        const ::phaser::arrow::FieldReader& reader,
                        std::string* out);

absl::Status WriteUnion(const Schema& schema, const Type& type,
                        const ::phaser::arrow::FieldReader& reader,
                        std::string* out) {
  int arm = 0;
  if (type.dense) {
    if (reader.IsNull()) {
      arm = 0;
    } else {
      arm = reader.UnionType();
      if (arm < 0 || static_cast<size_t>(arm) >= type.children.size()) {
        return absl::InvalidArgumentError("Arrow union arm is out of range");
      }
    }
  } else if (!reader.IsNull()) {
    arm = 1;
  }
  WriteLong(out, arm);
  const Type& selected = schema.types[static_cast<size_t>(type.children[static_cast<size_t>(arm)])];
  if (selected.kind == Kind::kNull) {
    return absl::OkStatus();
  }
  const ::phaser::arrow::FieldReader value =
      type.dense ? reader.UnionValue() : reader;
  return WriteValue(schema, type.children[static_cast<size_t>(arm)], value, out);
}

absl::Status WriteRecord(const Schema& schema, const Type& type,
                         const ::phaser::arrow::FieldReader& reader,
                         std::string* out) {
  if (reader.IsNull()) {
    return absl::InvalidArgumentError("Avro record value is null");
  }
  for (size_t i = 0; i < type.children.size(); ++i) {
    if (absl::Status status = WriteValue(schema, type.children[i],
                                         reader.Child(static_cast<int>(i)), out);
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status WriteArray(const Schema& schema, const Type& type,
                        const ::phaser::arrow::FieldReader& reader,
                        std::string* out) {
  const int64_t count = reader.length();
  if (count == 0) {
    WriteLong(out, 0);
    return absl::OkStatus();
  }
  WriteLong(out, count);
  for (int64_t i = 0; i < count; ++i) {
    if (absl::Status status =
            WriteValue(schema, type.children[0], reader.Element(i), out);
        !status.ok()) {
      return status;
    }
  }
  WriteLong(out, 0);
  return absl::OkStatus();
}

absl::Status WriteMap(const Schema& schema, const Type& type,
                      const ::phaser::arrow::FieldReader& reader,
                      std::string* out) {
  const int64_t count = reader.length();
  if (count == 0) {
    WriteLong(out, 0);
    return absl::OkStatus();
  }
  WriteLong(out, count);
  for (int64_t i = 0; i < count; ++i) {
    const ::phaser::arrow::FieldReader entry = reader.Element(i);
    absl::StatusOr<std::string_view> key = entry.Child(0).String();
    if (!key.ok()) {
      return key.status();
    }
    WriteLong(out, static_cast<int64_t>(key->size()));
    out->append(key->data(), key->size());
    if (absl::Status status =
            WriteValue(schema, type.children[0], entry.Child(1), out);
        !status.ok()) {
      return status;
    }
  }
  WriteLong(out, 0);
  return absl::OkStatus();
}

absl::Status WriteValue(const Schema& schema, int index,
                        const ::phaser::arrow::FieldReader& reader,
                        std::string* out) {
  if (absl::Status status = reader.status(); !status.ok()) {
    return status;
  }
  const Type& type = schema.types[static_cast<size_t>(index)];
  switch (type.kind) {
    case Kind::kNull:
      return absl::OkStatus();
    case Kind::kBool: {
      absl::StatusOr<bool> value = reader.Bool();
      if (!value.ok()) return value.status();
      AppendByte(out, *value ? 1 : 0);
      return absl::OkStatus();
    }
    case Kind::kInt: {
      absl::StatusOr<int32_t> value = reader.Int32();
      if (!value.ok()) return value.status();
      WriteLong(out, *value);
      return absl::OkStatus();
    }
    case Kind::kLong: {
      if (type.uint32) {
        absl::StatusOr<uint32_t> value = reader.UInt32();
        if (!value.ok()) return value.status();
        WriteLong(out, static_cast<int64_t>(*value));
      } else {
        absl::StatusOr<int64_t> value = reader.Int64();
        if (!value.ok()) return value.status();
        WriteLong(out, *value);
      }
      return absl::OkStatus();
    }
    case Kind::kFloat: {
      absl::StatusOr<float> value = reader.Float();
      if (!value.ok()) return value.status();
      AppendRaw(out, &*value, sizeof(float));
      return absl::OkStatus();
    }
    case Kind::kDouble: {
      absl::StatusOr<double> value = reader.Double();
      if (!value.ok()) return value.status();
      AppendRaw(out, &*value, sizeof(double));
      return absl::OkStatus();
    }
    case Kind::kString:
    case Kind::kBytes: {
      absl::StatusOr<std::string_view> value =
          type.kind == Kind::kString ? reader.String() : reader.Binary();
      if (!value.ok()) return value.status();
      WriteLong(out, static_cast<int64_t>(value->size()));
      out->append(value->data(), value->size());
      return absl::OkStatus();
    }
    case Kind::kFixed: {
      absl::StatusOr<uint64_t> value = reader.UInt64();
      if (!value.ok()) return value.status();
      uint64_t bits = *value;
      for (int shift = 0; shift < 64; shift += 8) {
        AppendByte(out, static_cast<unsigned char>((bits >> shift) & 0xffU));
      }
      return absl::OkStatus();
    }
    case Kind::kRecord:
      return WriteRecord(schema, type, reader, out);
    case Kind::kArray:
      return WriteArray(schema, type, reader, out);
    case Kind::kMap:
      return WriteMap(schema, type, reader, out);
    case Kind::kUnion:
      return WriteUnion(schema, type, reader, out);
  }
  return absl::InternalError("unhandled Avro type");
}

}  // namespace

absl::Status WriteRecordBytes(const struct ArrowSchema* schema,
                              const struct ArrowArray* array, int64_t row,
                              std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Avro output string is null");
  }
  if (schema == nullptr || array == nullptr) {
    return absl::InvalidArgumentError("Arrow batch is missing");
  }
  Schema built;
  absl::StatusOr<int> root = built.Build(schema, "");
  if (!root.ok()) {
    return root.status();
  }
  if (built.types[static_cast<size_t>(*root)].kind != Kind::kRecord) {
    return absl::InvalidArgumentError("Avro record root must be a record");
  }
  absl::StatusOr<::phaser::arrow::BatchReader> rows =
      ::phaser::arrow::BatchReader::Open(schema, array);
  if (!rows.ok()) {
    return rows.status();
  }
  if (row < 0 || row >= rows->length()) {
    return absl::OutOfRangeError(
        absl::StrFormat("Avro row index %d out of range [0, %d)", row,
                        rows->length()));
  }
  out->clear();
  return WriteValue(built, *root, rows->Row(row), out);
}

absl::Status WriteAllRecordBytes(const struct ArrowSchema* schema,
                                 const struct ArrowArray* array,
                                 std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Avro output string is null");
  }
  if (schema == nullptr || array == nullptr) {
    return absl::InvalidArgumentError("Arrow batch is missing");
  }
  Schema built;
  absl::StatusOr<int> root = built.Build(schema, "");
  if (!root.ok()) {
    return root.status();
  }
  if (built.types[static_cast<size_t>(*root)].kind != Kind::kRecord) {
    return absl::InvalidArgumentError("Avro record root must be a record");
  }
  absl::StatusOr<::phaser::arrow::BatchReader> rows =
      ::phaser::arrow::BatchReader::Open(schema, array);
  if (!rows.ok()) {
    return rows.status();
  }
  out->clear();
  for (int64_t row = 0; row < rows->length(); ++row) {
    if (absl::Status status =
            WriteValue(built, *root, rows->Row(row), out);
        !status.ok()) {
      out->clear();
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status WriteFile(const struct ArrowSchema* schema,
                       const struct ArrowArray* array, std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Avro output string is null");
  }
  if (schema == nullptr || array == nullptr) {
    return absl::InvalidArgumentError("Arrow batch is missing");
  }
  Schema built;
  absl::StatusOr<int> root = built.Build(schema, "");
  if (!root.ok()) {
    return root.status();
  }
  if (built.types[static_cast<size_t>(*root)].kind != Kind::kRecord) {
    return absl::InvalidArgumentError("Avro file root must be a record");
  }
  std::string json;
  built.WriteSchema(*root, &json);
  absl::StatusOr<::phaser::arrow::BatchReader> rows =
      ::phaser::arrow::BatchReader::Open(schema, array);
  if (!rows.ok()) {
    return rows.status();
  }
  std::string body;
  if (absl::Status status = WriteAllRecordBytes(schema, array, &body);
      !status.ok()) {
    return status;
  }
  out->clear();
  out->append("Obj\x01", 4);
  WriteLong(out, 2);
  WriteLong(out, static_cast<int64_t>(std::char_traits<char>::length("avro.schema")));
  out->append("avro.schema");
  WriteLong(out, static_cast<int64_t>(json.size()));
  out->append(json);
  WriteLong(out, static_cast<int64_t>(std::char_traits<char>::length("avro.codec")));
  out->append("avro.codec");
  WriteLong(out, 4);
  out->append("null");
  WriteLong(out, 0);
  out->append(kSync, sizeof(kSync) - 1);
  if (rows->length() > 0) {
    WriteLong(out, rows->length());
    WriteLong(out, static_cast<int64_t>(body.size()));
    out->append(body);
    out->append(kSync, sizeof(kSync) - 1);
  }
  return absl::OkStatus();
}

}  // namespace avro
}  // namespace phaser
