/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CACHE_H_
#define GRANARY_CACHE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

#include "granary/code/allocate.h"

#ifdef GRANARY_INTERNAL
# include "metadata.h"
#endif

namespace granary {

// Forward declaration.
class CodeCacheTransaction;

// Interface for code caches.
class CodeCacheInterface {
 public:
  CodeCacheInterface(void) = default;

  // Needed for linking against the base vtable.
  virtual ~CodeCacheInterface(void);

  // Allocate a block of code from this code cache.
  virtual CachePC AllocateBlock(int size) = 0;

 protected:
  friend class CodeCacheTransaction;

  // Begin a transaction that will read or write to the code cache.
  //
  // Note: Transactions are distinct from allocations. Therefore, many threads/
  //       cores can simultaneously allocate from a code cache, but only one
  //       should be able to read/write data to the cache at a given time.
  virtual void BeginTransaction(CachePC begin, CachePC end) = 0;

  // End a transaction that will read or write to the code cache.
  virtual void EndTransaction(CachePC begin, CachePC end) = 0;
};

// Transaction on the code cache. Wrapper around `BeginTransaction` and
// `EndTransaction` that ensures the two are lexically matched.
class CodeCacheTransaction {
 public:
  inline CodeCacheTransaction(CodeCacheInterface *cache_,
                              CachePC begin_, CachePC end_)
      : cache(cache_),
        begin(begin_),
        end(end_) {
    cache->BeginTransaction(begin, end);
  }

  ~CodeCacheTransaction(void) {
    cache->EndTransaction(begin, end);
  }

 private:
  CodeCacheTransaction(void) = delete;

  CodeCacheInterface *cache;
  CachePC begin;
  CachePC end;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCacheTransaction);
};

// Implementation of Granary's code caches.
class CodeCache : public CodeCacheInterface {
 public:
  explicit CodeCache(int slab_size=0);
  virtual ~CodeCache(void) = default;

  // Allocate a block of code from this code cache.
  virtual CachePC AllocateBlock(int size) override;

  // Begin a transaction that will read or write to the code cache.
  //
  // Note: Transactions are distinct from allocations. Therefore, many threads/
  //       cores can simultaneously allocate from a code cache, but only one
  //       should be able to read/write data to the cache at a given time.
  virtual void BeginTransaction(CachePC begin, CachePC end) override;

  // End a transaction that will read or write to the code cache.
  virtual void EndTransaction(CachePC begin, CachePC end) override;

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

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCache);
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
      : cache_pc(nullptr),
        native_addresses(nullptr) {}

  ~CacheMetaData(void);

  // Where this block is located in the code cache.
  //
  // If the value is non-null, then this points to the location of the first
  // instruction of the block in the code cache. If the value is null, then
  // either this block has not been encoded, or it represents the meta-data
  // of the target of an indirect control-flow instruction.
  CachePC cache_pc;

  // Far-away code addresses referenced by code in this block.
  NativeAddress *native_addresses;

};
#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_CACHE_H_
