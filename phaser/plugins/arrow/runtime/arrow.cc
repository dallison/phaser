// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/plugins/arrow/arrow.h"

#include <cstdlib>
#include <cstring>
#include <limits>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "phaser/runtime/any.h"
#include "phaser/runtime/phaser_bank.h"

namespace phaser {
namespace arrow {

enum class Kind {
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
  kFixed,
  kMap,
  kUnion,
};

struct SchemaNode {
  Kind kind = Kind::kNull;
  bool nullable = false;
  int32_t fixed = 0;
  std::string format;
  std::string name;
  std::vector<std::unique_ptr<SchemaNode>> children;
};

void ReleaseSchema(struct ArrowSchema* schema);
void ReleaseArray(struct ArrowArray* array);

void* AllocateAligned(size_t size) {
  if (size == 0) {
    size = 64;
  }
  size = (size + 63u) & ~size_t{63};
  void* memory = nullptr;
  if (posix_memalign(&memory, 64, size) != 0) {
    return nullptr;
  }
  memset(memory, 0, size);
  return memory;
}

void PushBit(std::vector<uint8_t>* bytes, int64_t index, bool set) {
  const size_t byte = static_cast<size_t>(index) / 8u;
  if (bytes->size() <= byte) {
    bytes->resize(byte + 1, 0);
  }
  if (set) {
    (*bytes)[byte] |= static_cast<uint8_t>(1u << (static_cast<unsigned>(index) % 8u));
  }
}

bool BitSet(const void* bytes, int64_t index) {
  if (bytes == nullptr) {
    return true;
  }
  const auto* data = static_cast<const uint8_t*>(bytes);
  const size_t byte = static_cast<size_t>(index) / 8u;
  return (data[byte] & static_cast<uint8_t>(1u << (static_cast<unsigned>(index) % 8u))) != 0;
}

absl::Status FormatError(std::string_view name, std::string_view expected,
                         std::string_view actual) {
  return absl::InvalidArgumentError(absl::StrFormat(
      "Arrow field '%s' has format '%s', expected '%s'", name, actual,
      expected));
}

struct ArrowNode {
  Kind kind = Kind::kNull;
  bool nullable = false;
  int32_t fixed = 0;
  std::string name;
  std::vector<std::unique_ptr<ArrowNode>> children;
  int64_t length = 0;
  int64_t null_count = 0;
  std::vector<uint8_t> validity;
  std::vector<uint8_t> values;
  std::vector<int32_t> offsets{0};
  std::vector<int8_t> type_ids;
  std::vector<int32_t> union_offsets;
  absl::Status error;
};

void Mark(ArrowNode* node, bool present) {
  if (!present) {
    ++node->null_count;
  }
  if (node->nullable) {
    PushBit(&node->validity, node->length, present);
  }
}

absl::Status Fail(ArrowNode* node, absl::Status status) {
  if (node != nullptr && node->error.ok()) {
    node->error = status;
  }
  return status;
}

absl::Status Require(ArrowNode* node, Kind kind) {
  if (node == nullptr) {
    return absl::InvalidArgumentError("Arrow writer has no column");
  }
  if (!node->error.ok()) {
    return node->error;
  }
  if (node->kind != kind) {
    return Fail(node, absl::InvalidArgumentError(absl::StrFormat(
                          "Arrow field '%s' was written as the wrong type",
                          node->name)));
  }
  return absl::OkStatus();
}

template <typename T>
absl::Status AppendRaw(ArrowNode* node, Kind kind, T value) {
  if (absl::Status status = Require(node, kind); !status.ok()) {
    return status;
  }
  Mark(node, true);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  node->values.insert(node->values.end(), bytes, bytes + sizeof(T));
  ++node->length;
  return absl::OkStatus();
}

absl::Status AppendBytes(ArrowNode* node, Kind kind, std::string_view value) {
  if (absl::Status status = Require(node, kind); !status.ok()) {
    return status;
  }
  const int32_t end = node->offsets.back();
  if (value.size() >
      static_cast<size_t>(std::numeric_limits<int32_t>::max() - end)) {
    return Fail(node, absl::InvalidArgumentError(
                          "Arrow string or binary column exceeds int32 offsets"));
  }
  Mark(node, true);
  node->values.insert(node->values.end(), value.begin(), value.end());
  node->offsets.push_back(end + static_cast<int32_t>(value.size()));
  ++node->length;
  return absl::OkStatus();
}

absl::Status AppendNull(ArrowNode* node) {
  if (node == nullptr) {
    return absl::InvalidArgumentError("Arrow writer has no column");
  }
  if (!node->error.ok()) {
    return node->error;
  }
  if (!node->nullable) {
    return Fail(node, absl::InvalidArgumentError(absl::StrFormat(
                          "Arrow field '%s' is not nullable", node->name)));
  }
  Mark(node, false);
  switch (node->kind) {
    case Kind::kBool:
      PushBit(&node->values, node->length, false);
      break;
    case Kind::kInt32:
    case Kind::kUInt32:
    case Kind::kFloat:
      node->values.insert(node->values.end(), sizeof(int32_t), 0);
      break;
    case Kind::kInt64:
    case Kind::kUInt64:
    case Kind::kDouble:
      node->values.insert(node->values.end(), sizeof(int64_t), 0);
      break;
    case Kind::kUtf8:
    case Kind::kBinary:
      node->offsets.push_back(node->offsets.back());
      break;
    default:
      return Fail(node, absl::InvalidArgumentError(absl::StrFormat(
                            "Arrow field '%s' cannot be a null scalar",
                            node->name)));
  }
  ++node->length;
  return absl::OkStatus();
}

absl::Status AppendDefault(ArrowNode* node);
absl::Status NullNode(ArrowNode* node);

absl::Status FinishStruct(ArrowNode* node) {
  if (absl::Status status = Require(node, Kind::kStruct); !status.ok()) {
    return status;
  }
  for (const std::unique_ptr<ArrowNode>& child : node->children) {
    if (child->length != node->length + 1) {
      return Fail(node, absl::InternalError(absl::StrFormat(
                            "Arrow struct '%s' child '%s' has %v rows, expected %v",
                            node->name, child->name, child->length,
                            node->length + 1)));
    }
  }
  Mark(node, true);
  ++node->length;
  return absl::OkStatus();
}

// A valid placeholder row. Used for a non-nullable column of a null parent,
// and for a nullable column that is itself null when the column can be null.
absl::Status AppendDefault(ArrowNode* node) {
  if (node == nullptr) {
    return absl::InvalidArgumentError("Arrow writer has no column");
  }
  if (!node->error.ok()) {
    return node->error;
  }
  // A nullable column of a placeholder row is null. A non-nullable column
  // still needs a valid row so the parent struct's children stay aligned.
  if (node->nullable && node->kind != Kind::kUnion &&
      node->kind != Kind::kFixed) {
    return NullNode(node);
  }
  switch (node->kind) {
    case Kind::kStruct:
      for (const std::unique_ptr<ArrowNode>& child : node->children) {
        if (absl::Status status = AppendDefault(child.get()); !status.ok()) {
          return status;
        }
      }
      return FinishStruct(node);
    case Kind::kList:
    case Kind::kMap:
      Mark(node, true);
      node->offsets.push_back(node->offsets.back());
      ++node->length;
      return absl::OkStatus();
    case Kind::kFixed:
      for (int32_t i = 0; i < node->fixed; ++i) {
        if (absl::Status status = AppendDefault(node->children[0].get());
            !status.ok()) {
          return status;
        }
      }
      Mark(node, true);
      ++node->length;
      return absl::OkStatus();
    case Kind::kUnion:
      if (node->children.empty() || node->children[0]->kind != Kind::kNull) {
        return Fail(node, absl::InternalError(
                              "Arrow union is missing its none arm"));
      }
      node->type_ids.push_back(0);
      node->union_offsets.push_back(
          static_cast<int32_t>(node->children[0]->length));
      ++node->children[0]->length;
      ++node->length;
      return absl::OkStatus();
    case Kind::kBool:
      Mark(node, true);
      PushBit(&node->values, node->length, false);
      ++node->length;
      return absl::OkStatus();
    case Kind::kUtf8:
      return AppendBytes(node, Kind::kUtf8, std::string_view());
    case Kind::kBinary:
      return AppendBytes(node, Kind::kBinary, std::string_view());
    case Kind::kInt32:
      return AppendRaw(node, Kind::kInt32, int32_t{0});
    case Kind::kUInt32:
      return AppendRaw(node, Kind::kUInt32, uint32_t{0});
    case Kind::kInt64:
      return AppendRaw(node, Kind::kInt64, int64_t{0});
    case Kind::kUInt64:
      return AppendRaw(node, Kind::kUInt64, uint64_t{0});
    case Kind::kFloat:
      return AppendRaw(node, Kind::kFloat, 0.f);
    case Kind::kDouble:
      return AppendRaw(node, Kind::kDouble, 0.0);
    case Kind::kNull:
      ++node->length;
      return absl::OkStatus();
  }
  return Fail(node, absl::InternalError("Arrow field has an unknown kind"));
}

absl::Status NullNode(ArrowNode* node) {
  if (node == nullptr) {
    return absl::InvalidArgumentError("Arrow writer has no column");
  }
  if (!node->nullable || node->kind == Kind::kUnion ||
      node->kind == Kind::kFixed) {
    return AppendDefault(node);
  }
  switch (node->kind) {
    case Kind::kStruct:
      for (const std::unique_ptr<ArrowNode>& child : node->children) {
        if (absl::Status status = NullNode(child.get()); !status.ok()) {
          return status;
        }
      }
      Mark(node, false);
      ++node->length;
      return absl::OkStatus();
    case Kind::kList:
    case Kind::kMap:
      Mark(node, false);
      node->offsets.push_back(node->offsets.back());
      ++node->length;
      return absl::OkStatus();
    default:
      return AppendNull(node);
  }
}

absl::Status NullStruct(ArrowNode* node) {
  if (absl::Status status = Require(node, Kind::kStruct); !status.ok()) {
    return status;
  }
  if (!node->nullable) {
    return AppendDefault(node);
  }
  return NullNode(node);
}

Kind KindOf(std::string_view format) {
  if (format == "n") return Kind::kNull;
  if (format == "b") return Kind::kBool;
  if (format == "i") return Kind::kInt32;
  if (format == "I") return Kind::kUInt32;
  if (format == "l") return Kind::kInt64;
  if (format == "L") return Kind::kUInt64;
  if (format == "f") return Kind::kFloat;
  if (format == "g") return Kind::kDouble;
  if (format == "u") return Kind::kUtf8;
  if (format == "z") return Kind::kBinary;
  if (format == "+s") return Kind::kStruct;
  if (format == "+l") return Kind::kList;
  if (format == "+m") return Kind::kMap;
  if (format.size() > 3 && format.substr(0, 3) == "+w:") return Kind::kFixed;
  if (format.size() > 4 && format.substr(0, 4) == "+ud:") return Kind::kUnion;
  return Kind::kNull;
}

absl::StatusOr<std::unique_ptr<ArrowNode>> ImportNode(const ArrowSchema* schema) {
  if (schema == nullptr || schema->format == nullptr) {
    return absl::InvalidArgumentError("Arrow schema is missing a format");
  }
  if (schema->dictionary != nullptr) {
    return absl::InvalidArgumentError("Arrow dictionary types are not supported");
  }
  auto node = std::make_unique<ArrowNode>();
  node->name = schema->name == nullptr ? "" : schema->name;
  node->nullable = (schema->flags & ARROW_FLAG_NULLABLE) != 0;
  const std::string_view format(schema->format);
  node->kind = KindOf(format);
  if (format == "n") {
    node->kind = Kind::kNull;
  } else if (node->kind == Kind::kNull && format != "n") {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported Arrow format '%s'", format));
  }
  if (node->kind == Kind::kFixed) {
    node->fixed = std::atoi(schema->format + 3);
    if (node->fixed <= 0) {
      return absl::InvalidArgumentError("Arrow fixed-size list has no size");
    }
  }
  if (schema->n_children < 0) {
    return absl::InvalidArgumentError("Arrow schema has a negative child count");
  }
  if (schema->n_children > 0 && schema->children == nullptr) {
    return absl::InvalidArgumentError("Arrow schema is missing its children");
  }
  for (int64_t i = 0; i < schema->n_children; ++i) {
    absl::StatusOr<std::unique_ptr<ArrowNode>> child = ImportNode(schema->children[i]);
    if (!child.ok()) {
      return child.status();
    }
    node->children.push_back(std::move(*child));
  }
  if ((node->kind == Kind::kList || node->kind == Kind::kMap ||
       node->kind == Kind::kFixed) &&
      node->children.size() != 1) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Arrow field '%s' must have one child", node->name));
  }
  return node;
}

struct SchemaOwned {
  std::string format;
  std::string name;
  std::vector<ArrowSchema*> children;
};

struct ArrayOwned {
  std::vector<void*> allocations;
  std::vector<const void*> buffers;
  std::vector<ArrowArray*> children;
};

void ReleaseSchema(struct ArrowSchema* schema) {
  if (schema == nullptr || schema->release == nullptr) {
    return;
  }
  auto* owned = static_cast<SchemaOwned*>(schema->private_data);
  if (owned != nullptr) {
    for (ArrowSchema* child : owned->children) {
      if (child != nullptr && child->release != nullptr) {
        child->release(child);
      }
      delete child;
    }
    delete owned;
  }
  schema->release = nullptr;
  schema->private_data = nullptr;
}

void ReleaseArray(struct ArrowArray* array) {
  if (array == nullptr || array->release == nullptr) {
    return;
  }
  auto* owned = static_cast<ArrayOwned*>(array->private_data);
  if (owned != nullptr) {
    for (ArrowArray* child : owned->children) {
      if (child != nullptr && child->release != nullptr) {
        child->release(child);
      }
      delete child;
    }
    for (void* allocation : owned->allocations) {
      free(allocation);
    }
    delete owned;
  }
  array->release = nullptr;
  array->private_data = nullptr;
}

ArrowSchema* ExportSchemaNode(const SchemaNode& node) {
  auto* schema = new ArrowSchema();
  auto* owned = new SchemaOwned();
  owned->format = node.format;
  owned->name = node.name;
  schema->format = owned->format.c_str();
  schema->name = owned->name.c_str();
  schema->metadata = nullptr;
  schema->flags = node.nullable ? ARROW_FLAG_NULLABLE : 0;
  schema->n_children = static_cast<int64_t>(node.children.size());
  schema->dictionary = nullptr;
  schema->release = ReleaseSchema;
  schema->private_data = owned;
  if (node.children.empty()) {
    schema->children = nullptr;
    return schema;
  }
  owned->children.reserve(node.children.size());
  for (const std::unique_ptr<SchemaNode>& child : node.children) {
    owned->children.push_back(ExportSchemaNode(*child));
  }
  schema->children = owned->children.data();
  return schema;
}

const void* ExportBytes(ArrayOwned* owned, const void* data, size_t size) {
  if (size == 0) {
    return nullptr;
  }
  void* memory = AllocateAligned(size);
  if (memory == nullptr) {
    return nullptr;
  }
  memcpy(memory, data, size);
  owned->allocations.push_back(memory);
  return memory;
}

absl::Status CheckNode(const ArrowNode& node) {
  if (!node.error.ok()) {
    return node.error;
  }
  for (const std::unique_ptr<ArrowNode>& child : node.children) {
    if (absl::Status status = CheckNode(*child); !status.ok()) {
      return status;
    }
  }
  switch (node.kind) {
    case Kind::kStruct:
      for (const std::unique_ptr<ArrowNode>& child : node.children) {
        if (child->length != node.length) {
          return absl::InternalError(absl::StrFormat(
              "Arrow struct '%s' child '%s' length %d does not match %d",
              node.name, child->name, child->length, node.length));
        }
      }
      break;
    case Kind::kList:
    case Kind::kMap:
      if (static_cast<int64_t>(node.offsets.size()) != node.length + 1) {
        return absl::InternalError("Arrow list offsets do not match its length");
      }
      if (node.children[0]->length != node.offsets.back()) {
        return absl::InternalError("Arrow list values do not match its offsets");
      }
      break;
    case Kind::kFixed:
      if (node.children[0]->length != node.length * node.fixed) {
        return absl::InternalError("Arrow fixed-size list values do not match");
      }
      break;
    case Kind::kUnion: {
      std::vector<int64_t> counts(node.children.size(), 0);
      if (static_cast<int64_t>(node.type_ids.size()) != node.length ||
          node.union_offsets.size() != node.type_ids.size()) {
        return absl::InternalError("Arrow union buffers do not match its length");
      }
      for (int64_t i = 0; i < node.length; ++i) {
        const int id = node.type_ids[static_cast<size_t>(i)];
        if (id < 0 || static_cast<size_t>(id) >= node.children.size() ||
            node.union_offsets[static_cast<size_t>(i)] != counts[static_cast<size_t>(id)]) {
          return absl::InternalError("Arrow union offsets are not dense");
        }
        ++counts[static_cast<size_t>(id)];
      }
      for (size_t i = 0; i < node.children.size(); ++i) {
        if (node.children[i]->length != counts[i]) {
          return absl::InternalError("Arrow union child length does not match");
        }
      }
      break;
    }
    default:
      break;
  }
  return absl::OkStatus();
}

ArrowArray* ExportNode(const ArrowNode& node, absl::Status* status) {
  auto* array = new ArrowArray();
  auto* owned = new ArrayOwned();
  array->length = node.length;
  array->null_count = node.null_count;
  array->offset = 0;
  array->dictionary = nullptr;
  array->release = ReleaseArray;
  array->private_data = owned;
  array->n_children = static_cast<int64_t>(node.children.size());
  const void* validity = nullptr;
  if (node.null_count > 0) {
    validity = ExportBytes(owned, node.validity.data(), node.validity.size());
  }
  auto fail = [&]() {
    *status = absl::ResourceExhaustedError("Out of memory exporting Arrow");
    array->release(array);
    delete array;
    return static_cast<ArrowArray*>(nullptr);
  };
  switch (node.kind) {
    case Kind::kNull:
      array->n_buffers = 0;
      array->buffers = nullptr;
      break;
    case Kind::kBool:
    case Kind::kInt32:
    case Kind::kUInt32:
    case Kind::kInt64:
    case Kind::kUInt64:
    case Kind::kFloat:
    case Kind::kDouble: {
      const void* values =
          ExportBytes(owned, node.values.data(), node.values.size());
      if (node.length > 0 && values == nullptr) {
        return fail();
      }
      owned->buffers = {validity, values};
      array->n_buffers = 2;
      array->buffers = owned->buffers.data();
      break;
    }
    case Kind::kUtf8:
    case Kind::kBinary: {
      const void* offsets =
          ExportBytes(owned, node.offsets.data(), node.offsets.size() * sizeof(int32_t));
      const void* values =
          ExportBytes(owned, node.values.data(), node.values.size());
      if (offsets == nullptr || (node.values.size() > 0 && values == nullptr)) {
        return fail();
      }
      owned->buffers = {validity, offsets, values};
      array->n_buffers = 3;
      array->buffers = owned->buffers.data();
      break;
    }
    case Kind::kStruct:
    case Kind::kFixed:
      owned->buffers = {validity};
      array->n_buffers = 1;
      array->buffers = owned->buffers.data();
      break;
    case Kind::kList:
    case Kind::kMap: {
      const void* offsets =
          ExportBytes(owned, node.offsets.data(), node.offsets.size() * sizeof(int32_t));
      if (offsets == nullptr) {
        return fail();
      }
      owned->buffers = {validity, offsets};
      array->n_buffers = 2;
      array->buffers = owned->buffers.data();
      break;
    }
    case Kind::kUnion: {
      const void* types =
          ExportBytes(owned, node.type_ids.data(), node.type_ids.size());
      const void* offsets = ExportBytes(
          owned, node.union_offsets.data(),
          node.union_offsets.size() * sizeof(int32_t));
      if (node.length > 0 && (types == nullptr || offsets == nullptr)) {
        return fail();
      }
      owned->buffers = {types, offsets};
      array->n_buffers = 2;
      array->buffers = owned->buffers.data();
      break;
    }
  }
  if (!node.children.empty()) {
    for (const std::unique_ptr<ArrowNode>& child : node.children) {
      ArrowArray* exported = ExportNode(*child, status);
      if (exported == nullptr) {
        array->release(array);
        delete array;
        return nullptr;
      }
      owned->children.push_back(exported);
    }
    array->children = owned->children.data();
  } else {
    array->children = nullptr;
  }
  return array;
}

bool NonZeroOffset(const ArrowArray* array) {
  if (array == nullptr || array->offset != 0) {
    return true;
  }
  for (int64_t i = 0; i < array->n_children; ++i) {
    if (array->children == nullptr || NonZeroOffset(array->children[i])) {
      return true;
    }
  }
  return false;
}

const char* FormatOf(const ArrowSchema* schema) {
  return schema->format == nullptr ? "" : schema->format;
}

std::string_view NameOf(const ArrowSchema* schema) {
  return schema->name == nullptr ? std::string_view() : std::string_view(schema->name);
}

absl::Status CheckRow(const ArrowArray* array, int64_t row) {
  if (array == nullptr) {
    return absl::InvalidArgumentError("Arrow array is missing");
  }
  if (row < 0 || row >= array->length) {
    return absl::OutOfRangeError("Arrow row is out of range");
  }
  return absl::OkStatus();
}

template <typename T>
absl::StatusOr<T> ReadNumber(const ArrowArray* array, int64_t row,
                             const char* format, std::string_view name,
                             std::string_view expected) {
  if (std::string_view(format) != expected) {
    return FormatError(name, expected, format);
  }
  if (absl::Status status = CheckRow(array, row); !status.ok()) {
    return status;
  }
  if (array->n_buffers < 2 || array->buffers == nullptr || array->buffers[1] == nullptr) {
    return absl::InvalidArgumentError("Arrow numeric column is missing its values");
  }
  const auto* values = static_cast<const T*>(array->buffers[1]);
  return values[row];
}

absl::StatusOr<std::string_view> ReadBytes(const ArrowArray* array, int64_t row,
                                           const char* format,
                                           std::string_view name,
                                           std::string_view expected) {
  if (std::string_view(format) != expected) {
    return FormatError(name, expected, format);
  }
  if (absl::Status status = CheckRow(array, row); !status.ok()) {
    return status;
  }
  if (array->n_buffers < 3 || array->buffers == nullptr || array->buffers[1] == nullptr) {
    return absl::InvalidArgumentError("Arrow string column is missing its offsets");
  }
  const auto* offsets = static_cast<const int32_t*>(array->buffers[1]);
  const int32_t begin = offsets[row];
  const int32_t end = offsets[row + 1];
  if (end < begin) {
    return absl::InvalidArgumentError("Arrow string offsets go backwards");
  }
  if (end == begin) {
    return std::string_view();
  }
  if (array->buffers[2] == nullptr) {
    return absl::InvalidArgumentError("Arrow string column is missing its data");
  }
  return std::string_view(static_cast<const char*>(array->buffers[2]) + begin,
                          static_cast<size_t>(end - begin));
}

void TypeSet::Add(std::string_view full_name) {
  const std::string name(full_name);
  if (index_.contains(name)) {
    return;
  }
  index_.emplace(name, static_cast<int>(order_.size()));
  order_.push_back(name);
}

int TypeSet::IndexOf(std::string_view full_name) const {
  const auto it = index_.find(std::string(full_name));
  if (it == index_.end()) {
    return -1;
  }
  return it->second;
}

bool TypeSet::Visit(const void* buffer, uint32_t offset) {
  return seen_.insert(VisitKey{buffer, offset}).second;
}

struct SchemaBuilder::Impl {
  std::unique_ptr<SchemaNode> root = std::make_unique<SchemaNode>();
  std::vector<SchemaNode*> stack;
  std::vector<std::string> types;
  absl::Status error;

  Impl() {
    root->kind = Kind::kStruct;
    stack.push_back(root.get());
  }

  SchemaNode* Current() { return stack.back(); }

  void Fail(absl::Status status) {
    if (error.ok()) {
      error = std::move(status);
    }
  }

  SchemaNode* AddChild(std::string_view name, Kind kind, std::string format,
                       bool nullable) {
    if (!error.ok()) {
      return nullptr;
    }
    auto child = std::make_unique<SchemaNode>();
    child->kind = kind;
    child->format = std::move(format);
    child->name = std::string(name);
    child->nullable = nullable;
    SchemaNode* raw = child.get();
    Current()->children.push_back(std::move(child));
    return raw;
  }
};

SchemaBuilder::SchemaBuilder() : impl_(std::make_unique<Impl>()) {}
SchemaBuilder::~SchemaBuilder() = default;
SchemaBuilder::SchemaBuilder(SchemaBuilder&&) noexcept = default;
SchemaBuilder& SchemaBuilder::operator=(SchemaBuilder&&) noexcept = default;

SchemaBuilder::TypeGuard::TypeGuard(SchemaBuilder& builder,
                                    std::string_view full_name)
    : builder_(&builder) {
  std::vector<std::string>& types = builder.impl_->types;
  const std::string name(full_name);
  for (const std::string& existing : types) {
    if (existing == name) {
      cycle_ = true;
      return;
    }
  }
  types.push_back(name);
}

SchemaBuilder::TypeGuard::~TypeGuard() {
  if (builder_ != nullptr && !cycle_) {
    builder_->impl_->types.pop_back();
  }
}

void SchemaBuilder::BeginStruct(std::string_view name, bool nullable) {
  SchemaNode* node = impl_->AddChild(name, Kind::kStruct, "+s", nullable);
  if (node != nullptr) {
    impl_->stack.push_back(node);
  }
}

void SchemaBuilder::EndStruct() {
  if (impl_->stack.size() <= 1 || impl_->Current()->kind != Kind::kStruct) {
    impl_->Fail(absl::InternalError("Arrow schema struct is unbalanced"));
    return;
  }
  impl_->stack.pop_back();
}

void SchemaBuilder::BeginList(std::string_view name, bool nullable) {
  SchemaNode* node = impl_->AddChild(name, Kind::kList, "+l", nullable);
  if (node != nullptr) {
    impl_->stack.push_back(node);
  }
}

void SchemaBuilder::EndList() {
  if (impl_->stack.size() <= 1 || impl_->Current()->kind != Kind::kList ||
      impl_->Current()->children.size() != 1) {
    impl_->Fail(absl::InternalError("Arrow schema list is unbalanced"));
    return;
  }
  impl_->stack.pop_back();
}

void SchemaBuilder::BeginFixedList(std::string_view name, int32_t size) {
  SchemaNode* node =
      impl_->AddChild(name, Kind::kFixed, absl::StrFormat("+w:%d", size), false);
  if (node != nullptr) {
    node->fixed = size;
    impl_->stack.push_back(node);
  }
}

void SchemaBuilder::EndFixedList() {
  if (impl_->stack.size() <= 1 || impl_->Current()->kind != Kind::kFixed ||
      impl_->Current()->children.size() != 1) {
    impl_->Fail(absl::InternalError("Arrow schema fixed list is unbalanced"));
    return;
  }
  impl_->stack.pop_back();
}

void SchemaBuilder::BeginMap(std::string_view name, bool nullable) {
  SchemaNode* node = impl_->AddChild(name, Kind::kMap, "+m", nullable);
  if (node != nullptr) {
    impl_->stack.push_back(node);
  }
}

void SchemaBuilder::EndMap() {
  if (impl_->stack.size() <= 1 || impl_->Current()->kind != Kind::kMap ||
      impl_->Current()->children.size() != 1 ||
      impl_->Current()->children[0]->kind != Kind::kStruct) {
    impl_->Fail(absl::InternalError("Arrow schema map is unbalanced"));
    return;
  }
  impl_->stack.pop_back();
}

void SchemaBuilder::BeginUnion(std::string_view name, bool nullable) {
  SchemaNode* node = impl_->AddChild(name, Kind::kUnion, "+ud:", nullable);
  if (node == nullptr) {
    return;
  }
  impl_->stack.push_back(node);
  SchemaNode* none = impl_->AddChild("none", Kind::kNull, "n", false);
  (void)none;
}

void SchemaBuilder::EndUnion() {
  if (impl_->stack.size() <= 1 || impl_->Current()->kind != Kind::kUnion ||
      impl_->Current()->children.empty()) {
    impl_->Fail(absl::InternalError("Arrow schema union is unbalanced"));
    return;
  }
  std::string format = "+ud:";
  for (size_t i = 0; i < impl_->Current()->children.size(); ++i) {
    if (i != 0) {
      format.push_back(',');
    }
    format += absl::StrCat(i);
  }
  impl_->Current()->format = std::move(format);
  impl_->stack.pop_back();
}

void SchemaBuilder::AddInt32(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kInt32, "i", nullable);
}
void SchemaBuilder::AddUInt32(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kUInt32, "I", nullable);
}
void SchemaBuilder::AddInt64(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kInt64, "l", nullable);
}
void SchemaBuilder::AddUInt64(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kUInt64, "L", nullable);
}
void SchemaBuilder::AddFloat(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kFloat, "f", nullable);
}
void SchemaBuilder::AddDouble(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kDouble, "g", nullable);
}
void SchemaBuilder::AddBool(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kBool, "b", nullable);
}
void SchemaBuilder::AddString(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kUtf8, "u", nullable);
}
void SchemaBuilder::AddBinary(std::string_view name, bool nullable) {
  impl_->AddChild(name, Kind::kBinary, "z", nullable);
}

absl::Status SchemaBuilder::status() const { return impl_->error; }

absl::Status SchemaBuilder::Finish(struct ArrowSchema* schema) {
  if (!impl_->error.ok()) {
    return impl_->error;
  }
  if (impl_->stack.size() != 1 || impl_->root->children.size() != 1) {
    return absl::InternalError("Arrow schema was not finished");
  }
  *schema = ArrowSchema{};
  ArrowSchema* exported = ExportSchemaNode(*impl_->root->children[0]);
  *schema = *exported;
  // The caller owns the top-level schema object. Keep the private data and
  // retarget release at that object, then drop the temporary shell.
  schema->release = ReleaseSchema;
  delete exported;
  return absl::OkStatus();
}

absl::Status FieldWriter::status() const {
  if (node_ == nullptr) {
    return absl::InvalidArgumentError("Arrow writer has no column");
  }
  return node_->error;
}

absl::Status FieldWriter::Int32(int32_t value) {
  return AppendRaw(node_, Kind::kInt32, value);
}
absl::Status FieldWriter::UInt32(uint32_t value) {
  return AppendRaw(node_, Kind::kUInt32, value);
}
absl::Status FieldWriter::Int64(int64_t value) {
  return AppendRaw(node_, Kind::kInt64, value);
}
absl::Status FieldWriter::UInt64(uint64_t value) {
  return AppendRaw(node_, Kind::kUInt64, value);
}
absl::Status FieldWriter::Float(float value) {
  return AppendRaw(node_, Kind::kFloat, value);
}
absl::Status FieldWriter::Double(double value) {
  return AppendRaw(node_, Kind::kDouble, value);
}
absl::Status FieldWriter::Bool(bool value) {
  if (absl::Status status = Require(node_, Kind::kBool); !status.ok()) {
    return status;
  }
  Mark(node_, true);
  PushBit(&node_->values, node_->length, value);
  ++node_->length;
  return absl::OkStatus();
}
absl::Status FieldWriter::String(std::string_view value) {
  return AppendBytes(node_, Kind::kUtf8, value);
}
absl::Status FieldWriter::Binary(std::string_view value) {
  return AppendBytes(node_, Kind::kBinary, value);
}
absl::Status FieldWriter::Null() { return AppendNull(node_); }

FieldWriter FieldWriter::Child(int index) {
  if (node_ == nullptr || index < 0 ||
      static_cast<size_t>(index) >= node_->children.size()) {
    ArrowNode* invalid = node_;
    if (invalid != nullptr) {
      Fail(invalid, absl::InvalidArgumentError(absl::StrFormat(
                        "Arrow field '%s' has no child %d", node_->name, index)));
    }
    return FieldWriter(nullptr);
  }
  return FieldWriter(node_->children[static_cast<size_t>(index)].get());
}

absl::Status FieldWriter::FinishStruct() { return ::phaser::arrow::FinishStruct(node_); }
absl::Status FieldWriter::NullStruct() { return ::phaser::arrow::NullStruct(node_); }

FieldWriter FieldWriter::Values() { return Child(0); }

absl::Status FieldWriter::FinishList(int64_t count) {
  if (node_ == nullptr || (node_->kind != Kind::kList && node_->kind != Kind::kMap)) {
    return Fail(node_, absl::InvalidArgumentError("Arrow field is not a list"));
  }
  if (count < 0 ||
      count > static_cast<int64_t>(std::numeric_limits<int32_t>::max()) ||
      node_->offsets.back() >
          std::numeric_limits<int32_t>::max() - static_cast<int32_t>(count)) {
    return Fail(node_, absl::InvalidArgumentError(
                           "Arrow list is larger than int32 offsets allow"));
  }
  if (node_->children[0]->length !=
      static_cast<int64_t>(node_->offsets.back()) + count) {
    return Fail(node_, absl::InternalError(absl::StrFormat(
                           "Arrow list '%s' expected %d new values", node_->name,
                           count)));
  }
  Mark(node_, true);
  node_->offsets.push_back(node_->offsets.back() + static_cast<int32_t>(count));
  ++node_->length;
  return absl::OkStatus();
}

absl::Status FieldWriter::FinishFixed(int32_t count) {
  if (absl::Status status = Require(node_, Kind::kFixed); !status.ok()) {
    return status;
  }
  if (count != node_->fixed) {
    return Fail(node_, absl::InvalidArgumentError(
                           "Arrow fixed-size list was given the wrong count"));
  }
  if (node_->children[0]->length != (node_->length + 1) * node_->fixed) {
    return Fail(node_, absl::InternalError(
                           "Arrow fixed-size list is missing values"));
  }
  Mark(node_, true);
  ++node_->length;
  return absl::OkStatus();
}

absl::Status FieldWriter::NullUnion() {
  if (absl::Status status = Require(node_, Kind::kUnion); !status.ok()) {
    return status;
  }
  if (node_->children.empty() || node_->children[0]->kind != Kind::kNull) {
    return Fail(node_, absl::InternalError("Arrow union is missing its none arm"));
  }
  node_->type_ids.push_back(0);
  node_->union_offsets.push_back(static_cast<int32_t>(node_->children[0]->length));
  ++node_->children[0]->length;
  ++node_->length;
  return absl::OkStatus();
}

FieldWriter FieldWriter::UnionChild(int child_index) {
  if (absl::Status status = Require(node_, Kind::kUnion); !status.ok()) {
    return FieldWriter(nullptr);
  }
  if (child_index <= 0 ||
      static_cast<size_t>(child_index) >= node_->children.size()) {
    Fail(node_, absl::InvalidArgumentError("Arrow union arm is out of range"));
    return FieldWriter(nullptr);
  }
  ArrowNode* child = node_->children[static_cast<size_t>(child_index)].get();
  node_->type_ids.push_back(static_cast<int8_t>(child_index));
  node_->union_offsets.push_back(static_cast<int32_t>(child->length));
  ++node_->length;
  return FieldWriter(child);
}

BatchWriter::BatchWriter(std::unique_ptr<ArrowNode> root) : root_(std::move(root)) {}
BatchWriter::~BatchWriter() = default;
BatchWriter::BatchWriter(BatchWriter&&) noexcept = default;
BatchWriter& BatchWriter::operator=(BatchWriter&&) noexcept = default;

absl::StatusOr<BatchWriter> BatchWriter::Open(const struct ArrowSchema* schema) {
  absl::StatusOr<std::unique_ptr<ArrowNode>> root = ImportNode(schema);
  if (!root.ok()) {
    return root.status();
  }
  return BatchWriter(std::move(*root));
}

FieldWriter BatchWriter::Root() { return FieldWriter(root_.get()); }

absl::Status BatchWriter::Finish(struct ArrowArray* array) {
  if (absl::Status status = CheckNode(*root_); !status.ok()) {
    return status;
  }
  absl::Status status = absl::OkStatus();
  ArrowArray* exported = ExportNode(*root_, &status);
  if (exported == nullptr) {
    return status.ok() ? absl::InternalError("Arrow export failed") : status;
  }
  *array = *exported;
  array->release = ReleaseArray;
  delete exported;
  return absl::OkStatus();
}

bool FieldReader::IsNull() const {
  if (!status_.ok() || schema_ == nullptr || array_ == nullptr) {
    return false;
  }
  if ((schema_->flags & ARROW_FLAG_NULLABLE) == 0 || array_->null_count == 0) {
    return false;
  }
  if (array_->n_buffers < 1 || array_->buffers == nullptr) {
    return false;
  }
  return !BitSet(array_->buffers[0], row_);
}

absl::Status FieldReader::status() const { return status_; }

int64_t FieldReader::length() const {
  if (!status_.ok() || IsNull() || schema_ == nullptr) {
    return 0;
  }
  const std::string_view format(FormatOf(schema_));
  if (format == "+l" || format == "+m") {
    if (array_->n_buffers < 2 || array_->buffers == nullptr ||
        array_->buffers[1] == nullptr) {
      return 0;
    }
    const auto* offsets = static_cast<const int32_t*>(array_->buffers[1]);
    return static_cast<int64_t>(offsets[row_ + 1] - offsets[row_]);
  }
  if (format.size() > 3 && format.substr(0, 3) == "+w:") {
    return std::atoi(schema_->format + 3);
  }
  return 0;
}

absl::StatusOr<int32_t> FieldReader::Int32() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadNumber<int32_t>(array_, row_, FormatOf(schema_), NameOf(schema_), "i");
}
absl::StatusOr<uint32_t> FieldReader::UInt32() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadNumber<uint32_t>(array_, row_, FormatOf(schema_), NameOf(schema_), "I");
}
absl::StatusOr<int64_t> FieldReader::Int64() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadNumber<int64_t>(array_, row_, FormatOf(schema_), NameOf(schema_), "l");
}
absl::StatusOr<uint64_t> FieldReader::UInt64() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadNumber<uint64_t>(array_, row_, FormatOf(schema_), NameOf(schema_), "L");
}
absl::StatusOr<float> FieldReader::Float() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadNumber<float>(array_, row_, FormatOf(schema_), NameOf(schema_), "f");
}
absl::StatusOr<double> FieldReader::Double() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadNumber<double>(array_, row_, FormatOf(schema_), NameOf(schema_), "g");
}
absl::StatusOr<bool> FieldReader::Bool() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  if (std::string_view(FormatOf(schema_)) != "b") {
    return FormatError(NameOf(schema_), "b", FormatOf(schema_));
  }
  if (absl::Status row = CheckRow(array_, row_); !row.ok()) return row;
  if (array_->n_buffers < 2 || array_->buffers == nullptr) {
    return absl::InvalidArgumentError("Arrow bool column is missing its values");
  }
  return BitSet(array_->buffers[1], row_);
}
absl::StatusOr<std::string_view> FieldReader::String() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadBytes(array_, row_, FormatOf(schema_), NameOf(schema_), "u");
}
absl::StatusOr<std::string_view> FieldReader::Binary() const {
  if (!status_.ok()) return status_;
  if (IsNull()) return absl::FailedPreconditionError("Arrow value is null");
  return ReadBytes(array_, row_, FormatOf(schema_), NameOf(schema_), "z");
}

FieldReader FieldReader::Child(int index) const {
  FieldReader child = *this;
  if (!status_.ok()) {
    return child;
  }
  if (schema_ == nullptr || index < 0 || index >= schema_->n_children ||
      schema_->children == nullptr || array_->children == nullptr) {
    child.status_ = absl::InvalidArgumentError("Arrow field has no such child");
    return child;
  }
  child.schema_ = schema_->children[index];
  child.array_ = array_->children[index];
  return child;
}

FieldReader FieldReader::Element(int64_t index) const {
  FieldReader child = Child(0);
  if (!child.status_.ok()) {
    return child;
  }
  const std::string_view format(FormatOf(schema_));
  int64_t base = 0;
  if (format == "+l" || format == "+m") {
    if (index < 0 || index >= length()) {
      child.status_ = absl::OutOfRangeError("Arrow list element is out of range");
      return child;
    }
    const auto* offsets = static_cast<const int32_t*>(array_->buffers[1]);
    base = offsets[row_];
  } else if (format.size() > 3 && format.substr(0, 3) == "+w:") {
    const int32_t size = std::atoi(schema_->format + 3);
    if (index < 0 || index >= size) {
      child.status_ = absl::OutOfRangeError("Arrow fixed-list element is out of range");
      return child;
    }
    base = row_ * size;
  } else {
    child.status_ = absl::InvalidArgumentError("Arrow field is not a list");
    return child;
  }
  child.row_ = base + index;
  return child;
}

int FieldReader::UnionType() const {
  if (!status_.ok() || std::string_view(FormatOf(schema_)).substr(0, 4) != "+ud:") {
    return -1;
  }
  if (array_->n_buffers < 1 || array_->buffers == nullptr || array_->buffers[0] == nullptr) {
    return -1;
  }
  return static_cast<const int8_t*>(array_->buffers[0])[row_];
}

absl::StatusOr<std::string_view> FieldReader::UnionChildName() const {
  const int type = UnionType();
  if (type < 0 || schema_->children == nullptr || type >= schema_->n_children) {
    return absl::InvalidArgumentError("Arrow union arm is out of range");
  }
  return NameOf(schema_->children[type]);
}

FieldReader FieldReader::UnionValue() const {
  FieldReader child;
  const int type = UnionType();
  if (type < 0 || schema_->children == nullptr || array_->children == nullptr ||
      type >= schema_->n_children) {
    child.status_ = absl::InvalidArgumentError("Arrow union arm is out of range");
    return child;
  }
  if (array_->n_buffers < 2 || array_->buffers[1] == nullptr) {
    child.status_ = absl::InvalidArgumentError("Arrow union is missing its offsets");
    return child;
  }
  child.schema_ = schema_->children[type];
  child.array_ = array_->children[type];
  child.row_ = static_cast<const int32_t*>(array_->buffers[1])[row_];
  return child;
}

absl::StatusOr<BatchReader> BatchReader::Open(const struct ArrowSchema* schema,
                                              const struct ArrowArray* array) {
  if (schema == nullptr || array == nullptr || schema->format == nullptr) {
    return absl::InvalidArgumentError("Arrow batch is missing its schema or array");
  }
  if (std::string_view(schema->format) != "+s") {
    return absl::InvalidArgumentError("Arrow batch root must be a struct");
  }
  if (array->n_children != schema->n_children) {
    return absl::InvalidArgumentError("Arrow array and schema child counts differ");
  }
  if (NonZeroOffset(array)) {
    return absl::InvalidArgumentError(
        "Arrow arrays with a non-zero offset are not supported");
  }
  BatchReader reader;
  reader.schema_ = schema;
  reader.array_ = array;
  return reader;
}

int64_t BatchReader::length() const {
  return array_ == nullptr ? 0 : array_->length;
}

FieldReader BatchReader::Row(int64_t row) const {
  FieldReader reader;
  reader.schema_ = schema_;
  reader.array_ = array_;
  reader.row_ = row;
  reader.status_ = CheckRow(array_, row);
  return reader;
}

namespace {

absl::flat_hash_map<std::string, ArrowPluginFn>& ArrowOps() {
  static auto* ops = new absl::flat_hash_map<std::string, ArrowPluginFn>();
  return *ops;
}

absl::Status MissingArrowHook(std::string_view full_name,
                              std::string_view operation) {
  return absl::FailedPreconditionError(absl::StrFormat(
      "Phaser message '%s' does not support %s", full_name, operation));
}

template <typename Fn>
absl::StatusOr<Fn> ArrowHook(std::string_view full_name, Fn BankInfo::* member,
                             std::string_view operation) {
  absl::StatusOr<BankInfo*> bank_info = GetPhaserBankInfo(full_name);
  if (!bank_info.ok()) {
    return bank_info.status();
  }
  const Fn hook = (**bank_info).*member;
  if (hook == nullptr) {
    return MissingArrowHook(full_name, operation);
  }
  return hook;
}

absl::Status CollectTypes(std::string_view full_name,
                          const ArrowMessageArgs& args) {
  if (args.types == nullptr) {
    return absl::InvalidArgumentError("Arrow type set is null");
  }
  absl::StatusOr<decltype(BankInfo::collect_arrow_types_at_offset)> hook =
      ArrowHook(full_name, &BankInfo::collect_arrow_types_at_offset,
                "Arrow type collection");
  if (!hook.ok()) {
    return hook.status();
  }
  return (*hook)(args.runtime, args.offset, args.types);
}

absl::Status WriteAtOffset(std::string_view full_name,
                           const ArrowMessageArgs& args) {
  if (args.writer == nullptr) {
    return absl::InvalidArgumentError("Arrow field writer is null");
  }
  absl::StatusOr<decltype(BankInfo::write_arrow_at_offset)> hook =
      ArrowHook(full_name, &BankInfo::write_arrow_at_offset, "Arrow writing");
  if (!hook.ok()) {
    return hook.status();
  }
  return (*hook)(args.runtime, args.offset, args.writer, args.schema_types);
}

absl::Status ReadAtOffset(std::string_view full_name,
                          const ArrowMessageArgs& args) {
  if (args.reader == nullptr) {
    return absl::InvalidArgumentError("Arrow field reader is null");
  }
  absl::StatusOr<decltype(BankInfo::read_arrow_at_offset)> hook =
      ArrowHook(full_name, &BankInfo::read_arrow_at_offset, "Arrow reading");
  if (!hook.ok()) {
    return hook.status();
  }
  return (*hook)(args.runtime, args.offset, args.reader);
}

absl::Status AppendSchema(std::string_view full_name,
                         const ArrowMessageArgs& args) {
  if (args.schema_builder == nullptr) {
    return absl::InvalidArgumentError("Arrow schema builder is null");
  }
  absl::StatusOr<decltype(BankInfo::append_arrow_schema)> hook =
      ArrowHook(full_name, &BankInfo::append_arrow_schema, "Arrow schema");
  if (!hook.ok()) {
    return hook.status();
  }
  return (*hook)(args.schema_builder, args.field_name, args.nullable,
                 args.schema_types);
}

absl::Status RegisterBuiltinArrowOps() {
  static const ArrowPluginOp kOps[] = {
      {kArrowPluginCollectTypes.data(), CollectTypes},
      {kArrowPluginWrite.data(), WriteAtOffset},
      {kArrowPluginRead.data(), ReadAtOffset},
      {kArrowPluginAppendSchema.data(), AppendSchema},
  };
  return RegisterArrowPlugin(absl::MakeConstSpan(kOps));
}

std::vector<std::string> ArrowPluginOperationNames() {
  static const absl::Status builtins = RegisterBuiltinArrowOps();
  (void)builtins;
  std::vector<std::string> names;
  names.reserve(ArrowOps().size());
  for (const auto& [name, unused] : ArrowOps()) {
    (void)unused;
    names.push_back(name);
  }
  return names;
}

}  // namespace

std::vector<std::string> ListPluginOperations() {
  return ArrowPluginOperationNames();
}

absl::Status RegisterArrowPlugin(absl::Span<const ArrowPluginOp> operations) {
  for (const ArrowPluginOp& operation : operations) {
    if (operation.name == nullptr || operation.name[0] == '\0') {
      return absl::InvalidArgumentError("Arrow plugin operation name is empty");
    }
    if (operation.invoke == nullptr) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Arrow plugin operation %s has no function", operation.name));
    }
    const bool inserted =
        ArrowOps().emplace(operation.name, operation.invoke).second;
    if (!inserted) {
      return absl::AlreadyExistsError(absl::StrFormat(
          "Arrow plugin operation %s is already registered", operation.name));
    }
  }
  return absl::OkStatus();
}

absl::Status CallArrowPlugin(std::string_view operation,
                             std::string_view full_name,
                             const ArrowMessageArgs& args) {
  static const absl::Status builtins = RegisterBuiltinArrowOps();
  if (!builtins.ok()) {
    return builtins;
  }
  const auto it = ArrowOps().find(std::string(operation));
  if (it == ArrowOps().end()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Arrow plugin has no operation %s", operation));
  }
  return it->second(full_name, args);
}

absl::Status CollectAny(const AnyMessage& any, TypeSet* types) {
  if (!any.has_value() || any.value().data() == nullptr) {
    return absl::OkStatus();
  }
  const std::string type(any.MessageTypeName());
  if (type.empty()) {
    return absl::InvalidArgumentError("Any value is missing a message type");
  }
  types->Add(type);
  absl::StatusOr<::phaser::BufferOffset> offset = any.EmbeddedValueOffset();
  if (!offset.ok()) {
    return offset.status();
  }
  return CallArrowPlugin(kArrowPluginCollectTypes, type,
                         ArrowMessageArgs{.runtime = any.runtime,
                                          .offset = *offset,
                                          .types = types});
}

absl::Status WriteAny(const AnyMessage& any, FieldWriter& writer,
                      const TypeSet& types) {
  if (!any.has_type_url()) {
    if (absl::Status status = writer.Child(0).Null(); !status.ok()) {
      return status;
    }
  } else if (absl::Status status = writer.Child(0).String(any.type_url());
             !status.ok()) {
    return status;
  }
  if (!any.has_value()) {
    if (absl::Status status = writer.Child(1).NullUnion(); !status.ok()) {
      return status;
    }
  } else {
    const std::string type(any.MessageTypeName());
    const int index = types.IndexOf(type);
    if (index < 0) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Any payload type '%s' was not part of the Arrow schema", type));
    }
    FieldWriter arm = writer.Child(1).UnionChild(index + 1);
    absl::StatusOr<::phaser::BufferOffset> offset = any.EmbeddedValueOffset();
    if (!offset.ok()) {
      return offset.status();
    }
    if (absl::Status status = CallArrowPlugin(
            kArrowPluginWrite, type,
            ArrowMessageArgs{.runtime = any.runtime,
                             .offset = *offset,
                             .writer = &arm,
                             .schema_types = &types});
        !status.ok()) {
      return status;
    }
  }
  return writer.FinishStruct();
}

absl::Status ReadAny(AnyMessage* any, const FieldReader& row) {
  any->Clear();
  const FieldReader url = row.Child(0);
  const FieldReader value = row.Child(1);
  if (absl::Status status = url.status(); !status.ok()) {
    return status;
  }
  if (absl::Status status = value.status(); !status.ok()) {
    return status;
  }
  std::string type_url;
  if (!url.IsNull()) {
    absl::StatusOr<std::string_view> text = url.String();
    if (!text.ok()) {
      return text.status();
    }
    type_url.assign(text->data(), text->size());
  }
  const int type_id = value.UnionType();
  if (type_id < 0) {
    return absl::InvalidArgumentError("Any value is not an Arrow union");
  }
  if (type_id == 0) {
    if (!type_url.empty()) {
      any->set_type_url(type_url);
    }
    return absl::OkStatus();
  }
  absl::StatusOr<std::string_view> child_name = value.UnionChildName();
  if (!child_name.ok()) {
    return child_name.status();
  }
  const std::string type_name(child_name->data(), child_name->size());
  if (type_url.empty()) {
    type_url = std::string("type.googleapis.com/") + type_name;
  }
  ::phaser::BufferOffset offset = 0;
  if (absl::Status status =
          any->AllocateEmbeddedForArrow(type_url, type_name, &offset);
      !status.ok()) {
    return status;
  }
  const FieldReader payload = value.UnionValue();
  return CallArrowPlugin(kArrowPluginRead, type_name,
                         ArrowMessageArgs{.runtime = any->runtime,
                                          .offset = offset,
                                          .reader = &payload});
}

}  // namespace arrow
}  // namespace phaser
