#!/usr/bin/env bash
set -euo pipefail

protoc="$1"
plugin="$2"
invalid_proto="$3"
out_dir="$(mktemp -d)"
trap 'rm -rf "${out_dir}"' EXIT

root="${TEST_SRCDIR}/${TEST_WORKSPACE}"
proto_include="${TEST_SRCDIR}/protobuf+/src"
if ! "${protoc}" \
  --plugin="protoc-gen-phaser=${plugin}" \
  -I"${root}" \
  -I"${proto_include}" \
  --phaser_out="frontend=ros,add_namespace=phaser:${out_dir}" \
  "${invalid_proto}" 2>"${out_dir}/err.txt"; then
  grep -q "must be singular and cannot be in a oneof" "${out_dir}/err.txt"
  exit 0
fi

echo "expected phaser plugin to reject repeated ROS intrinsic" >&2
exit 1
