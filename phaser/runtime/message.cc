// Copyright 2024 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/message.h"

namespace phaser {

int32_t Message::FindFieldOffset(uint32_t field_number) const {
  if (runtime == nullptr) {
    return -1;
  }
  // First 4 bytes of message are the the offset to the field data.
  ::toolbelt::BufferOffset *field_data =
      runtime->ToAddress<::toolbelt::BufferOffset>(absolute_binary_offset);
  if (field_data == nullptr) {
    return -1;
  }
  // Dereference offset to get a pointer to the field data (in the payload
  // buffer)l
  FieldData *fd = runtime->ToAddress<FieldData>(*field_data);
  if (fd == nullptr) {
    return -1;
  }
  // Search for number using binary search.  This must be sorted by field
  // number.
  uint32_t left = 0;
  uint32_t right = fd->num;
  while (left < right) {
    uint32_t mid = left + (right - left) / 2;
    if (fd->fields[mid].number == field_number) {
      return int32_t(fd->fields[mid].offset);
    } else if (fd->fields[mid].number < field_number) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return -1;
}

int32_t Message::FindFieldId(uint32_t field_number) const {
  if (runtime == nullptr) {
    return -1;
  }
  // First 4 bytes of message are the the offset to the field data.
  ::toolbelt::BufferOffset *field_data =
      runtime->ToAddress<::toolbelt::BufferOffset>(absolute_binary_offset);

  if (field_data == nullptr) {
    return -1;
  }
  // Dereference offset to get a pointer to the field data (in the payload
  // buffer)l
  FieldData *fd = runtime->ToAddress<FieldData>(*field_data);
  if (fd == nullptr) {
    return -1;
  }
  // Search for number using binary search.  This must be sorted by field
  // number.
  uint32_t left = 0;
  uint32_t right = fd->num;
  while (left < right) {
    uint32_t mid = left + (right - left) / 2;
    if (fd->fields[mid].number == field_number) {
      return int32_t(fd->fields[mid].id);
    } else if (fd->fields[mid].number < field_number) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return -1;
}

::toolbelt::PayloadBuffer *NewDynamicBuffer(size_t initial_size) {
  char *buffer = (char *)malloc(initial_size);
  ::toolbelt::PayloadBuffer *pb = new (buffer)::toolbelt::PayloadBuffer(
      initial_size, [](::toolbelt::PayloadBuffer **p, size_t new_size) {
        *p = reinterpret_cast<::toolbelt::PayloadBuffer *>(realloc(*p, new_size));
      });
  return pb;
}

} // namespace phaser
