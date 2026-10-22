#include "toolbelt/clock.h"
#include "toolbelt/hexdump.h"
#include "phaser/runtime/payload_buffer.h"
#include <cstddef>
#include <gtest/gtest.h>
#include <sstream>

using PayloadBuffer = ::phaser::PayloadBuffer;
using BufferOffset = ::phaser::BufferOffset;
using VectorHeader = ::phaser::VectorHeader;
using Resizer = ::phaser::Resizer;

TEST(BufferTest, Simple) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr = PayloadBuffer::Allocate(&pb, 32);
  memset(addr, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, TwoAllocs) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr = PayloadBuffer::Allocate(&pb, 32);
  memset(addr, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  addr = PayloadBuffer::Allocate(&pb, 64);
  memset(addr, 0xda, 64);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, Free) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr1 = PayloadBuffer::Allocate(&pb, 32);
  memset(addr1, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr1 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  void *addr2 = PayloadBuffer::Allocate(&pb, 64);
  memset(addr2, 0xda, 64);

  pb->Free(addr1);

  pb->Dump(std::cout);
  std::cout << "Allocated " << addr2 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, FreeThenAlloc) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr1 = PayloadBuffer::Allocate(&pb, 32);
  memset(addr1, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr1 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  void *addr2 = PayloadBuffer::Allocate(&pb, 64);
  memset(addr2, 0xda, 64);

  pb->Free(addr1);

  // 20 bytes fits into the free block.
  void *addr3 = PayloadBuffer::Allocate(&pb, 20);
  memset(addr3, 0xda, 20);

  pb->Dump(std::cout);
  std::cout << "Allocated " << addr2 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

// A block that grows into the free block below it moves down.  The free
// blocks below leave 8 bytes, the smallest free block, and nothing.
TEST(BufferTest, ReallocIntoFreeBlockBelow) {
  for (const uint32_t below : {256u, 248u}) {
    char *buffer = (char *)calloc(1, 4096);
    PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
    void *lower = PayloadBuffer::Allocate(&pb, below, true, false);
    auto *block =
        static_cast<uint8_t *>(PayloadBuffer::Allocate(&pb, 256, true, false));
    auto *upper =
        static_cast<uint8_t *>(PayloadBuffer::Allocate(&pb, 256, true, false));
    for (uint32_t i = 0; i < 256; ++i) {
      block[i] = static_cast<uint8_t>(i);
    }
    memset(upper, 0xc5, 256);
    const uint64_t upper_size = *(reinterpret_cast<uint64_t *>(upper) - 1);
    pb->Free(lower);

    auto *grown = static_cast<uint8_t *>(
        PayloadBuffer::Realloc(&pb, block, 512, true, false));
    ASSERT_EQ(grown, lower) << below;
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT_EQ(grown[i], i) << below;
    }
    for (uint32_t i = 256; i < 512; ++i) {
      ASSERT_EQ(grown[i], 0) << below;
    }
    EXPECT_EQ(*(reinterpret_cast<uint64_t *>(upper) - 1), upper_size) << below;
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT_EQ(upper[i], 0xc5) << below;
    }
    for (const auto *free_block = pb->FreeList(); free_block != nullptr;
         free_block =
             pb->ToAddress<::phaser::FreeBlockHeader>(free_block->next)) {
      EXPECT_GE(free_block->length, sizeof(::phaser::FreeBlockHeader)) << below;
    }
    free(buffer);
  }
}

TEST(BufferTest, SmallBlockAllocSimple) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  void *addr = PayloadBuffer::Allocate(&pb, 16);
  ASSERT_NE(nullptr, addr);
  pb->Free(addr);

  // Allocate again and make sure it's the same address.
  void *addr2 = PayloadBuffer::Allocate(&pb, 16);
  ASSERT_EQ(addr, addr2);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, SmallBlockAlloc) {
  char *buffer = (char *)calloc(1, 8192);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(8192);

  // Small block sizes are 16, 32, 64 and 128.
  // There are 16 16-byte blocks per run.
  for (int i = 0; i < 50; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 10);
    memset(addr, 0xda, 10);
  }

  // There are 8 32-byte blocks per run.
  for (int i = 0; i < 20; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 30);
    memset(addr, 0xdb, 30);
  }

  // There are 4 64-byte blocks per run.
  for (int i = 0; i < 10; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 50);
    memset(addr, 0xdc, 50);
  }

  // There are 2 128-byte blocks per run.
  for (int i = 0; i < 5; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 100);
    memset(addr, 0xdd, 100);
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, SmallBlockAllocFree) {
  char *buffer = (char *)calloc(1, 8192);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(8192);

  // Do a mix of sizes and free them.
  std::vector<void *> blocks;
  std::vector<size_t> sizes = {10, 30, 50, 100, 150};
  for (int i = 0; i < 50; i++) {
    size_t size = sizes[i % sizes.size()];
    void *addr = PayloadBuffer::Allocate(&pb, size);
    memset(addr, 0xda, size);
    blocks.push_back(addr);
  }
  // Free every 5th block.
  for (size_t i = 0; i < blocks.size(); i++) {
    if (i % 5 == 0) {
      pb->Free(blocks[i]);
    }
  }
  // Now allocate every 5th block again.
  for (size_t i = 0; i < blocks.size(); i++) {
    if (i % 5 == 0) {
      size_t size = sizes[i % sizes.size()];
      void *addr = PayloadBuffer::Allocate(&pb, size);
      memset(addr, 0xda, size);
      blocks[i] = addr;
    }
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  pb->~PayloadBuffer();
  free(buffer);
}

// This performance test compares the performance of the small block allocator
// against the regular allocator.  It is a best-case test where we are not
// stressing the small block allocator by allocating more blocks than a single
// run.  We are also not stressing the free list in the regular allocator and it
// always be taking the block from the start of the free list (no
// fragmentation).
TEST(BufferTest, BestCasePerformance) {
  constexpr int kSize = 2 * 1024 * 1024;

  double total_small = 0;
  double total_large = 0;

  constexpr int kIterations = 10000;

  for (int iter = 0; iter < kIterations; iter++) {
    char *buffer = (char *)calloc(1, kSize);
    PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 16));
    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 32));
    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 64));
    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 128));

    uint64_t small_start = toolbelt::Now();

    std::vector<void *> small_blocks;

    // 16 byte blocks.
    for (int i = 0; i < 32; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 10, false);
      small_blocks.push_back(addr);
    }

    // 32 byte blocks.
    for (int i = 0; i < 16; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 28, false);
      small_blocks.push_back(addr);
    }

    // 64 byte blocks.
    for (int i = 0; i < 8; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 60, false);
      small_blocks.push_back(addr);
    }

    // 128 byte blocks.
    for (int i = 0; i < 4; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 120, false);
      small_blocks.push_back(addr);
    }

    // Free all the blocks.
    for (auto addr : small_blocks) {
      pb->Free(addr);
    }

    uint64_t small_end = toolbelt::Now();

    // New buffer.
    free(buffer);
    buffer = (char *)calloc(1, kSize);
    pb = new (buffer) PayloadBuffer(kSize);

    // Now allocate by disabling the small block allocator.
    std::vector<void *> large_blocks;
    uint64_t large_start = toolbelt::Now();

    // 16 byte blocks.
    for (int i = 0; i < 32; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 10, false, false);
      large_blocks.push_back(addr);
    }

    // 32 byte blocks.
    for (int i = 0; i < 16; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 28, false, false);
      large_blocks.push_back(addr);
    }

    // 64 byte blocks.
    for (int i = 0; i < 8; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 60, false, false);
      large_blocks.push_back(addr);
    }

    // 128 byte blocks.
    for (int i = 0; i < 4; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 120, false, false);
      large_blocks.push_back(addr);
    }

    // Free them
    for (auto addr : large_blocks) {
      pb->Free(addr);
    }

    uint64_t large_end = toolbelt::Now();

    // Update totals.
    total_small += (small_end - small_start);
    total_large += (large_end - large_start);
    free(buffer);
  }

  std::cout << "Small block allocator: " << (total_small / kIterations) << " ns"
            << std::endl;
  std::cout << "Large block allocator: " << (total_large / kIterations) << " ns"
            << std::endl;
  // Ratio of small block allocator to large block allocator.
  std::cout << "Ratio: " << (total_large / total_small) << std::endl;
}

TEST(BufferTest, TypicalPerformance) {
  constexpr int kSize = 2 * 1024 * 1024;

  constexpr int kNumBlocks = 100;

  double total_small = 0;
  double total_large = 0;

  constexpr int kIterations = 100;
  std::vector<size_t> sizes;
  // Random sizes up to 128
  for (int i = 0; i < kNumBlocks; i++) {
    sizes.push_back((rand() % 127) + 1);
  }

  for (int iter = 0; iter < kIterations; iter++) {
    char *buffer = (char *)calloc(1, kSize);
    PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

    // No priming the small block allocator for this test.  It probably won't
    // be called in real life.

    // Allocate some small blocks (<= 128 bytes).
    std::vector<void *> small_blocks;
    uint64_t small_start = toolbelt::Now();
    for (int j = 0; j < 1000; j++) {
      int prev_size = int(small_blocks.size());
      for (int i = 0; i < kNumBlocks; i++) {
        void *addr = PayloadBuffer::Allocate(&pb, 10, false);
        small_blocks.push_back(addr);
      }
      // Free some of the blocks.
      for (size_t i = prev_size; i < small_blocks.size(); i++) {
        if (i % 8 == 0) {
          continue;
        }
        pb->Free(small_blocks[i]);
        small_blocks[i] = nullptr;
      }
    }
    uint64_t small_end = toolbelt::Now();

    total_small += (small_end - small_start);

    // New buffer.
    free(buffer);
    buffer = (char *)calloc(1, kSize);
    pb = new (buffer) PayloadBuffer(kSize);

    // Switch off small block alloctor.
    std::vector<void *> large_blocks;
    uint64_t large_start = toolbelt::Now();
    for (int j = 0; j < 1000; j++) {
      int prev_size = int(large_blocks.size());
      for (int i = 0; i < kNumBlocks; i++) {
        void *addr = PayloadBuffer::Allocate(&pb, 10, false,
                                             /*enable_small_block=*/false);
        large_blocks.push_back(addr);
      }
      // Free some of the blocks.
      for (size_t i = prev_size; i < large_blocks.size(); i++) {
        if (i % 8 == 0) {
          continue;
        }
        pb->Free(large_blocks[i]);
        large_blocks[i] = nullptr;
      }
    }
    uint64_t large_end = toolbelt::Now();
    total_large += (large_end - large_start);
    free(buffer);
  }

  std::cout << "Small block allocator: " << (total_small / kIterations) << " ns"
            << std::endl;
  std::cout << "Large block allocator: " << (total_large / kIterations) << " ns"
            << std::endl;
  std::cout << "Ratio: " << (total_large / total_small) << std::endl;
}

TEST(BufferTest, Many) {
  constexpr size_t kSize = 8192;
  char *buffer = (char *)calloc(1, kSize);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  std::vector<void *> addrs = PayloadBuffer::AllocateMany(&pb, 100, 10, true);
  ASSERT_EQ(10, addrs.size());
  // Print the addresses.
  for (auto addr : addrs) {
    std::cout << "Allocated " << addr << std::endl;
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  // Make sure we can free them.
  pb->Free(addrs[0]);
  pb->Free(addrs[2]);

  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, String) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing an offset for the string.
  PayloadBuffer::AllocateMainMessage(&pb, 32);

  void *addr = pb->ToAddress(pb->message);
  std::cout << "Messsage allocated at " << addr << std::endl;
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);
  BufferOffset offset = pb->ToOffset(addr);

  char *s = PayloadBuffer::SetString(&pb, std::string("foobar"), offset);
  std::cout << "String allocated at " << (void *)s << std::endl;

  toolbelt::Hexdump(pb, pb->hwm);

  // Now put in a bigger string, replacing the old one.
  s = PayloadBuffer::SetString(&pb, std::string("foobar has been replaced"),
                               offset);
  std::cout << "New string allocated at " << (void *)s << std::endl;

  toolbelt::Hexdump(pb, pb->hwm);

  std::string rs = pb->GetString(offset);
  ASSERT_EQ("foobar has been replaced", rs);
  free(buffer);
}

TEST(BufferTest, CopyStringCopiesExactlyTheBytes) {
  for (size_t n = 0; n <= 70; ++n) {
    std::string source;
    for (size_t i = 0; i < n; ++i) {
      source.push_back(static_cast<char>('!' + i));
    }
    std::string destination(n + 32, '#');
    phaser::internal::CopyString(destination.data() + 16, source.data(), n);
    EXPECT_EQ(destination, std::string(16, '#') + source + std::string(16, '#'))
        << n;
  }
}

TEST(BufferTest, NewStringsOfEveryShortLength) {
  constexpr size_t kMaxLength = 70;
  for (bool bitmaps : {true, false}) {
    char *buffer = (char *)calloc(1, 8192);
    PayloadBuffer *pb = new (buffer) PayloadBuffer(8192, bitmaps);
    PayloadBuffer::AllocateMainMessage(&pb,
                                       (kMaxLength + 1) * sizeof(BufferOffset));
    std::string all;
    for (size_t i = 0; i < kMaxLength; ++i) {
      all.push_back(static_cast<char>('a' + i % 26));
    }
    for (size_t n = 0; n <= kMaxLength; ++n) {
      PayloadBuffer::SetString(&pb, all.data(), n,
                               pb->message + n * sizeof(BufferOffset));
    }
    for (size_t n = 0; n <= kMaxLength; ++n) {
      EXPECT_EQ(pb->GetString(pb->message + n * sizeof(BufferOffset)),
                all.substr(0, n))
          << n << " " << bitmaps;
    }
    free(buffer);
  }
}

TEST(BufferTest, StringThatDoesNotFitLeavesTheOldValue) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  PayloadBuffer::AllocateMainMessage(&pb, 32);
  const BufferOffset offset = pb->message;
  const std::string big(8192, 'x');

  EXPECT_EQ(PayloadBuffer::SetString(&pb, big, offset), nullptr);
  EXPECT_TRUE(pb->AllocationFailed());
  EXPECT_EQ(pb->GetString(offset), "");

  // The failure sticks: a later string that would fit is not stored.
  EXPECT_EQ(PayloadBuffer::SetString(&pb, std::string("small"), offset),
            nullptr);
  EXPECT_EQ(pb->GetString(offset), "");
  EXPECT_TRUE(pb->AllocationFailed());
  free(buffer);

  buffer = (char *)calloc(1, 4096);
  pb = new (buffer) PayloadBuffer(4096);
  PayloadBuffer::AllocateMainMessage(&pb, 32);
  const BufferOffset kept = pb->message;
  EXPECT_NE(PayloadBuffer::SetString(&pb, std::string("small"), kept),
            nullptr);
  EXPECT_EQ(PayloadBuffer::SetString(&pb, big, kept), nullptr);
  EXPECT_TRUE(pb->AllocationFailed());
  EXPECT_EQ(pb->GetString(kept), "small");
  free(buffer);
}

TEST(BufferTest, HostileStringLengthIsClamped) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  PayloadBuffer::AllocateMainMessage(&pb, 32);
  BufferOffset offset = pb->message;
  PayloadBuffer::SetString(&pb, std::string("hello"), offset);

  const size_t received_size = pb->hwm;

  // Corrupt the stored length to a huge value, simulating a hostile payload.
  BufferOffset data_off = *pb->ToAddress<BufferOffset>(offset);
  uint32_t *len = pb->ToAddress<uint32_t>(data_off);
  *len = 0xffffffffu;

  // A size-aware read clamps the length to what actually fits in the received
  // buffer, so it never reads out of bounds (ASan would flag a violation).
  std::string_view sv = pb->GetStringView(offset, received_size);
  ASSERT_LE(sv.size(), received_size);
  ASSERT_EQ(0, sv.compare(0, 5, "hello"));

  // Inflating full_size must not expand what a size-aware read will accept.
  pb->full_size = 0xffffffffu;
  std::string_view sv2 = pb->GetStringView(offset, received_size);
  ASSERT_LE(sv2.size(), received_size);
  ASSERT_EQ(0, sv2.compare(0, 5, "hello"));
  free(buffer);
}

TEST(BufferTest, ToAddressRequiresFullObjectFit) {
  // A uint32_t length word whose start is inside the buffer but whose 4 bytes
  // would run past the end must not be accepted (ASan heap-buffer-overflow).
  const size_t received_size = 64;
  char *buffer = (char *)calloc(1, received_size);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(static_cast<uint32_t>(received_size));
  pb->magic = ::phaser::kFixedBufferMagic;

  // Offset where only 3 bytes remain in the trusted size.
  const BufferOffset near_end =
      static_cast<BufferOffset>(received_size - 3);
  ASSERT_EQ(nullptr, pb->ToAddress<uint32_t>(near_end, received_size));
  ASSERT_EQ(nullptr, pb->ToAddress<const uint32_t>(near_end, received_size));

  // A single-byte (void) probe at the last valid byte is OK.
  ASSERT_NE(nullptr, pb->ToAddress(received_size - 1, received_size));
  ASSERT_EQ(nullptr, pb->ToAddress(received_size, received_size));

  // GetStringView with a header whose data offset points at near_end must
  // return empty rather than reading past the end.
  BufferOffset header_off = 16;
  *pb->ToAddress<BufferOffset>(header_off, received_size) = near_end;
  std::string_view sv = pb->GetStringView(header_off, received_size);
  ASSERT_TRUE(sv.empty());
  free(buffer);
}

TEST(BufferTest, Vector) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing the VectorHeader.
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);
  std::cout << "Vector header: " << hdr << std::endl;
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0x12345678);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  uint32_t v = pb->VectorGet<uint32_t>(hdr, 0);
  ASSERT_EQ(0x12345678, v);

  free(buffer);
}

TEST(BufferTest, VectorExpand) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing the VectorHeader.
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);
  std::cout << "Vector header: " << hdr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  pb->Dump(std::cout);

  for (int i = 0; i < 3; i++) {
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i + 1);
  }
  toolbelt::Hexdump(pb, pb->hwm);

  pb->Dump(std::cout);
  for (int i = 0; i < 3; i++) {
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }

  free(buffer);
}

TEST(BufferTest, VectorExpandMore) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing the VectorHeader.
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);
  std::cout << "Vector header: " << hdr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  pb->Dump(std::cout);

  for (int i = 0; i < 100; i++) {
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i + 1);
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }
  toolbelt::Hexdump(pb, pb->hwm);

  for (int i = 0; i < 100; i++) {
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }
  pb->Dump(std::cout);

  free(buffer);
}

TEST(BufferTest, VectorPushFillsSmallBlockInPlace) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);

  // The first push asks for 8 bytes, which comes from the 16 byte class, so
  // the next three pushes fit without moving the data.
  PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 1);
  const BufferOffset data = hdr->data;
  for (uint32_t i = 2; i <= 4; i++) {
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i);
    ASSERT_EQ(data, hdr->data);
    const uint64_t size_word =
        *(pb->ToAddress<uint64_t>(hdr->data) - 1) & 0xffffffff;
    ASSERT_NE(0u, size_word & ::phaser::kSmallBlockFlag);
    ASSERT_GE(size_word & ::phaser::kBitmapRunSizeMask, i * sizeof(uint32_t));
  }

  PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 5);
  ASSERT_EQ(5u, hdr->num_elements);
  for (uint32_t i = 0; i < 5; i++) {
    ASSERT_EQ(i + 1, pb->VectorGet<uint32_t>(hdr, i));
  }

  free(buffer);
}

TEST(BufferTest, VectorPushWithResize) {
  char *buffer = (char *)calloc(256, 1);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized, &buffer](PayloadBuffer **p, size_t, size_t new_size) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        buffer = reinterpret_cast<char *>(*p);
        resized = true;
      });

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  BufferOffset msg_offset = pb->message;

  constexpr int kCount = 200;
  for (int i = 0; i < kCount; i++) {
    VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i + 1);
  }
  ASSERT_TRUE(resized);

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_EQ(kCount, hdr->num_elements);
  for (int i = 0; i < kCount; i++) {
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, VectorReserveWithResize) {
  char *buffer = (char *)calloc(256, 1);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized, &buffer](PayloadBuffer **p, size_t, size_t new_size) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        buffer = reinterpret_cast<char *>(*p);
        resized = true;
      });

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  BufferOffset msg_offset = pb->message;

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  PayloadBuffer::VectorReserve<uint32_t>(&pb, hdr, 500);
  ASSERT_TRUE(resized);

  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_NE(0u, hdr->data);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, VectorResizeWithResize) {
  char *buffer = (char *)calloc(256, 1);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized, &buffer](PayloadBuffer **p, size_t, size_t new_size) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        buffer = reinterpret_cast<char *>(*p);
        resized = true;
      });

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  BufferOffset msg_offset = pb->message;

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  PayloadBuffer::VectorResize<uint32_t>(&pb, hdr, 500);
  ASSERT_TRUE(resized);

  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_EQ(500u, hdr->num_elements);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, Resizeable) {
  char *buffer = (char *)calloc(1, 512);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized](PayloadBuffer **p, size_t, size_t new_size) {
        std::cout << "resize for " << new_size << std::endl;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        resized = true;
      });
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr = PayloadBuffer::Allocate(&pb, 130);
  ASSERT_NE(nullptr, addr);
  ASSERT_FALSE(resized);

  memset(addr, 0xda, 128);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  // This will cause a resize.
  addr = PayloadBuffer::Allocate(&pb, 256);
  ASSERT_NE(nullptr, addr);
  ASSERT_TRUE(resized);
  memset(addr, 0xdd, 128);

  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  // Don't free 'buffer' as it has already been freed by the call to realloc.
  // pb was constructed via placement-new on a malloc/realloc'd buffer, so we
  // must invoke the destructor explicitly and then free() the storage rather
  // than calling operator delete (which would be an alloc-dealloc mismatch).
  pb->~PayloadBuffer();
  free(pb);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
