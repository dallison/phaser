// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Avro object-container writer. Phaser does not link libavro. The file is one
// uncompressed block whose records follow the Arrow batch produced for the
// same messages.

#include <string>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/arrow.h"

struct ArrowSchema;
struct ArrowArray;

namespace phaser {
namespace avro {

absl::Status WriteFile(const struct ArrowSchema* schema,
                       const struct ArrowArray* array, std::string* out);

// Binary Avro encoding for one row of an Arrow batch (no object-container
// header). Concatenated row encodings match the block body inside WriteFile.
absl::Status WriteRecordBytes(const struct ArrowSchema* schema,
                              const struct ArrowArray* array, int64_t row,
                              std::string* out);
absl::Status WriteAllRecordBytes(const struct ArrowSchema* schema,
                                 const struct ArrowArray* array,
                                 std::string* out);

template <typename Message>
absl::Status WriteMessages(absl::Span<const Message> messages,
                           std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Avro output string is null");
  }
  out->clear();
  struct ArrowSchema schema {};
  struct ArrowArray array {};
  absl::Status status =
      ::phaser::arrow::MessagesToArrow(messages, &schema, &array);
  if (status.ok()) {
    status = WriteFile(&schema, &array, out);
  }
  if (!status.ok()) {
    out->clear();
  }
  if (schema.release != nullptr) {
    schema.release(&schema);
  }
  if (array.release != nullptr) {
    array.release(&array);
  }
  return status;
}

}  // namespace avro
}  // namespace phaser
