// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

// Uncompressed Parquet writer. Phaser does not link libparquet. One row group,
// PLAIN values, and data page v1. The columns follow the Arrow batch produced
// for the same messages.

#include <string>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "phaser/runtime/arrow.h"

struct ArrowSchema;
struct ArrowArray;

namespace phaser {
namespace parquet {

absl::Status WriteFile(const struct ArrowSchema* schema,
                       const struct ArrowArray* array, std::string* out);

template <typename Message>
absl::Status WriteMessages(absl::Span<const Message> messages,
                           std::string* out) {
  if (out == nullptr) {
    return absl::InvalidArgumentError("Parquet output string is null");
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

}  // namespace parquet
}  // namespace phaser
