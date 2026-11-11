#!/usr/bin/env bash
# Cross-build Phaser for 32-bit ARM Linux (armhf) on Linux (CI or native Ubuntu).
#
# Requires: g++-arm-linux-gnueabihf, qemu-user-static (Debian/Ubuntu).
#
# Usage:
#   scripts/build-armhf-linux.sh
#   scripts/build-armhf-linux.sh --test
#
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

run_tests=0
if [[ "${1:-}" == "--test" ]]; then
  run_tests=1
elif [[ -n "${1:-}" ]]; then
  echo "usage: $0 [--test]" >&2
  exit 2
fi

if ! command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
  echo "arm-linux-gnueabihf-g++ not found; install g++-arm-linux-gnueabihf" >&2
  exit 1
fi

build_targets="//phaser/runtime:phaser_runtime"
test_targets=(
  "//phaser:arm32_platform_test"
  "//phaser/runtime:payload_buffer_test"
  "//phaser/runtime:md5_test"
)

bazel build --config=linux_arm32 ${build_targets}
readelf -h bazel-bin/phaser/runtime/libphaser_runtime.so | grep -E 'Class:|Machine:'

if [[ ${run_tests} -eq 1 ]]; then
  if ! command -v qemu-arm-static >/dev/null 2>&1 && ! command -v qemu-arm >/dev/null 2>&1; then
    echo "qemu-arm-static or qemu-arm required to run armhf tests" >&2
    exit 1
  fi
  bazel test --config=linux_arm32 "${test_targets[@]}"
fi
