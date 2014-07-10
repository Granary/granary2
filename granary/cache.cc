/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/option.h"

#include "granary/cache.h"
#include "granary/memory.h"

namespace granary {

CodeCache::CodeCache(Module *module_, int slab_size)
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

// Apply some memory page protections to some range of memory.
static void ProtectRange(CachePC begin, CachePC end, MemoryProtection prot) {
  auto begin_addr = reinterpret_cast<uintptr_t>(begin);
  auto end_addr = reinterpret_cast<uintptr_t>(end);

  begin_addr -= begin_addr % arch::PAGE_SIZE_BYTES;
  end_addr += GRANARY_ALIGN_FACTOR(end_addr, arch::PAGE_SIZE_BYTES);
  auto diff_pages = (end_addr - begin_addr) / arch::PAGE_SIZE_BYTES;

  ProtectPages(reinterpret_cast<void *>(begin_addr),
               static_cast<int>(diff_pages), prot);
}

}  // namespace

// Begin a transaction that will read or write to the code cache.
//
// Note: Transactions are distinct from allocations. Therefore, many threads/
//       cores can simultaneously allocate from a code cache, but only one
//       should be able to read/write data to the cache at a given time.
void CodeCache::BeginTransaction(CachePC begin, CachePC end) {

  // TODO(pag): Need some kind of signaling mechanism that allows us to handle
  //            faults happening when code in a protected region is being
  //            modified.

  ProtectRange(begin, end, MemoryProtection::READ_WRITE);
  lock.Acquire();
}

// End a transaction that will read or write to the code cache.
void CodeCache::EndTransaction(CachePC begin, CachePC end) {
  lock.Release();
  ProtectRange(begin, end, MemoryProtection::EXECUTABLE);
}

// Initialize Granary's internal translation cache meta-data.
CacheMetaData::CacheMetaData(void)
    : cache_pc(nullptr),
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
