// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/runtime/md5.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace phaser {
namespace {

constexpr std::array<uint32_t, 64> kConstants = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

constexpr std::array<uint32_t, 64> kRotations = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

constexpr uint32_t RotateLeft(uint32_t value, uint32_t count) {
  return (value << count) | (value >> (32 - count));
}

uint32_t ReadLittleEndian32(const uint8_t* bytes) {
  return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) |
         (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
}

void ProcessBlock(const uint8_t* block, std::array<uint32_t, 4>* state) {
  std::array<uint32_t, 16> words;
  for (size_t i = 0; i < words.size(); ++i) {
    words[i] = ReadLittleEndian32(block + i * 4);
  }

  uint32_t a = (*state)[0];
  uint32_t b = (*state)[1];
  uint32_t c = (*state)[2];
  uint32_t d = (*state)[3];

  for (size_t i = 0; i < kConstants.size(); ++i) {
    uint32_t function = 0;
    size_t word_index = 0;
    if (i < 16) {
      function = (b & c) | (~b & d);
      word_index = i;
    } else if (i < 32) {
      function = (d & b) | (~d & c);
      word_index = (5 * i + 1) % 16;
    } else if (i < 48) {
      function = b ^ c ^ d;
      word_index = (3 * i + 5) % 16;
    } else {
      function = c ^ (b | ~d);
      word_index = (7 * i) % 16;
    }

    const uint32_t previous_d = d;
    d = c;
    c = b;
    b += RotateLeft(a + function + kConstants[i] + words[word_index],
                    kRotations[i]);
    a = previous_d;
  }

  (*state)[0] += a;
  (*state)[1] += b;
  (*state)[2] += c;
  (*state)[3] += d;
}

char HexDigit(uint8_t value) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  return kHexDigits[value];
}

}  // namespace

std::string Md5(std::string_view input) {
  std::array<uint32_t, 4> state = {
      0x67452301,
      0xefcdab89,
      0x98badcfe,
      0x10325476,
  };

  const auto* bytes = reinterpret_cast<const uint8_t*>(input.data());
  size_t offset = 0;
  while (input.size() - offset >= 64) {
    ProcessBlock(bytes + offset, &state);
    offset += 64;
  }

  std::array<uint8_t, 128> tail = {};
  const size_t remaining = input.size() - offset;
  if (remaining != 0) {
    std::memcpy(tail.data(), bytes + offset, remaining);
  }
  tail[remaining] = 0x80;

  const size_t tail_size = remaining < 56 ? 64 : 128;
  const uint64_t bit_length = uint64_t{input.size()} * 8;
  for (size_t i = 0; i < 8; ++i) {
    tail[tail_size - 8 + i] =
        static_cast<uint8_t>((bit_length >> (i * 8)) & 0xff);
  }
  ProcessBlock(tail.data(), &state);
  if (tail_size == 128) {
    ProcessBlock(tail.data() + 64, &state);
  }

  std::string digest(32, '\0');
  size_t digest_offset = 0;
  for (uint32_t word : state) {
    for (size_t i = 0; i < 4; ++i) {
      const uint8_t byte = static_cast<uint8_t>((word >> (i * 8)) & 0xff);
      digest[digest_offset++] = HexDigit(byte >> 4);
      digest[digest_offset++] = HexDigit(byte & 0x0f);
    }
  }
  return digest;
}

}  // namespace phaser
