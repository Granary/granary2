/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "generated/linux_user/types.h"

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/container.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

extern "C" {

// Path to the loaded Granary library. Code cache `mmap`s are associated with
// this file.
char *granary_block_cache_begin = nullptr;
char *granary_block_cache_end = nullptr;
char *granary_edge_cache_begin = nullptr;
char *granary_edge_cache_end = nullptr;

}  // extern C
namespace granary {
namespace os {
namespace {

enum : size_t {
  kBlockCacheNumPages = 10240UL,
  kBlockCacheNumBytes = kBlockCacheNumPages * arch::PAGE_SIZE_BYTES,  // 40mb
  kEdgeCacheNumPages = 2560,
  kEdgeCacheNumBytes = kEdgeCacheNumPages * arch::PAGE_SIZE_BYTES,  // 10mb
  kCodeCacheNumBytes = kBlockCacheNumBytes + kEdgeCacheNumBytes
};

// Slab allocators for block and edge cache code.
static Container<DynamicHeap<kBlockCacheNumPages>> block_memory;
static Container<DynamicHeap<kEdgeCacheNumPages>> edge_memory;

}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {
  auto prot = PROT_EXEC | PROT_READ | PROT_WRITE;
  auto flags = MAP_PRIVATE | MAP_ANONYMOUS;

  // Initialize the block code cache.
  granary_block_cache_begin = reinterpret_cast<char *>(
      mmap(nullptr, kCodeCacheNumBytes, prot, flags, -1, 0));
  granary_block_cache_end = granary_block_cache_begin + kBlockCacheNumBytes;

  // Initialize the edge code cache.
  granary_edge_cache_begin = granary_block_cache_end;
  granary_edge_cache_end = granary_edge_cache_begin + kEdgeCacheNumBytes;

  block_memory.Construct(granary_block_cache_begin);
  edge_memory.Construct(granary_edge_cache_begin);
}

// Destroys the Granary heap.
void ExitHeap(void) {
  block_memory.Destroy();
  edge_memory.Destroy();

  munmap(granary_block_cache_begin, kCodeCacheNumBytes);

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
  for (auto num_tries = 0; num_tries < 3; ++num_tries) {
    auto ret = mmap(nullptr, arch::PAGE_SIZE_BYTES * num, prot, flags, -1, 0);
    if (reinterpret_cast<void *>(-1LL) == ret) continue;
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
