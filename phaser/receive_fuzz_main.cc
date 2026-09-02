// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Empty translation unit so //phaser:receive_fuzz can be a cc_binary that
// depends on :receive_fuzz_lib (which defines LLVMFuzzerTestOneInput).
// Link with --config=fuzz (-fsanitize=fuzzer) to get libFuzzer's main.
