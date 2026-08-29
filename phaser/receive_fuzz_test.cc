// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "phaser/receive_fuzz.h"

namespace {

std::vector<std::vector<uint8_t>> LoadCorpus(const std::string& dir) {
  std::vector<std::vector<uint8_t>> inputs;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    // Only the generated corpus_* seeds (ignore other runfiles in the package).
    if (name.rfind("corpus_", 0) != 0) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    inputs.push_back(std::move(bytes));
  }
  return inputs;
}

}  // namespace

TEST(ReceiveFuzzTest, SeedCorpusDoesNotCrash) {
  const char* corpus_rel = std::getenv("TEST_CORPUS");
  ASSERT_NE(corpus_rel, nullptr);

  std::string corpus_dir = corpus_rel;
  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    std::string ws = "_main";
    if (const char* w = std::getenv("TEST_WORKSPACE")) {
      ws = w;
    }
    corpus_dir = std::string(srcdir) + "/" + ws + "/" + corpus_rel;
  }
  if (!std::filesystem::exists(corpus_dir)) {
    // Fallback: runfiles-relative path from the test cwd.
    corpus_dir = corpus_rel;
  }
  ASSERT_TRUE(std::filesystem::exists(corpus_dir)) << corpus_dir;

  auto inputs = LoadCorpus(corpus_dir);
  ASSERT_FALSE(inputs.empty());
  for (const auto& input : inputs) {
    PhaserReceiveFuzzOneInput(input.data(), input.size());
  }
}

TEST(ReceiveFuzzTest, RandomMutationsDoNotCrash) {
  // Deterministic pseudo-random walk so CI catches crashes without libFuzzer.
  std::vector<uint8_t> buf(256);
  uint32_t state = 0xC0FFEEu;
  auto rnd = [&]() -> uint8_t {
    state = state * 1664525u + 1013904223u;
    return static_cast<uint8_t>(state >> 24);
  };

  for (int iter = 0; iter < 2000; ++iter) {
    const size_t n = 1 + (rnd() % buf.size());
    buf[0] = static_cast<uint8_t>(iter & 1);  // alternate modes
    for (size_t i = 1; i < n; ++i) {
      buf[i] = rnd();
    }
    PhaserReceiveFuzzOneInput(buf.data(), n);
  }
}

TEST(ReceiveFuzzTest, KnownUnionCrashDoesNotSegfault) {
  // Regression for ASan SEGV in UnionInt64Field::Get when a hostile payload
  // inflates full_size and Get used PayloadBuffer::Get (no trusted size).
  const char* rel = "phaser/testdata/fuzz_crash_union_int64.bin";
  std::string path = rel;
  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    std::string ws = "_main";
    if (const char* w = std::getenv("TEST_WORKSPACE")) {
      ws = w;
    }
    path = std::string(srcdir) + "/" + ws + "/" + rel;
  }
  ASSERT_TRUE(std::filesystem::exists(path)) << path;
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  ASSERT_FALSE(bytes.empty());
  PhaserReceiveFuzzOneInput(bytes.data(), bytes.size());
}

TEST(ReceiveFuzzTest, KnownBoundedStringCrashDoesNotOverflow) {
  // Regression for ASan heap-buffer-overflow in BoundedString when a string
  // length word starts inside the buffer but sizeof(uint32_t) does not fit.
  const char* rel = "phaser/testdata/fuzz_crash_bounded_string.bin";
  std::string path = rel;
  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    std::string ws = "_main";
    if (const char* w = std::getenv("TEST_WORKSPACE")) {
      ws = w;
    }
    path = std::string(srcdir) + "/" + ws + "/" + rel;
  }
  ASSERT_TRUE(std::filesystem::exists(path)) << path;
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  ASSERT_FALSE(bytes.empty());
  PhaserReceiveFuzzOneInput(bytes.data(), bytes.size());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
