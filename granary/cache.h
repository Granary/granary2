/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CACHE_H_
#define GRANARY_CACHE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/cpu.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

#include "metadata.h"

namespace granary {
namespace internal {
class CodeSlab;
}  // namespace internal

enum CodeCacheKind {
  // Generally filled with application code.
  kCodeCacheHot,

  // Filled with cold application code, or instrumentation code that is
  // targeted by a branch instruction.
  kCodeCacheCold,

  // Filled with edge code, or instrumentation code that is targeted by a
  // branch from a cold basic block.
  kCodeCacheFrozen,

  kNumCodeCacheKinds
};

// Forward declaration.
class CodeCacheTransaction;

// Implementation of Granary's code caches.
class CodeCache {
 public:
  explicit CodeCache(size_t slab_size_);
  ~CodeCache(void);

  // Allocate a block of code from this code cache.
  CachePC AllocateBlock(size_t size);

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeCache, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 private:
  // The size of a slab.
  const size_t slab_num_pages;
  const size_t slab_num_bytes;

  // The offset into the current slab that's serving allocations.
  size_t slab_byte_offset;

  // Lock around the whole code cache, which prevents multiple people from
  // reading/writing to the cache at once.
  SpinLock slab_list_lock;

  // Allocator used to allocate blocks from this code cache.
  const internal::CodeSlab *slab_list;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCache);
};

// Transaction on the code cache.
class CodeCacheTransaction {
 public:
  // Begin a transaction that will read or write to the code cache.
  //
  // Note: Transactions are distinct from allocations. Therefore, many threads/
  //       cores can simultaneously allocate from a code cache, but only one
  //       should be able to read/write data to the cache at a given time.
  CodeCacheTransaction(CachePC begin_, CachePC end_);

  // End a transaction that will read or write to the code cache.
  ~CodeCacheTransaction(void);

 private:
  CodeCacheTransaction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCacheTransaction);
};

#ifdef GRANARY_INTERNAL
// Provides a good estimation of the location of the code cache. This is used
// by all code that computes whether or not an address is too far away from the
// code cache.
CachePC EstimatedCachePC(void);

// Some architectures cannot encode arbitrarily (i.e. beyond 24- or 32-bits
// of relative displacement) far jumps; however, they sometimes can encode far
// jumps that use 32- or even 64-bit relative or absolute memory locations,
// where the jump target is first loaded from memory.
class NativeAddress {
 public:
  inline NativeAddress(PC pc_, NativeAddress **next_)
      : pc(pc_),
        next(*next_) {
    *next_ = this;
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
  void Join(const CacheMetaData &) {}

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
