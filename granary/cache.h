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

namespace granary {

// Forward declaration.
class CodeCacheTransaction;

// Interface for code caches.
class CodeCacheInterface {
 public:
  CodeCacheInterface(void) = default;
  virtual ~CodeCacheInterface(void) = default;

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

}  // namespace granary

#endif  // GRANARY_CACHE_H_
