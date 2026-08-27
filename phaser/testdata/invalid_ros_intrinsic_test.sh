#!/usr/bin/env bash
set -euo pipefail

protoc="$1"
plugin="$2"
repeated_header_proto="$3"
oneof_proto="$4"
out_dir="$(mktemp -d)"
trap 'rm -rf "${out_dir}"' EXIT

root="${TEST_SRCDIR}/${TEST_WORKSPACE}"
proto_include="${TEST_SRCDIR}/protobuf+/src"

# A repeated time or duration is supported; these two remain rejected.
check_rejected() {
  local proto="$1"
  local expected="$2"
  local err="${out_dir}/err.txt"
  if "${protoc}" \
    --plugin="protoc-gen-phaser=${plugin}" \
    -I"${root}" \
    -I"${proto_include}" \
    --phaser_out="frontend=ros,add_namespace=phaser:${out_dir}" \
    "${proto}" 2>"${err}"; then
    echo "expected phaser plugin to reject ${proto}" >&2
    exit 1
  fi
  if ! grep -q "${expected}" "${err}"; then
    echo "expected ${proto} to be rejected with '${expected}', got:" >&2
    cat "${err}" >&2
    exit 1
  fi
}

check_rejected "${repeated_header_proto}" "does not support a repeated Header"
check_rejected "${oneof_proto}" "cannot be in a oneof"
