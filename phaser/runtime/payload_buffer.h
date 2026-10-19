#pragma once

// Phaser's own PayloadBuffer, forked from toolbelt::PayloadBuffer so that its
// allocator can change without affecting other toolbelt users.  The in-buffer
// layout (the PayloadBuffer header, magic values and the 8-byte size word
// before each block) must stay compatible with toolbelt::PayloadBuffer since
// existing code reads Phaser buffers through the toolbelt type.

#include "absl/types/span.h"
#include <functional>
#include <iostream>
#include <limits>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace phaser {

constexpr uint32_t kFixedBufferMagic = 0xe5f6f1c4;
constexpr uint32_t kMovableBufferMagic = 0xc5f6f1c4;

// Bottom bit is set for enabling bitmap allocator.
constexpr uint32_t kBitMapMask = 0xfffffffe;
constexpr uint32_t kBitMapFlag = 1;

using BufferOffset = uint32_t;

struct FreeBlockHeader {
  uint32_t length;   // Length of the free block, including the header.
  BufferOffset next; // Absolute offset into buffer for next free block.
};

// The 'data' member refers to a block of allocated memory in the
// buffer.  Since it's been allocated using Allocate, the preceding
// 8 bytes contains the length of the block in bytes (little endian).
// To get the count of the number of elements in the allocated block
// (the vector's capacity), divide this by sizeof(T) where T is the
// type of the values stored in the vector.
struct VectorHeader {
  uint32_t num_elements; // Number of populated elements.
  BufferOffset data;     // Absolute offset of vector data.
};

struct PayloadBuffer;

// A string is held in an allocated block as a 4 byte little endian
// length field, followed immediately by the string data.  The block
// length is aligned to a multiple of 4 bytes, so that can't be
// used to hold the actual length of the string.
// A StringHeader is the absolute offset into the buffer of the
// length field of the string.
using StringHeader = BufferOffset;

// The resizer function is called when the buffer needs to be expanded.
// Its responsibility is to:
// 1. Allocate memory for the new buffer with the size given.
// 2. Copy the current buffer contents to the new memory
// 3. Set *buffer to the address of the new buffer.
// 4. Freeing up the old buffer if necessary.
//
// It is not responsible for adding the new memory to the free list in
// the buffer to make it available for allocation.  If the buffer
// is allocated using malloc, the resize would be a simple call
// to realloc.  However, care must be taken to preserve the
// contents of the resizer function.
using Resizer =
    std::function<void(PayloadBuffer **, size_t old_size, size_t new_size)>;

// Small block allocator.  In order to reduce fragmentation and speed up
// allocation of small blocks, blocks of up to 128 bytes are rounded up to one
// of a fixed number of size classes.  Each size class has a LIFO free list
// (like the fastbins in common malloc implementations) whose head offset is
// held in the PayloadBuffer header ('bitmaps', a name kept for layout
// compatibility).  A free small block holds the offset of the next free block
// of its class in its first 4 bytes.  When a class's free list is empty a
// single block is carved from the main free list, so a freshly created buffer
// is filled by what is effectively bump allocation and no space is reserved
// for blocks that are never used.  Freed small blocks stay in their class and
// are not coalesced back into the main free list.

// Number of small block size classes.
inline constexpr size_t kNumBitmapRuns = 4;

// Block size for each small block class.
inline constexpr int kBitmapRunSize1 = 16;
inline constexpr int kBitmapRunSize2 = 32;
inline constexpr int kBitmapRunSize3 = 64;
inline constexpr int kBitmapRunSize4 = 128;

// The 8 bytes preceding an allocated block hold its size.  To distinguish a
// small block from a regular block allocated from the free list, a small
// block's size word has the top bit set with the requested size (at most 128)
// in bits 7-0.  The requested size always maps back to the block's size class
// because a block is always taken from the smallest class that fits.
inline constexpr uint32_t kSmallBlockFlag = 1U << 31;
inline constexpr uint32_t kBitmapRunSizeMask = 0xff;

// This is a buffer that holds the contents of a message.
// It is located at the first address of the actual buffer with the
// reset of the buffer memory following it.
//
struct PayloadBuffer {
  uint32_t magic;         // Magic to identify wireformat.
  BufferOffset message;   // Offset for the message.
  uint32_t hwm;           // Offset one beyond the highest used.
  uint32_t full_size;     // Full size of buffer.
  BufferOffset free_list; // Heap free list.
  BufferOffset metadata;  // Offset to message metadata.
  BufferOffset
      bitmaps[kNumBitmapRuns]; // Free list head for each small block class.

  // Initialize a new PayloadBuffer at this with a message of the
  // given size.  This is a fixed size buffer.
  PayloadBuffer(uint32_t size, bool bitmap_allocator = true)
      : magic(kFixedBufferMagic | (bitmap_allocator ? kBitMapFlag : 0)),
        message(0), hwm(0), full_size(size), metadata(0) {
    for (size_t i = 0; i < kNumBitmapRuns; i++) {
      bitmaps[i] = 0;
    }
    InitFreeList();
  }

  // This is a variable sized buffer with the resizer being a function to
  // allocate new memory.  A pointer to the resizer function is stored in the
  // memory immediately above the header.  We can't store the actual
  // std::function in the memory because the memory will be deleted during the
  // call to the std::function which invalidates its this pointer.  Instead we
  // copy the std::function to the heap and store a raw pointer to it.  The heap
  // copy will be destructed when the payload buffer is destructed.
  // This implies that you need to destruct the payload buffer to avoid
  // a memory leak.  This is only necessary for resizable buffers.  Fixed
  // size buffers don't need to be destructed.
  PayloadBuffer(uint32_t initial_size, Resizer r, bool bitmap_allocator = true)
      : magic(kMovableBufferMagic | (bitmap_allocator ? kBitMapFlag : 0)),
        message(0), hwm(0), full_size(initial_size), metadata(0) {
    for (size_t i = 0; i < kNumBitmapRuns; i++) {
      bitmaps[i] = 0;
    }
    InitFreeList();
    SetResizer(std::move(r));
  }

  ~PayloadBuffer() {
    if (IsMoveable()) {
      // Destruct the resizer.
      Resizer **addr = reinterpret_cast<Resizer **>(this + 1);
      delete *addr;
    }
  }

  bool BitmapsEnabled() const { return (magic & kBitMapFlag) != 0; }

  void SetResizer(Resizer r) {
    // Place a pointer to the resizer function in the buffer just after the
    // header.
    Resizer **addr = reinterpret_cast<Resizer **>(this + 1);
    Resizer *on_heap = new Resizer(std::move(r)); // Moved to heap memory.
    *addr = on_heap;                              // Place address in memory.
  }

  Resizer *GetResizer() {
    if (!IsMoveable()) {
      return nullptr;
    }
    return *reinterpret_cast<Resizer **>(this + 1);
  }

  size_t Size() const { return size_t(hwm); }

  // Allocate space for the main message in the buffer and set the
  // 'message' field to its offset.
  static void *AllocateMainMessage(PayloadBuffer **self, size_t size);

  // Allocate space for the message metadata and copy it in.
  static void AllocateMetadata(PayloadBuffer **self, void *md, size_t size);
  // The small block allocator has no per-class setup cost so there is nothing
  // to prime.  Always returns true.
  static bool PrimeBitmapAllocator(PayloadBuffer **self, size_t size);
  void SetPresenceBit(uint32_t bit, uint32_t offset) {
    uint32_t word = bit / 32;
    bit %= 32;
    uint32_t *p = ToAddress<uint32_t>(offset);
    p[word] |= (1 << bit);
  }

  void ClearPresenceBit(uint32_t bit, uint32_t offset) {
    uint32_t word = bit / 32;
    bit %= 32;
    uint32_t *p = ToAddress<uint32_t>(offset);
    p[word] &= ~(1 << bit);
  }

  bool IsPresent(uint32_t bit, uint32_t offset) const {
    // Presence bits are packed 32 to a word; this must match SetPresenceBit /
    // ClearPresenceBit which use 32-bit words.  Validate the specific word we
    // read so a hostile field id cannot walk off the end of the buffer.
    uint32_t word = bit / 32;
    bit %= 32;
    const uint32_t *p = ToAddress<const uint32_t>(
        offset + word * static_cast<uint32_t>(sizeof(uint32_t)));
    if (p == nullptr) {
      return false;
    }
    return (*p & (1U << bit)) != 0;
  }

  static uint32_t DecodeSize(BufferOffset *addr) {
    // Length is 64 bits long but we only need the bottom 32 bits of it.
    uint32_t *p = reinterpret_cast<uint32_t *>(addr) - 2;
    if ((*p & kSmallBlockFlag) == 0) {
      return *p;
    }
    return *p & kBitmapRunSizeMask;
  }

  template <typename MessageType>
  static MessageType *NewMessage(PayloadBuffer **self, uint32_t size,
                                 BufferOffset offset);

  // Allocate space for the string.
  // 'header_offset' is the offset into the buffer of the StringHeader.
  // The string is copied in.

  // C-string style (allows for no allocation of std::string).
  static char *SetString(PayloadBuffer **self, const char *s, size_t len,
                         BufferOffset header_offset);

  template <typename Str>
  static char *SetString(PayloadBuffer **self, Str s,
                         BufferOffset header_offset) {
    return SetString(self, s.data(), s.size(), header_offset);
  }

  static void ClearString(PayloadBuffer **self, BufferOffset header_offset);

  static absl::Span<char> AllocateString(PayloadBuffer **self, size_t len,
                                         BufferOffset header_offset,
                                         bool clear = false);

  bool IsNull(BufferOffset offset) {
    BufferOffset *p = ToAddress<BufferOffset>(offset);
    return *p == 0;
  }

  template <typename T> void Set(BufferOffset offset, T v);
  template <typename T> T &Get(BufferOffset offset);

  template <typename T>
  static void VectorPush(PayloadBuffer **self, VectorHeader *hdr, T v,
                         bool enable_small_block = true);

  template <typename T>
  static void VectorReserve(PayloadBuffer **self, VectorHeader *hdr, size_t n,
                            bool enable_small_block = true);

  template <typename T>
  static void VectorResize(PayloadBuffer **self, VectorHeader *hdr, size_t n);

  template <typename T>
  static void VectorClear(PayloadBuffer **self, VectorHeader *hdr);

  // 'header_offset' is the offset into the buffer StringHeader.
  std::string GetString(BufferOffset header_offset) const {
    return GetString(ToAddress<const BufferOffset>(header_offset));
  }
  std::string_view GetStringView(BufferOffset header_offset) const {
    return GetStringView(ToAddress<const BufferOffset>(header_offset));
  }
  size_t StringSize(BufferOffset header_offset) const {
    return StringSize(ToAddress<const BufferOffset>(header_offset));
  }

  const char *StringData(BufferOffset header_offset) const {
    return StringData(ToAddress<const BufferOffset>(header_offset));
  }

  std::string GetString(const StringHeader *addr) const;
  std::string_view GetStringView(const StringHeader *addr) const;
  size_t StringSize(const StringHeader *addr) const;
  const char *StringData(const StringHeader *addr) const;

  // Size-aware string readers.  'buffer_size' is the number of bytes actually
  // known to be available at 'this'.  A value of 0 means "trust full_size" and
  // must only be used for buffers we own and built ourselves.  When reading
  // data received from an untrusted source, pass the real received size so the
  // stored string length cannot be used to read out of bounds: both the
  // location of the length word and the returned length are validated against
  // 'buffer_size'.
  std::string GetString(BufferOffset header_offset, size_t buffer_size) const;
  std::string_view GetStringView(BufferOffset header_offset,
                                 size_t buffer_size) const;
  size_t StringSize(BufferOffset header_offset, size_t buffer_size) const;
  const char *StringData(BufferOffset header_offset, size_t buffer_size) const;

  template <typename T>
  T VectorGet(const VectorHeader *hdr, size_t index) const;

  void Dump(std::ostream &os);
  void DumpFreeList(std::ostream &os);
  void CheckFreeList();

  void InitFreeList();
  FreeBlockHeader *FreeList() { return ToAddress<FreeBlockHeader>(free_list); }

  // Allocate some memory in the buffer.  The buffer might move.  The common
  // cases (reusing a free small block, or carving from the start of the first
  // free block) are handled inline; everything else goes to AllocateSlow.
  static void *Allocate(PayloadBuffer **buffer, uint32_t n, bool clear = true,
                        bool enable_small_block = true);
  static void *AllocateSlow(PayloadBuffer **buffer, uint32_t n, bool clear,
                            bool enable_small_block);
  void Free(void *p);
  void FreeSlow(void *p);

  // Index of the small block class for an allocation of 'n' bytes, or -1 if
  // it is too big for a small block.
  static int SmallBlockIndex(uint32_t n) {
    return n <= uint32_t(kBitmapRunSize1)   ? 0
           : n <= uint32_t(kBitmapRunSize2) ? 1
           : n <= uint32_t(kBitmapRunSize3) ? 2
           : n <= uint32_t(kBitmapRunSize4) ? 3
                                             : -1;
  }
  static uint32_t SmallBlockSize(int index) { return 16U << index; }
  static void *Realloc(PayloadBuffer **buffer, void *p, uint32_t n,
                       bool clear = true, bool enable_small_block = true);
  static void *AllocateSmallBlock(PayloadBuffer **pb, uint32_t size, int index,
                                  bool clear = true);
  static void FreeSmallBlock(PayloadBuffer *pb, void *p, int index);

  // Allocate 'n' items of size 'size' in the buffer.  The buffer might move.
  // Each allocation is capable of being freed
  // individually. The addresses of the allocated items are returned in a
  // vector.
  static std::vector<void *> AllocateMany(PayloadBuffer **buffer, uint32_t size,
                                          uint32_t n, bool clear = true);

  bool IsValidMagic() const {
    uint32_t m = magic & kBitMapMask;
    return m == kFixedBufferMagic || m == kMovableBufferMagic;
  }
  bool IsMoveable() const {
    return (magic & kBitMapMask) == kMovableBufferMagic;
  }

  // Returns true if [addr, addr + nbytes) lies entirely within the first
  // 'size' bytes of this buffer (or full_size when size == 0).
  bool IsValidAddress(const void *addr, size_t size,
                      size_t nbytes = 1) const {
    if (nbytes == 0) {
      return false;
    }
    if (size == 0) {
      size = full_size;
    }
    const char *base = reinterpret_cast<const char *>(this);
    const char *a = reinterpret_cast<const char *>(addr);
    if (a < base) {
      return false;
    }
    // Compute the offset without forming an out-of-range end pointer (which
    // would be undefined behavior for a hostile 'size').
    const size_t off = static_cast<size_t>(a - base);
    if (off >= size) {
      return false;
    }
    // Require the full object (nbytes) to fit; start-only checks let a
    // uint32_t length word at size-1 through and then heap-overflow on read.
    return nbytes <= size - off;
  }

  // Given the address of a block, return the size of the block.  This is
  // in the previous 8 bytes but might be encoded if the block is a small
  // block.
  static uint32_t DecodedSize(BufferOffset *addr) {
    BufferOffset *p = addr - 2;
    if (*p & kSmallBlockFlag) {
      return *p & kBitmapRunSizeMask;
    }
    return *p;
  }

  template <typename T = void>
  T *ToAddress(BufferOffset offset, size_t size = 0) {
    if (offset == 0) {
      return nullptr;
    }
    if (!IsValidMagic()) {
      return nullptr;
    }
    // void* probes a single byte; typed pointers must fit sizeof(T).
    // Use conditional_t so sizeof(void) is never evaluated.
    constexpr size_t kN =
        sizeof(typename std::conditional<std::is_void<T>::value, char, T>::type);
    char *addr = reinterpret_cast<char *>(this) + offset;
    if (!IsValidAddress(addr, size, kN)) {
      return nullptr;
    }

    return reinterpret_cast<T *>(addr);
  }

  template <typename T = void> BufferOffset ToOffset(T *addr, size_t size = 0) {
    if (addr == reinterpret_cast<T *>(this) || addr == nullptr) {
      return 0;
    }
    if (!IsValidMagic()) {
      return 0;
    }
    constexpr size_t kN =
        sizeof(typename std::conditional<std::is_void<T>::value, char, T>::type);
    if (!IsValidAddress(addr, size, kN)) {
      return 0;
    }
    return reinterpret_cast<const char *>(addr) -
           reinterpret_cast<const char *>(this);
  }

  template <typename T = void>
  const T *ToAddress(BufferOffset offset, size_t size = 0) const {
    if (offset == 0) {
      return nullptr;
    }
    if (!IsValidMagic()) {
      return nullptr;
    }
    constexpr size_t kN =
        sizeof(typename std::conditional<std::is_void<T>::value, char, T>::type);
    const char *addr = reinterpret_cast<const char *>(this) + offset;
    if (!IsValidAddress(addr, size, kN)) {
      return nullptr;
    }

    return reinterpret_cast<const T *>(addr);
  }

  template <typename T = void>
  BufferOffset ToOffset(const T *addr, size_t size = 0) const {
    if (addr == reinterpret_cast<const T *>(this) || addr == nullptr) {
      return 0;
    }
    if (!IsValidMagic()) {
      return 0;
    }
    constexpr size_t kN =
        sizeof(typename std::conditional<std::is_void<T>::value, char, T>::type);
    if (!IsValidAddress(addr, size, kN)) {
      return 0;
    }
    return reinterpret_cast<const char *>(addr) -
           reinterpret_cast<const char *>(this);
  }

  void InsertNewFreeBlockAtEnd(FreeBlockHeader *free_block,
                               FreeBlockHeader *prev, uint32_t length);

  void MergeWithAboveIfPossible(FreeBlockHeader *alloc_block,
                                FreeBlockHeader *alloc_header,
                                FreeBlockHeader *free_block,
                                BufferOffset *next_ptr, size_t alloc_length);

  void ShrinkBlock(FreeBlockHeader *alloc_block, uint32_t orig_length,
                   uint32_t new_length, uint64_t *len_ptr);

  uint64_t *MergeWithFreeBlockBelow(void *alloc_block, FreeBlockHeader *prev,
                                    FreeBlockHeader *free_block,
                                    uint32_t new_length, uint32_t orig_length,
                                    bool clear);

  void ExpandIntoFreeBlockAbove(FreeBlockHeader *free_block,
                                uint32_t new_length, uint32_t len_diff,
                                uint32_t free_remaining, uint64_t *len_ptr,
                                BufferOffset *next_ptr, bool clear);
  uint32_t TakeStartOfFreeBlock(FreeBlockHeader *block, uint32_t num_bytes,
                                uint32_t full_length, FreeBlockHeader *prev);

  void UpdateHWM(void *p) { UpdateHWM(ToOffset(p)); }

  void UpdateHWM(BufferOffset off) {
    if (off > hwm) {
      hwm = off;
    }
  }
};

template <>
inline char *PayloadBuffer::SetString(PayloadBuffer **self, const char *s,
                                      BufferOffset header_offset) {
  return SetString(self, s, strlen(s), header_offset);
}

inline void *PayloadBuffer::Allocate(PayloadBuffer **buffer, uint32_t n,
                                     bool clear, bool enable_small_block) {
  PayloadBuffer *pb = *buffer;
  if (n == 0 || n > pb->full_size) {
    return AllocateSlow(buffer, n, clear, enable_small_block);
  }
  char *base = reinterpret_cast<char *>(pb);
  uint64_t size_word;
  uint32_t block_size;
  int index = -1;
  if (enable_small_block && pb->BitmapsEnabled() &&
      (index = SmallBlockIndex(n)) >= 0) {
    block_size = SmallBlockSize(index);
    size_word = kSmallBlockFlag | n;
    const BufferOffset head = pb->bitmaps[index];
    if (head != 0) {
      if (head > pb->full_size - block_size) {
        return AllocateSlow(buffer, n, clear, enable_small_block);
      }
      char *addr = base + head;
      pb->bitmaps[index] = *reinterpret_cast<BufferOffset *>(addr);
      *(reinterpret_cast<uint64_t *>(addr) - 1) = size_word;
      if (clear) {
        memset(addr, 0, block_size);
      }
      return addr;
    }
  } else {
    block_size = (n + 7) & ~7U;
    size_word = block_size;
  }
  // Carve from the start of the first free block, leaving room for the
  // remainder's FreeBlockHeader.
  const BufferOffset first = pb->free_list;
  const uint32_t full_length = block_size + uint32_t(sizeof(uint64_t));
  if (first == 0 ||
      first > pb->full_size - uint32_t(sizeof(FreeBlockHeader))) {
    return AllocateSlow(buffer, n, clear, enable_small_block);
  }
  FreeBlockHeader *free_block = reinterpret_cast<FreeBlockHeader *>(base + first);
  const uint32_t length = free_block->length;
  if (length < full_length + uint32_t(sizeof(FreeBlockHeader)) ||
      length > pb->full_size - first) {
    return AllocateSlow(buffer, n, clear, enable_small_block);
  }
  const BufferOffset rest_offset = first + full_length;
  FreeBlockHeader *rest = reinterpret_cast<FreeBlockHeader *>(base + rest_offset);
  rest->next = free_block->next;
  rest->length = length - full_length;
  pb->free_list = rest_offset;
  pb->UpdateHWM(rest_offset + uint32_t(sizeof(FreeBlockHeader)));
  uint64_t *header = reinterpret_cast<uint64_t *>(free_block);
  *header = size_word;
  void *addr = header + 1;
  if (clear) {
    memset(addr, 0, block_size);
  }
  return addr;
}

inline void PayloadBuffer::Free(void *p) {
  if (p == nullptr) {
    return;
  }
  const uint64_t size_word = *(reinterpret_cast<uint64_t *>(p) - 1);
  if ((size_word & kSmallBlockFlag) != 0) {
    const int index =
        SmallBlockIndex(static_cast<uint32_t>(size_word & kBitmapRunSizeMask));
    if (index >= 0) {
      *reinterpret_cast<BufferOffset *>(p) = bitmaps[index];
      bitmaps[index] = BufferOffset(reinterpret_cast<char *>(p) -
                                    reinterpret_cast<char *>(this));
      return;
    }
  }
  FreeSlow(p);
}

template <typename T> inline void PayloadBuffer::Set(BufferOffset offset, T v) {
  T *addr = ToAddress<T>(offset);
  *addr = v;
}

template <typename T> inline T &PayloadBuffer::Get(BufferOffset offset) {
  T *addr = ToAddress<T>(offset);
  return *addr;
}

template <typename T>
inline void PayloadBuffer::VectorPush(PayloadBuffer **self, VectorHeader *hdr,
                                      T v, bool enable_small_block) {
  // hdr points to a VectorHeader:
  // uint32_t num_elements;     - number of elements in the vector
  // BufferOffset data;         - BufferOffset to vector contents
  // The vector contents is allocated in the buffer.  It is preceded
  // by the block size (in bytes).
  BufferOffset hdr_offset = (*self)->ToOffset(hdr);

  uint32_t total_size = hdr->num_elements * sizeof(T);
  if (hdr->data == 0) {
    // The vector is empty, allocate it with a default size of 2.
    void *vecp = Allocate(self, 2 * sizeof(T), true, enable_small_block);
    VectorHeader *new_hdr = (*self)->ToAddress<VectorHeader>(hdr_offset);
    new_hdr->data = (*self)->ToOffset(vecp);
    hdr = new_hdr;
  } else {
    // Vector has some values in it.  Retrieve the total size from
    // the allocated block header (before the start of the memory)
    uint32_t *block = (*self)->ToAddress<uint32_t>(hdr->data);
    uint32_t current_size = DecodeSize(block);
    const uint32_t needed = (hdr->num_elements + 1) * sizeof(T);
    if (needed > current_size) {
      // Grow to fit the next element; at least double prior capacity.
      const uint32_t min_capacity =
          2u * static_cast<uint32_t>(sizeof(T));
      const uint32_t new_size = std::max<uint32_t>(
          needed, std::max<uint32_t>(current_size * 2, min_capacity));
      void *vecp = Realloc(self, block, new_size, true, enable_small_block);
      VectorHeader *new_hdr = (*self)->ToAddress<VectorHeader>(hdr_offset);
      new_hdr->data = (*self)->ToOffset(vecp);
      hdr = new_hdr;
    }
  }
  // Get address of next location in vector.
  T *valuep = (*self)->ToAddress<T>(hdr->data) + hdr->num_elements;
  // Assign value.
  *valuep = v;
  // Increment the number of elements.
  hdr->num_elements++;
}

template <typename T>
inline void PayloadBuffer::VectorReserve(PayloadBuffer **self,
                                         VectorHeader *hdr, size_t n,
                                         bool enable_small_block) {
  BufferOffset hdr_offset = (*self)->ToOffset(hdr);
  if (hdr->data == 0) {
    void *vecp = Allocate(self, n * sizeof(T), false, enable_small_block);
    VectorHeader* new_hdr = (*self)->ToAddress<VectorHeader>(hdr_offset);
    new_hdr->data = (*self)->ToOffset(vecp);
    hdr = new_hdr;
  } else {
    // Vector has some values in it.  Retrieve the total size from
    // the allocated block header (before the start of the memory)
    uint32_t *block = (*self)->ToAddress<uint32_t>(hdr->data);
    uint32_t current_size = DecodeSize(block);
    if (current_size < n * sizeof(T)) {
      // Need to expand the memory to the size given.
      void *vecp =
          Realloc(self, block, n * sizeof(T), false, enable_small_block);
      VectorHeader* new_hdr = (*self)->ToAddress<VectorHeader>(hdr_offset);
      new_hdr->data = (*self)->ToOffset(vecp);
      hdr = new_hdr;
    }
  }
}

template <typename T>
inline void PayloadBuffer::VectorResize(PayloadBuffer **self, VectorHeader *hdr,
                                        size_t n) {
  BufferOffset hdr_offset = (*self)->ToOffset(hdr);
  if (hdr->data == 0) {
    void *vecp = Allocate(self, n * sizeof(T));
    VectorHeader* new_hdr = (*self)->ToAddress<VectorHeader>(hdr_offset);
    new_hdr->data = (*self)->ToOffset(vecp);
    hdr = new_hdr;
  } else {
    // Vector has some values in it.  Retrieve the total size from
    // the allocated block header (before the start of the memory)
    uint32_t *block = (*self)->ToAddress<uint32_t>(hdr->data);
    uint32_t current_size = DecodeSize(block);
    if (current_size < n * sizeof(T)) {
      // Need to expand the memory to the size given.
      void *vecp = Realloc(self, block, n * sizeof(T), 8);
      VectorHeader* new_hdr = (*self)->ToAddress<VectorHeader>(hdr_offset);
      new_hdr->data = (*self)->ToOffset(vecp);
      hdr = new_hdr;
    }
  }
  hdr->num_elements = n;
}

template <typename T>
inline void PayloadBuffer::VectorClear(PayloadBuffer **self,
                                       VectorHeader *hdr) {
  if (hdr->data != 0) {
    (*self)->Free((*self)->ToAddress<void>(hdr->data));
  }
  hdr->data = 0;
  hdr->num_elements = 0;
}

template <typename T>
inline T PayloadBuffer::VectorGet(const VectorHeader *hdr, size_t index) const {
  if (index >= hdr->num_elements) {
    return static_cast<T>(0);
  }
  const T *addr = ToAddress<const T>(hdr->data);
  if (addr == nullptr) {
    return static_cast<T>(0);
  }
  return addr[index];
}

template <typename MessageType>
inline MessageType *PayloadBuffer::NewMessage(PayloadBuffer **self,
                                              uint32_t size,
                                              BufferOffset offset) {
  void *msg = Allocate(self, size);
  BufferOffset *p = (*self)->ToAddress<BufferOffset>(offset);
  *p = (*self)->ToOffset(msg);
  return reinterpret_cast<MessageType *>(msg);
}
} // namespace phaser
