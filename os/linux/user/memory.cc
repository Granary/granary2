/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/container.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

#define O_CREAT       0100
#define O_RDONLY      00
#define O_RDWR        02
#define O_CLOEXEC     02000000

#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT     0x40
#define MAP_LOCKED 0x02000


extern "C" {

// Path to the loaded Granary library. Code cache `mmap`s are associated with
// this file.
char *granary_block_cache_begin = nullptr;
char *granary_block_cache_end = nullptr;
char *granary_edge_cache_begin = nullptr;
char *granary_edge_cache_end = nullptr;

extern char *mmap(void *__addr, size_t __len, int __prot, int __flags,
                  int __fd, long __offset);
extern int munmap(void *__addr, size_t __len);

}  // extern C
namespace granary {
namespace os {
namespace {

enum : size_t {
  BLOCK_NUM_PAGES = 10240UL,
  BLOCK_CACHE_SIZE = BLOCK_NUM_PAGES * arch::PAGE_SIZE_BYTES,  // 40mb
  EDGE_NUM_PAGES = 2560,
  EDGE_CACHE_SIZE = EDGE_NUM_PAGES * arch::PAGE_SIZE_BYTES,  // 10mb
  COMBINED_SIZE = BLOCK_CACHE_SIZE + EDGE_CACHE_SIZE
};

// Slab allocators for block and edge cache code.
static Container<DynamicHeap<BLOCK_NUM_PAGES>> block_memory;
static Container<DynamicHeap<EDGE_NUM_PAGES>> edge_memory;

}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {
  auto prot = PROT_EXEC | PROT_READ | PROT_WRITE;
  auto flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if !defined(GRANARY_TARGET_test) && !defined(GRANARY_WITH_VALGRIND)
  flags |= MAP_32BIT;
#endif

  // Initialize the block code cache.
  granary_block_cache_begin = mmap(nullptr, COMBINED_SIZE, prot, flags, -1, 0);
  granary_block_cache_end = granary_block_cache_begin + BLOCK_CACHE_SIZE;

  // Initialize the edge code cache.
  granary_edge_cache_begin = granary_block_cache_end;
  granary_edge_cache_end = granary_edge_cache_begin + EDGE_CACHE_SIZE;

  block_memory.Construct(granary_block_cache_begin);
  edge_memory.Construct(granary_edge_cache_begin);
}

// Destroys the Granary heap.
void ExitHeap(void) {
  block_memory.Destroy();
  edge_memory.Destroy();

  munmap(granary_block_cache_begin, COMBINED_SIZE);

  granary_block_cache_begin = nullptr;
  granary_block_cache_end = nullptr;

  granary_edge_cache_begin = nullptr;
  granary_edge_cache_end = nullptr;
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocateDataPages(size_t num) {
  auto prot = PROT_READ | PROT_WRITE;
  auto flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if !defined(GRANARY_TARGET_test) && !defined(GRANARY_WITH_VALGRIND)
  flags |= MAP_32BIT;
#endif
  for (auto num_tries = 0; num_tries < 3; ++num_tries) {
    auto ret = mmap(nullptr, arch::PAGE_SIZE_BYTES * num, prot, flags, -1, 0);
    if (reinterpret_cast<char *>(-1LL) == ret) continue;
    return ret;
  }
  GRANARY_ASSERT(false);
  return nullptr;
}

// Frees `num` pages back to the OS.
void FreeDataPages(void *addr, size_t num) {
  munmap(addr, arch::PAGE_SIZE_BYTES * num);
}

// Allocates `num` number of executable pages from the block code cache.
CachePC AllocateBlockCachePages(size_t num) {
  return reinterpret_cast<CachePC>(block_memory->AllocatePages(num));
}

// Frees `num` pages back to the block code cache.
void FreeBlockCachePages(CachePC addr, size_t num) {
  block_memory->FreePages(addr, num);
}

// Allocates `num` number of executable pages from the block code cache.
CachePC AllocateEdgeCachePages(size_t num) {
  return reinterpret_cast<CachePC>(edge_memory->AllocatePages(num));
}

// Frees `num` pages back to the block code cache.
void FreeEdgeCachePages(CachePC addr, size_t num) {
  edge_memory->FreePages(addr, num);
}

}  // namespace os
}  // namespace granary
