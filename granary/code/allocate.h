/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ALLOCATE_H_
#define GRANARY_CODE_ALLOCATE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"
#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/types.h"
#include "granary/base/lock.h"

namespace granary {
namespace internal {
class CodeSlab {
 public:
  CodeSlab(int num_pages, int num_bytes, int offset_, CodeSlab *next_);

  CachePC begin;
  CodeSlab *next;
  alignas(GRANARY_ARCH_CACHE_LINE_SIZE) std::atomic<int> offset;

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeSlab, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

 private:
  CodeSlab(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeSlab);
};

}  // namespace internal

// Used to allocate code that performs bump-pointer allocation from slabs of
// memory, each with size `num_pages`.
class CodeAllocator {
 public:
  // Initialize a new code allocator.
  explicit CodeAllocator(int num_pages_);

  // Allocates some executable code of size `size` with alignment `alignment`.
  CachePC Allocate(int alignment, int size);

 private:
  CodeAllocator(void) = delete;

  // Allocate a new slab of memory for executable code.
  void AllocateSlab(void);

  // Number of pages per allocated slab.
  const int num_pages;

  // Number of bytes per allocated slab.
  const int num_bytes;

  // A "dummy" slab that is at the end of the slab list.
  internal::CodeSlab slab_sentinel;

  // Pointer to the head of the slab list.
  alignas(GRANARY_ARCH_CACHE_LINE_SIZE) internal::CodeSlab *slab;

  // Lock acquired when we want to overwrite the `slab` pointer.
  FineGrainedLock slab_lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeAllocator);
};

}  // namespace granary

#endif  // GRANARY_CODE_ALLOCATE_H_
