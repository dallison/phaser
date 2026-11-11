#!/usr/bin/env bash
# Cross-build Phaser for 32-bit ARM Linux (armhf) inside Colima.
#
# Colima only hosts aarch64 and x86_64 VMs, and Bazel does not ship a 32-bit
# ARM binary, so this uses an Ubuntu container with gcc-arm-linux-gnueabihf and
# Bazel's @linux_arm32_toolchain.
#
# Usage, from the repo root:
#   scripts/build-armhf-colima.sh
#   scripts/build-armhf-colima.sh --test
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

if ! colima status >/dev/null 2>&1; then
  echo "Starting Colima (aarch64)..."
  colima start --profile default --arch aarch64 --cpu 6 --memory 12 --disk 80 --vm-type vz
fi

docker build -t phaser-arm32-builder -f docker/arm32/Dockerfile docker/arm32

# Cross-compile the runtime for the device. The phaser protoc plugin stays on the
# host (build without --config=linux_arm32).
build_targets="//phaser/runtime:phaser_runtime"
test_targets="//phaser:arm32_platform_test //phaser/runtime:payload_buffer_test //phaser/runtime:md5_test"

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e USER=builder \
  -v "$root:/src" \
  -w /src \
  phaser-arm32-builder \
  bash -eo pipefail -c "
    set -euo pipefail
    export PATH=/usr/local/bin:\$PATH
    bazel build --config=linux_arm32 ${build_targets}
    readelf -h bazel-bin/phaser/runtime/libphaser_runtime.so | grep -E 'Class:|Machine:'
    if [[ ${run_tests} -eq 1 ]]; then
      bazel test --config=linux_arm32 ${test_targets}
    fi
  "
