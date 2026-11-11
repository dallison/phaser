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

if [[ ${run_tests} -eq 0 ]]; then
  build_targets="//phaser/runtime:phaser_runtime"
  bazel build --config=linux_arm32 ${build_targets}
  readelf -h bazel-bin/phaser/runtime/libphaser_runtime.so | grep -E 'Class:|Machine:'
fi

if [[ ${run_tests} -eq 1 ]]; then
  if ! command -v qemu-arm-static >/dev/null 2>&1 && ! command -v qemu-arm >/dev/null 2>&1; then
    echo "qemu-arm-static or qemu-arm required to run armhf tests" >&2
    exit 1
  fi
  # Same cc_test scope as main CI, minus perf_test and host-only tests.
  # receive_fuzz_test: libFuzzer is unreliable under qemu-user (bus error).
  exclude=(
    //phaser:perf_test
    //phaser:receive_fuzz_test
    //phaser/runtime:phaser_bank_py_test
    //phaser/testdata:phaser_python_test
    //phaser/testdata:phaser_rust_test
    //phaser/testdata:ros_intrinsics_wire_compatibility_test
    //phaser/testdata:invalid_array_size_test
    //phaser/testdata:invalid_ros_intrinsic_test
  )
  query='tests(//...)'
  for label in "${exclude[@]}"; do
    query+=" except ${label}"
  done
  mapfile -t test_labels < <(bazel query --output=label "${query}")
  bazel test --config=linux_arm32 -- "${test_labels[@]}"
fi
