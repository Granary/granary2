/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/cache.h"

#include "os/memory.h"

extern "C" {
extern granary::CachePC granary_block_cache_begin;
}  // extern C
namespace granary {
namespace internal {

class CodeSlab {
 public:
  CodeSlab(CachePC begin_, CodeSlab *next_);

  size_t offset;
  CachePC begin;
  const CodeSlab *next;

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeSlab, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 private:
  CodeSlab(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeSlab);
};

// Initialize the metadata about a generic code slab.
CodeSlab::CodeSlab(CachePC begin_, CodeSlab *next_)
    : offset(0),
      begin(begin_),
      next(next_) {}

}  // namespace internal
namespace {

static internal::CodeSlab *AllocateSlab(size_t num_pages, CodeCacheKind kind,
                                        internal::CodeSlab *next) {
  CachePC addr(nullptr);
  if (kBlockCodeCache == kind) {
    addr = os::AllocateBlockCachePages(num_pages);
  } else {
    addr = os::AllocateEdgeCachePages(num_pages);
  }
  return new internal::CodeSlab(addr, next);
}

}  // namespace

CodeCache::CodeCache(size_t slab_size_, CodeCacheKind kind_)
    : slab_num_pages(slab_size_),
      slab_num_bytes(slab_size_ * arch::PAGE_SIZE_BYTES),
      kind(kind_),
      slab_list_lock(),
      slab_list(AllocateSlab(slab_num_pages, kind, nullptr)),
      code_lock() {}

// Allocate a block of code from this code cache.
CachePC CodeCache::AllocateBlock(size_t size) {
  SpinLockedRegion locker(&slab_list_lock);
  auto old_offset = slab_list->offset;
  auto aligned_offset = GRANARY_ALIGN_TO(old_offset, arch::CODE_ALIGN_BYTES);
  auto new_offset = aligned_offset + size;
  if (GRANARY_UNLIKELY(new_offset >= slab_num_bytes)) {
    slab_list = AllocateSlab(slab_num_pages, kind, slab_list);
    aligned_offset = GRANARY_ALIGN_TO(slab_list->offset,
                                      arch::CODE_ALIGN_BYTES);
    new_offset = aligned_offset + size;
    GRANARY_ASSERT(new_offset < slab_num_bytes);
  }
  auto addr = &(slab_list->begin[aligned_offset]);
  slab_list->offset = new_offset;
  return addr;
}

// Provides a good estimation of the location of the code cache. This is used
// by all code that computes whether or not an address is too far away from the
// code cache.
CachePC EstimatedCachePC(void) {
  return granary_block_cache_begin;
}

// Begin a transaction that will read or write to the code cache.
//
// Note: Transactions are distinct from allocations. Therefore, many threads/
//       cores can simultaneously allocate from a code cache, but only one
//       should be able to read/write data to the cache at a given time.
void CodeCache::BeginTransaction(CachePC, CachePC) {
  code_lock.Acquire();
}

// End a transaction that will read or write to the code cache.
void CodeCache::EndTransaction(CachePC, CachePC) {
  code_lock.Release();
}

// Initialize Granary's internal translation cache meta-data.
CacheMetaData::CacheMetaData(void)
    : start_pc(nullptr),
      native_addresses(nullptr) {}

// Clean up the cache meta-data, and any data structures tied in to the cached
// code.
CacheMetaData::~CacheMetaData(void) {
  NativeAddress *next_addr(nullptr);
  for (auto addr = native_addresses; addr; addr = next_addr) {
    next_addr = addr->next;
    delete addr;
  }
}

}  // namespace granary
