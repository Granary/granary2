/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ALLOCATE_H_
#define GRANARY_CODE_ALLOCATE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"
#include "granary/base/lock.h"

namespace granary {

class Module;

namespace internal {
class CodeSlab;
}  // namespace internal

// Used to allocate code that performs bump-pointer allocation from slabs of
// memory, each with size `num_pages`.
class CodeAllocator {
 public:
  // Initialize a new code allocator.
  explicit CodeAllocator(int num_pages_);

  ~CodeAllocator(void);

  // Allocates some executable code of size `size` with alignment `alignment`.
  CachePC Allocate(Module *module, int alignment, int size);

 private:
  CodeAllocator(void) = delete;

  // Allocate a new slab of memory for executable code.
  void AllocateSlab(Module *module);

  // Number of pages per allocated slab.
  const int num_pages;

  // Number of bytes per allocated slab.
  const int num_bytes;

  // Lock acquired when we want to overwrite the `slab` pointer.
  FineGrainedLock slab_lock;

  // Pointer to the head of the slab list.
  std::atomic<internal::CodeSlab *> slab;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeAllocator);
};

}  // namespace granary

#endif  // GRANARY_CODE_ALLOCATE_H_
