/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CACHE_H_
#define GRANARY_CACHE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/cpu.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

#include "granary/code/allocate.h"

#include "metadata.h"

namespace granary {

// Forward declaration.
class CodeCacheTransaction;
class Module;

// Implementation of Granary's code caches.
class CodeCache {
 public:
  CodeCache(Module *module_, int slab_size);
  ~CodeCache(void) = default;

  // Allocate a block of code from this code cache.
  CachePC AllocateBlock(int size);

  // Begin a transaction that will read or write to the code cache.
  //
  // Note: Transactions are distinct from allocations. Therefore, many threads/
  //       cores can simultaneously allocate from a code cache, but only one
  //       should be able to read/write data to the cache at a given time.
  void BeginTransaction(CachePC begin, CachePC end);

  // End a transaction that will read or write to the code cache.
  void EndTransaction(CachePC begin, CachePC end);

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeCache, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 private:
  // Lock around the whole code cache, which prevents multiple people from
  // reading/writing to the cache at once.
  FineGrainedLock lock;

  // Allocator used to allocate blocks from this code cache.
  CodeAllocator allocator;

  // Module that represents the slabs of the allocator as ranges of mapped
  // executable memory.
  Module * const module;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCache);
};

// Transaction on the code cache. Wrapper around `BeginTransaction` and
// `EndTransaction` that ensures the two are lexically matched.
class CodeCacheTransaction {
 public:
  inline CodeCacheTransaction(CodeCache *cache_, CachePC begin_, CachePC end_)
      : cache(cache_),
        begin(begin_),
        end(end_) {
    cache->BeginTransaction(begin, end);
  }

  ~CodeCacheTransaction(void) {
    cache->EndTransaction(begin, end);
    cpu::SynchronizePipeline();
  }

 private:
  CodeCacheTransaction(void) = delete;

  CodeCache *cache;
  CachePC begin;
  CachePC end;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCacheTransaction);
};

#ifdef GRANARY_INTERNAL
// Some architectures cannot encode arbitrarily (i.e. beyond 24- or 32-bits
// of relative displacement) far jumps; however, they sometimes can encode far
// jumps that use 32- or even 64-bit relative or absolute memory locations,
// where the jump target is first loaded from memory.
class NativeAddress {
 public:
  inline NativeAddress(PC pc_, NativeAddress *&next_)
      : pc(pc_),
        next(next_) {
    next_ = this;
  }

  // Address that a far away jump or call will target.
  alignas(alignof(void *)) union {
    const void *addr;
    PC pc;
  };

  // Next far away address in this block.
  NativeAddress *next;

  GRANARY_DEFINE_NEW_ALLOCATOR(NativeAddress, {
    SHARED = true,
    ALIGNMENT = 16
  })

 private:
  NativeAddress(void) = delete;
};

// Forward declaration.
class IndirectEdge;

// Meta-data that Granary maintains about all basic blocks that are committed to
// the code cache. This is meta-data is private to Granary and therefore not
// exposed (directly) to tools.
class CacheMetaData : public MutableMetaData<CacheMetaData> {
 public:
  CacheMetaData(void);

  // Don't copy anything over.
  CacheMetaData(const CacheMetaData &)
      : start_pc(nullptr),
        native_addresses(nullptr) {}

  ~CacheMetaData(void);

  // When an indirect CFI targets a translated block, don't copy over its
  // `start_pc` or `native_addresses`.
  void Join(const CacheMetaData *) {}

  // Where this block is located in the code cache.
  //
  // If the value is non-null, then this points to the location of the first
  // instruction of the block in the code cache. If the value is null, then
  // either this block has not been encoded, or it represents the meta-data
  // of the target of an indirect control-flow instruction.
  CachePC start_pc;

  // Far-away code addresses referenced by code in this block.
  NativeAddress *native_addresses;
};
#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_CACHE_H_
