// Copyright 2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Builds a message with the C++ frontend and reads its native payload with
// the C frontend.  Both libraries have the short header
// "phaser/testdata/Test.phaser.h", so these include the generated files.

#include <cstdint>
#include <cstdio>

#include "phaser/testdata/test_phaser/phaser/testdata/Test.phaser.h"
#include "phaser/testdata/test_phaser_c/phaser/testdata/Test.phaser.c.h"

int main() {
  foo::bar::smoke::Test message =
      foo::bar::smoke::Test::CreateDynamicMutable(1024);
  message.set_a(42);
  message.set_b("phaser");

  phaser_foo_bar_Test *parsed = phaser_foo_bar_Test_new(1024);
  const bool parsed_ok =
      phaser_foo_bar_Test_parse_native_payload(
          parsed, static_cast<const uint8_t *>(message.Data()),
          message.ByteSizeLong()) != 0;
  const int32_t a = phaser_foo_bar_Test_a(parsed);
  phaser_foo_bar_Test_free(parsed);

  if (!parsed_ok || a != 42) {
    std::fprintf(stderr, "The C frontend did not read the C++ message\n");
    return 1;
  }
  return 0;
}
