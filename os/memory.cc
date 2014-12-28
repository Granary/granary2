/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/container.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

extern "C" {

// Path to the loaded Granary library. Code cache `mmap`s are associated with
// this file.
char *granary_code_cache_begin = nullptr;
char *granary_code_cache_end = nullptr;
void *granary_heap_begin = nullptr;
void *granary_heap_end = nullptr;

// Forces the `.writable_text` section to exit with the right protections.
asm(
  ".section .writable_text,\"awx\",@nobits;"
  ".previous;"
);

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

// Each static allocator uses a static array of frames so that we can use
// attributes, so we use tag types to distinguish different instances.
struct code_cache_tag {};
struct heap_memory_tag {};

// Slab allocators for block and edge cache code.
static StaticPageAllocator<kBlockCacheNumPages, code_cache_tag, kMemoryTypeRWX>
    block_memory GRANARY_EARLY_GLOBAL;

static StaticPageAllocator<kHeapNumPages, heap_memory_tag, kMemoryTypeRW>
    heap_memory GRANARY_EARLY_GLOBAL;

}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {

  // Initialize the block code cache.
  granary_code_cache_begin = reinterpret_cast<char *>(
      block_memory.BeginAddress());
  granary_code_cache_end = reinterpret_cast<char *>(
      block_memory.EndAddress());

  granary_heap_begin = heap_memory.BeginAddress();
  granary_heap_end = heap_memory.EndAddress();
}

// Destroys the Granary heap.
void ExitHeap(void) {
  granary_code_cache_begin = nullptr;
  granary_code_cache_end = nullptr;

  granary_heap_begin = nullptr;
  granary_heap_end = nullptr;

  memset(&block_memory, 0, sizeof block_memory);
  memset(&heap_memory, 0, sizeof heap_memory);

  new (&block_memory) decltype(block_memory);
  new (&heap_memory) decltype(block_memory);
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocateDataPages(size_t num) {
  return heap_memory.AllocatePages(num);
}

// Frees `num` pages back to the OS.
void FreeDataPages(void *addr, size_t num) {
  heap_memory.FreePages(addr, num);
}

// Allocates `num` number of executable pages from the block code cache.
CachePC AllocateCodePages(size_t num) {
  return reinterpret_cast<CachePC>(block_memory.AllocatePages(num));
}

// Frees `num` pages back to the block code cache.
void FreeCodePages(CachePC addr, size_t num) {
  block_memory.FreePages(addr, num);
}
}  // namespace os
}  // namespace granary
