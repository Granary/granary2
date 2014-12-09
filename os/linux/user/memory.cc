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
void *granary_heap_begin = nullptr;
void *granary_heap_end = nullptr;

}  // extern C
namespace granary {
namespace os {
namespace {

enum : size_t {
  kBlockCacheNumPages = 20480UL,  // 80mb
  kBlockCacheNumBytes = kBlockCacheNumPages * arch::PAGE_SIZE_BYTES,
  kEdgeCacheNumPages = 2560,  // 10mb
  kEdgeCacheNumBytes = kEdgeCacheNumPages * arch::PAGE_SIZE_BYTES,
  kCodeCacheNumBytes = kBlockCacheNumBytes + kEdgeCacheNumBytes,
  kHeapNumPages = 40960UL,  // 160mb
  kHeapNumBytes = kHeapNumPages * arch::PAGE_SIZE_BYTES,
  kMmapNumBytes = kCodeCacheNumBytes + kHeapNumBytes
};

// Slab allocators for block and edge cache code.
static Container<DynamicHeap<kBlockCacheNumPages>> block_memory;
static Container<DynamicHeap<kEdgeCacheNumPages>> edge_memory;
static Container<DynamicHeap<kHeapNumPages>> heap_memory;

}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {
  auto all_mem = mmap(nullptr, kMmapNumBytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  mprotect(all_mem, kCodeCacheNumBytes, PROT_EXEC | PROT_READ | PROT_WRITE);

  // Initialize the block code cache.
  granary_block_cache_begin = reinterpret_cast<char *>(all_mem);
  granary_block_cache_end = granary_block_cache_begin + kBlockCacheNumBytes;

  // Initialize the edge code cache.
  granary_edge_cache_begin = granary_block_cache_end;
  granary_edge_cache_end = granary_edge_cache_begin + kEdgeCacheNumBytes;

  granary_heap_begin = granary_block_cache_begin + kCodeCacheNumBytes;
  granary_heap_end = granary_block_cache_begin + kMmapNumBytes;

  block_memory.Construct(granary_block_cache_begin);
  edge_memory.Construct(granary_edge_cache_begin);
  heap_memory.Construct(granary_heap_begin);
}

// Destroys the Granary heap.
void ExitHeap(void) {
  block_memory.Destroy();
  edge_memory.Destroy();
  heap_memory.Destroy();

  munmap(granary_block_cache_begin, kMmapNumBytes);

  granary_block_cache_begin = nullptr;
  granary_block_cache_end = nullptr;

  granary_edge_cache_begin = nullptr;
  granary_edge_cache_end = nullptr;

  granary_heap_begin = nullptr;
  granary_heap_end = nullptr;
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocateDataPages(size_t num) {
  return heap_memory->AllocatePages(num);
}

// Frees `num` pages back to the OS.
void FreeDataPages(void *addr, size_t num) {
  heap_memory->FreePages(addr, num);
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
