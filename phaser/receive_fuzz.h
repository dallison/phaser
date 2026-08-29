// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <cstddef>
#include <cstdint>

// Shared entry used by the libFuzzer binary and the corpus regression test.
// Returns 0 always; ASan/UBSan report memory errors.
int PhaserReceiveFuzzOneInput(const uint8_t* data, size_t size);
