/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/container.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

extern "C" {
extern char *(*linux_module_alloc)(unsigned long);

char *granary_block_cache_begin = nullptr;
char *granary_block_cache_end = nullptr;
char *granary_edge_cache_begin = nullptr;
char *granary_edge_cache_end = nullptr;

}  // extern C
namespace granary {
namespace os {

namespace {
enum {
  kHeapNumPages = 4096,  // 16 MB.
  kBlockCacheNumPages = 2048,  // 8 MB.
  kBlockCacheNumBytes = kBlockCacheNumPages * arch::PAGE_SIZE_BYTES,
  kEdgeCacheNumPages = 512,  // 2 MB.
  kEdgeCacheNumBytes = kBlockCacheNumPages * arch::PAGE_SIZE_BYTES,
  kCodeCacheNumBytes = kBlockCacheNumBytes + kEdgeCacheNumBytes
};
static Container<StaticHeap<kHeapNumPages>> rw_memory \
    GRANARY_UNPROTECTED_GLOBAL;
static Container<DynamicHeap<kBlockCacheNumPages>> block_memory;
static Container<DynamicHeap<kEdgeCacheNumPages>> edge_memory;
}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {
  if (granary_block_cache_begin) return;
  granary_block_cache_begin = linux_module_alloc(kCodeCacheNumBytes);
  granary_block_cache_end = granary_block_cache_begin + kBlockCacheNumBytes;
  granary_edge_cache_begin = granary_block_cache_end;
  granary_edge_cache_end = granary_edge_cache_begin + kEdgeCacheNumBytes;

  rw_memory.Construct();
  block_memory.Construct(granary_block_cache_begin);
  edge_memory.Construct(granary_edge_cache_begin);
}

// Destroys the Granary heap.
void ExitHeap(void) {
  rw_memory.Destroy();
  block_memory.Destroy();
  edge_memory.Destroy();
  // TODO(pag): How to release `backing_module_mem`?
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocateDataPages(size_t num) {
  return rw_memory->AllocatePages(num);
}

// Frees `num` pages back to the OS.
void FreeDataPages(void *addr, size_t num) {
  rw_memory->FreePages(addr, num);
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
