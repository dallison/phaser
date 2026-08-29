// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

// Writes a small seed corpus for receive_fuzz into the directory given as argv[1].

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "phaser/testdata/TestMessage.phaser.h"
#include "toolbelt/payload_buffer.h"

namespace {

void WriteFile(const std::string& path, const void* data, size_t size) {
  std::ofstream out(path, std::ios::binary);
  out.write(static_cast<const char*>(data),
            static_cast<std::streamsize>(size));
}

std::vector<char> ValidPayload() {
  foo::bar::phaser::TestMessage msg;
  msg.set_x(42);
  msg.set_s("seed");
  msg.add_vi32(1);
  msg.add_vi32(2);
  msg.add_vstr("a");
  msg.mutable_m()->set_str("inner");
  // Exercise oneof / union value slots in the seed corpus.
  msg.set_u3a(0x1122334455667788LL);
  const char* data = static_cast<const char*>(msg.Data());
  return std::vector<char>(data, data + msg.Size());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <out_dir>\n", argv[0]);
    return 1;
  }
  const std::string dir = argv[1];

  // Mode 0: empty / tiny / magic-only raw buffers.
  WriteFile(dir + "/empty", "", 0);
  const char tiny[] = {'\0'};
  WriteFile(dir + "/tiny", tiny, 1);
  {
    std::vector<char> buf(sizeof(toolbelt::PayloadBuffer), '\0');
    uint32_t magic = toolbelt::kFixedBufferMagic;
    std::memcpy(buf.data(), &magic, sizeof(magic));
    // Mode byte 0 = raw.
    std::vector<char> input;
    input.push_back(0);
    input.insert(input.end(), buf.begin(), buf.end());
    WriteFile(dir + "/magic_only", input.data(), input.size());
  }

  // Mode 0: a full valid payload as raw input.
  {
    auto payload = ValidPayload();
    std::vector<char> input;
    input.push_back(0);
    input.insert(input.end(), payload.begin(), payload.end());
    WriteFile(dir + "/valid_raw", input.data(), input.size());
  }

  // Mode 1: structure-aware with empty overlay (just the seed message).
  {
    char mode = 1;
    WriteFile(dir + "/mode1_empty", &mode, 1);
  }

  // Mode 1: structure-aware with a few XOR bytes.
  {
    std::vector<char> input;
    input.push_back(1);
    const uint8_t overlay[] = {0xff, 0x00, 0xaa, 0x55, 0x12, 0x34, 0x56, 0x78};
    input.insert(input.end(), overlay, overlay + sizeof(overlay));
    WriteFile(dir + "/mode1_xor", input.data(), input.size());
  }

  // Mode 0: inflate full_size on a valid payload.
  {
    auto payload = ValidPayload();
    if (payload.size() >= 16) {
      uint32_t huge = 0xffffffffu;
      std::memcpy(payload.data() + 12, &huge, sizeof(huge));
    }
    std::vector<char> input;
    input.push_back(0);
    input.insert(input.end(), payload.begin(), payload.end());
    WriteFile(dir + "/valid_inflated_full_size", input.data(), input.size());
  }

  return 0;
}
