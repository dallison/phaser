#include "phaser/testdata/test_message_c_phaser/phaser/testdata/TestMessage.phaser.c.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int contains(PhaserBytes text, const char* needle) {
  const size_t n = strlen(needle);
  if (text.data == NULL || text.size < n) {
    return 0;
  }
  for (size_t i = 0; i + n <= text.size; ++i) {
    if (memcmp(text.data + i, needle, n) == 0) {
      return 1;
    }
  }
  return 0;
}

static int bytes_eq(PhaserBytes got, const uint8_t* want, size_t len) {
  return got.size == len && (len == 0 || memcmp(got.data, want, len) == 0);
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);             \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main(void) {
  phaser_foo_bar_TestMessage* msg = phaser_foo_bar_TestMessage_new(8192);
  CHECK(msg != NULL);
  CHECK(!phaser_foo_bar_TestMessage_has_x(msg));
  phaser_foo_bar_TestMessage_set_x(msg, 0);
  CHECK(phaser_foo_bar_TestMessage_has_x(msg));
  CHECK(phaser_foo_bar_TestMessage_x(msg) == 0);
  phaser_foo_bar_TestMessage_clear_x(msg);
  CHECK(!phaser_foo_bar_TestMessage_has_x(msg));

  phaser_foo_bar_TestMessage_set_y(msg, -5);
  phaser_foo_bar_TestMessage_set_s(msg, (const uint8_t*)"hi", 2);
  const uint8_t raw_buf[] = {'a', 0, 'b'};
  phaser_foo_bar_TestMessage_set_buffer(msg, raw_buf, 3);
  phaser_foo_bar_TestMessage_set_e(msg, phaser_foo_bar_EnumTest_FOO);
  phaser_foo_bar_TestMessage_set_fl(msg, 1.5f);
  phaser_foo_bar_TestMessage_set_db(msg, 2.5);
  CHECK(phaser_foo_bar_TestMessage_y(msg) == -5);
  PhaserBytes s = phaser_foo_bar_TestMessage_s(msg);
  CHECK(bytes_eq(s, (const uint8_t*)"hi", 2));
  phaser_free(s.data);
  PhaserBytes buffer = phaser_foo_bar_TestMessage_buffer(msg);
  CHECK(bytes_eq(buffer, raw_buf, 3));
  phaser_free(buffer.data);
  CHECK(phaser_foo_bar_TestMessage_e(msg) == phaser_foo_bar_EnumTest_FOO);
  CHECK(phaser_foo_bar_TestMessage_has_e(msg));
  CHECK(phaser_foo_bar_TestMessage_fl(msg) == 1.5f);
  CHECK(phaser_foo_bar_TestMessage_db(msg) == 2.5);

  phaser_foo_bar_TestMessage_add_vi32(msg, 3);
  phaser_foo_bar_TestMessage_add_vi32(msg, 4);
  phaser_foo_bar_TestMessage_set_vi32(msg, 1, 9);
  CHECK(phaser_foo_bar_TestMessage_vi32_size(msg) == 2);
  CHECK(phaser_foo_bar_TestMessage_vi32(msg, 0) == 3);
  CHECK(phaser_foo_bar_TestMessage_vi32(msg, 1) == 9);
  phaser_foo_bar_TestMessage_add_vstr(msg, (const uint8_t*)"ab", 2);
  phaser_foo_bar_TestMessage_set_vstr(msg, 0, (const uint8_t*)"z", 1);
  s = phaser_foo_bar_TestMessage_vstr(msg, 0);
  CHECK(bytes_eq(s, (const uint8_t*)"z", 1));
  phaser_free(s.data);

  phaser_foo_bar_InnerMessage* nested = phaser_foo_bar_TestMessage_mutable_m(msg);
  CHECK(nested != NULL);
  phaser_foo_bar_InnerMessage_set_str(nested, (const uint8_t*)"nested", 6);
  phaser_foo_bar_InnerMessage_set_f(nested, -2);
  phaser_foo_bar_InnerMessage_add_ev(nested, phaser_foo_bar_EnumTest_BAR);
  phaser_foo_bar_InnerMessage_free(nested);
  CHECK(phaser_foo_bar_TestMessage_has_m(msg));
  nested = phaser_foo_bar_TestMessage_m(msg);
  s = phaser_foo_bar_InnerMessage_str(nested);
  CHECK(bytes_eq(s, (const uint8_t*)"nested", 6));
  phaser_free(s.data);
  CHECK(phaser_foo_bar_InnerMessage_f(nested) == -2);
  CHECK(phaser_foo_bar_InnerMessage_ev(nested, 0) == phaser_foo_bar_EnumTest_BAR);
  phaser_foo_bar_InnerMessage_free(nested);

  phaser_foo_bar_InnerMessage* child = phaser_foo_bar_TestMessage_add_vm(msg);
  phaser_foo_bar_InnerMessage_set_str(child, (const uint8_t*)"e0", 2);
  phaser_foo_bar_InnerMessage_free(child);
  child = phaser_foo_bar_TestMessage_mutable_vm(msg, 0);
  phaser_foo_bar_InnerMessage_set_str(child, (const uint8_t*)"e1", 2);
  phaser_foo_bar_InnerMessage_free(child);
  CHECK(phaser_foo_bar_TestMessage_vm_size(msg) == 1);
  child = phaser_foo_bar_TestMessage_vm(msg, 0);
  s = phaser_foo_bar_InnerMessage_str(child);
  CHECK(bytes_eq(s, (const uint8_t*)"e1", 2));
  phaser_free(s.data);
  phaser_foo_bar_InnerMessage_free(child);

  CHECK(phaser_foo_bar_TestMessage_u1_case(msg) == 0);
  phaser_foo_bar_TestMessage_set_u1a(msg, 7);
  CHECK(phaser_foo_bar_TestMessage_u1_case(msg) == 107);
  CHECK(phaser_foo_bar_TestMessage_u1a(msg) == 7);
  CHECK(phaser_foo_bar_TestMessage_has_u1a(msg));
  phaser_foo_bar_TestMessage_set_u1b(msg, 8);
  CHECK(phaser_foo_bar_TestMessage_u1_case(msg) == 108);
  CHECK(!phaser_foo_bar_TestMessage_has_u1a(msg));
  CHECK(phaser_foo_bar_TestMessage_u1b(msg) == 8);

  phaser_foo_bar_TestMessage_set_u2b(msg, (const uint8_t*)"zz", 2);
  CHECK(phaser_foo_bar_TestMessage_u2_case(msg) == 110);
  s = phaser_foo_bar_TestMessage_u2b(msg);
  CHECK(bytes_eq(s, (const uint8_t*)"zz", 2));
  phaser_free(s.data);
  phaser_foo_bar_TestMessage_set_u2a(msg, 5);
  CHECK(phaser_foo_bar_TestMessage_u2_case(msg) == 109);
  CHECK(!phaser_foo_bar_TestMessage_has_u2b(msg));
  CHECK(phaser_foo_bar_TestMessage_u2a(msg) == 5);

  phaser_foo_bar_InnerMessage* arm = phaser_foo_bar_TestMessage_mutable_u3b(msg);
  phaser_foo_bar_InnerMessage_set_str(arm, (const uint8_t*)"arm", 3);
  phaser_foo_bar_InnerMessage_free(arm);
  CHECK(phaser_foo_bar_TestMessage_u3_case(msg) == 112);
  arm = phaser_foo_bar_TestMessage_u3b(msg);
  s = phaser_foo_bar_InnerMessage_str(arm);
  CHECK(bytes_eq(s, (const uint8_t*)"arm", 3));
  phaser_free(s.data);
  phaser_foo_bar_InnerMessage_free(arm);

  phaser_foo_bar_TestMessage_ValuesEntry* entry =
      phaser_foo_bar_TestMessage_add_values(msg);
  phaser_foo_bar_TestMessage_ValuesEntry_set_key(entry, (const uint8_t*)"k", 1);
  phaser_foo_bar_TestMessage_ValuesEntry_set_value(entry, 3);
  phaser_foo_bar_TestMessage_ValuesEntry_free(entry);
  CHECK(phaser_foo_bar_TestMessage_values_size(msg) == 1);
  entry = phaser_foo_bar_TestMessage_values(msg, 0);
  s = phaser_foo_bar_TestMessage_ValuesEntry_key(entry);
  CHECK(bytes_eq(s, (const uint8_t*)"k", 1));
  phaser_free(s.data);
  CHECK(phaser_foo_bar_TestMessage_ValuesEntry_value(entry) == 3);
  phaser_foo_bar_TestMessage_ValuesEntry_free(entry);

  CHECK(!phaser_foo_bar_TestMessage_has_any(msg));
  const char* type_url = "type.googleapis.com/foo.bar.InnerMessage";
  phaser_AnyMessage* packed = phaser_foo_bar_TestMessage_mutable_any(msg);
  phaser_AnyMessage_set_type_url(packed, (const uint8_t*)type_url, strlen(type_url));
  const uint8_t any_value[] = {0, 1};
  phaser_AnyMessage_set_value(packed, any_value, 2);
  CHECK(phaser_foo_bar_TestMessage_has_any(msg));
  s = phaser_AnyMessage_type_url(packed);
  CHECK(bytes_eq(s, (const uint8_t*)type_url, strlen(type_url)));
  phaser_free(s.data);
  buffer = phaser_AnyMessage_value(packed);
  CHECK(bytes_eq(buffer, any_value, 2));
  phaser_free(buffer.data);
  phaser_AnyMessage_free(packed);

  CHECK(phaser_foo_bar_TestMessage_byte_size_long(msg) > 0);
  PhaserBytes debug = phaser_foo_bar_TestMessage_debug_string(msg);
  CHECK(contains(debug, "nested"));
  phaser_free(debug.data);
  PhaserBytes blob = phaser_foo_bar_TestMessage_serialize_as_string(msg);
  phaser_foo_bar_TestMessage* copy = phaser_foo_bar_TestMessage_new(1024);
  CHECK(phaser_foo_bar_TestMessage_parse_from_string(copy, blob.data, blob.size));
  phaser_free(blob.data);
  s = phaser_foo_bar_TestMessage_s(copy);
  CHECK(bytes_eq(s, (const uint8_t*)"hi", 2));
  phaser_free(s.data);
  buffer = phaser_foo_bar_TestMessage_buffer(copy);
  CHECK(bytes_eq(buffer, raw_buf, 3));
  phaser_free(buffer.data);
  CHECK(phaser_foo_bar_TestMessage_e(copy) == phaser_foo_bar_EnumTest_FOO);
  nested = phaser_foo_bar_TestMessage_m(copy);
  s = phaser_foo_bar_InnerMessage_str(nested);
  CHECK(bytes_eq(s, (const uint8_t*)"nested", 6));
  phaser_free(s.data);
  phaser_foo_bar_InnerMessage_free(nested);
  child = phaser_foo_bar_TestMessage_vm(copy, 0);
  s = phaser_foo_bar_InnerMessage_str(child);
  CHECK(bytes_eq(s, (const uint8_t*)"e1", 2));
  phaser_free(s.data);
  phaser_foo_bar_InnerMessage_free(child);
  entry = phaser_foo_bar_TestMessage_values(copy, 0);
  s = phaser_foo_bar_TestMessage_ValuesEntry_key(entry);
  CHECK(bytes_eq(s, (const uint8_t*)"k", 1));
  phaser_free(s.data);
  phaser_foo_bar_TestMessage_ValuesEntry_free(entry);
  packed = phaser_foo_bar_TestMessage_any(copy);
  s = phaser_AnyMessage_type_url(packed);
  CHECK(bytes_eq(s, (const uint8_t*)type_url, strlen(type_url)));
  phaser_free(s.data);
  phaser_AnyMessage_free(packed);
  CHECK(phaser_foo_bar_TestMessage_byte_size_long(copy) > 0);
  phaser_foo_bar_TestMessage_clear(copy);
  CHECK(!phaser_foo_bar_TestMessage_has_s(copy));
  CHECK(phaser_foo_bar_TestMessage_vstr_size(copy) == 0);
  CHECK(!phaser_foo_bar_TestMessage_has_any(copy));

  phaser_foo_bar_TestMessage_free(copy);
  phaser_foo_bar_TestMessage_free(msg);
  return 0;
}
