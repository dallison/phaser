#!/usr/bin/env bash
set -euo pipefail

phaser_tool="$1"
protobuf_tool="$2"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

phaser_wire="${tmp_dir}/phaser.wire"
protobuf_wire="${tmp_dir}/protobuf.wire"

"${phaser_tool}" write "${phaser_wire}"
"${protobuf_tool}" verify "${phaser_wire}"

"${protobuf_tool}" write "${protobuf_wire}"
"${phaser_tool}" verify "${protobuf_wire}"
