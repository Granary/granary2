/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/cache.h"

#include "os/memory.h"

namespace granary {

CodeCache::CodeCache(os::Module *module_, int slab_size)
    : lock(),
      allocator(slab_size),
      module(module_) {}

// Allocate a block of code from this code cache.
CachePC CodeCache::AllocateBlock(int size) {
  if (0 >= size) {
    // Staged allocation, which is typically used to get an "estimator" PC
    // within the code cache. The estimator PC is then used as a guide during
    // the relativization step of instruction encoding, which needs to ensure
    // that PC-relative references in application code to application data
    // continue to work.
    return allocator.Allocate(module, 1, 0);
  } else {
    return allocator.Allocate(module, arch::CODE_ALIGN_BYTES, size);
  }
}

namespace {
#if defined(GRANARY_WHERE_user) && defined(GRANARY_TARGET_debug)
# define PAGE_DOWN(x) ((reinterpret_cast<uintptr_t>(x) >> 12) << 12)
# define PAGE_UP(x) PAGE_DOWN(x + 4095)

void EnableWritesToCode(CachePC begin, CachePC end) {
  auto begin_addr = PAGE_DOWN(begin);
  auto end_addr = PAGE_UP(end);
  auto num_pages = (end_addr - begin_addr) / arch::PAGE_SIZE_BYTES;
  os::ProtectPages(reinterpret_cast<void *>(begin_addr),
                   static_cast<int>(num_pages),
                   os::MemoryProtection::PATCHABLE_EXECUTABLE);
}

void DisableWritesToCode(CachePC begin, CachePC end) {
  auto begin_addr = PAGE_DOWN(begin);
  auto end_addr = PAGE_UP(end);
  auto num_pages = (end_addr - begin_addr) / arch::PAGE_SIZE_BYTES;
  os::ProtectPages(reinterpret_cast<void *>(begin_addr),
                   static_cast<int>(num_pages),
                   os::MemoryProtection::EXECUTABLE);
}

#else
static inline void EnableWritesToCode(CachePC, CachePC) {}
static inline void DisableWritesToCode(CachePC, CachePC) {}
#endif  // GRANARY_WHERE_user && GRANARY_TARGET_debug
}  // namespace

// Begin a transaction that will read or write to the code cache.
//
// Note: Transactions are distinct from allocations. Therefore, many threads/
//       cores can simultaneously allocate from a code cache, but only one
//       should be able to read/write data to the cache at a given time.
void CodeCache::BeginTransaction(CachePC begin, CachePC end) {
  lock.Acquire();
  EnableWritesToCode(begin, end);
}

// End a transaction that will read or write to the code cache.
void CodeCache::EndTransaction(CachePC begin, CachePC end) {
  DisableWritesToCode(begin, end);
  lock.Release();
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
