/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/option.h"

#include "granary/cache.h"

GRANARY_DEFINE_positive_int(code_cache_slab_size, 8,
    "The number of pages allocated at once to store cache code. Each "
    "module maintains its own cache code allocator. The default value is "
    "8 pages per slab.");

namespace granary {

CodeCache::CodeCache(int slab_size)
    : lock(),
      allocator(0 >= slab_size ? FLAG_code_cache_slab_size : slab_size) {}

// Allocate a block of code from this code cache.
CachePC CodeCache::AllocateBlock(int size) {
  if (0 >= size) {
    // Staged allocation, which is typically used to get an "estimator" PC
    // within the code cache. The estimator PC is then used as a guide during
    // the relativization step of instruction encoding, which needs to ensure
    // that PC-relative references in application code to application data
    // continue to work.
    return allocator.Allocate(1, 0);
  } else {
    return allocator.Allocate(arch::CACHE_LINE_SIZE_BYTES, size);
  }
}

// Begin a transaction that will read or write to the code cache.
//
// Note: Transactions are distinct from allocations. Therefore, many threads/
//       cores can simultaneously allocate from a code cache, but only one
//       should be able to read/write data to the cache at a given time.
void CodeCache::BeginTransaction(void) {
  lock.Acquire();
}

// End a transaction that will read or write to the code cache.
void CodeCache::EndTransaction(void) {
  lock.Release();
}

}  // namespace granary
