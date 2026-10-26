// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// C frontend shared types. Generated message headers include this.
//
// A message pointer owns a small handle. Free it with the matching _free
// function. Freeing a handle returned by a field accessor does not free the
// parent. Free the pointer from _new last, after those handles.
// PhaserBytes owns its data. Free data with phaser_free. A null data pointer
// is an empty buffer.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PhaserBytes {
  uint8_t* data;
  size_t size;
} PhaserBytes;

void phaser_free(void* p);

typedef struct phaser_AnyMessage phaser_AnyMessage;

void phaser_AnyMessage_free(phaser_AnyMessage* msg);
void phaser_AnyMessage_clear(phaser_AnyMessage* msg);
void phaser_AnyMessage_set_type_url(phaser_AnyMessage* msg, const uint8_t* data,
                                    size_t len);
PhaserBytes phaser_AnyMessage_type_url(const phaser_AnyMessage* msg);
uint8_t phaser_AnyMessage_has_type_url(const phaser_AnyMessage* msg);
void phaser_AnyMessage_clear_type_url(phaser_AnyMessage* msg);
void phaser_AnyMessage_set_value(phaser_AnyMessage* msg, const uint8_t* data,
                                 size_t len);
PhaserBytes phaser_AnyMessage_value(const phaser_AnyMessage* msg);
uint8_t phaser_AnyMessage_has_value(const phaser_AnyMessage* msg);
void phaser_AnyMessage_clear_value(phaser_AnyMessage* msg);
PhaserBytes phaser_AnyMessage_serialize_as_string(const phaser_AnyMessage* msg);
uint8_t phaser_AnyMessage_parse_from_string(phaser_AnyMessage* msg,
                                            const uint8_t* data, size_t len);
size_t phaser_AnyMessage_byte_size_long(const phaser_AnyMessage* msg);

#ifdef __cplusplus
}  // extern "C"
#endif
