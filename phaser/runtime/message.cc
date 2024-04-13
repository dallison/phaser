#include "phaser/runtime/message.h"

namespace phaser {

int32_t Message::FindFieldOffset(uint32_t field_number) const {
  if (runtime == nullptr) {
    return -1;
  }
  // First 4 bytes of message are the the offset to the field data.
  phaser::BufferOffset *field_data =
      runtime->pb->ToAddress<phaser::BufferOffset>(absolute_binary_offset);

  // Dereference offset to get a pointer to the field data (in the payload
  // buffer)l
  FieldData *fd = runtime->pb->ToAddress<FieldData>(*field_data);
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
  phaser::BufferOffset *field_data =
      runtime->pb->ToAddress<phaser::BufferOffset>(absolute_binary_offset);

  // Dereference offset to get a pointer to the field data (in the payload
  // buffer)l
  FieldData *fd = runtime->pb->ToAddress<FieldData>(*field_data);
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

} // namespace phaser
