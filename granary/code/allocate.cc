/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"
#include "granary/code/allocate.h"
#include "granary/memory.h"

namespace granary {
namespace internal {
CodeSlab::CodeSlab(int num_pages, int num_bytes, int offset_, CodeSlab *next_)
    : begin(nullptr),
      next(next_),
      offset(ATOMIC_VAR_INIT(offset_)) {
  if (GRANARY_LIKELY(0 < num_pages)) {
    begin = reinterpret_cast<CachePC>(
        AllocatePages(num_pages, MemoryIntent::EXECUTABLE));
    memset(
        begin, GRANARY_ARCH_EXEC_POISON, static_cast<unsigned long>(num_bytes));
    ProtectPages(begin, num_pages, MemoryProtection::EXECUTABLE);
    VALGRIND_MAKE_MEM_UNDEFINED(begin, num_bytes);
  }
}
}  // namespace internal

CodeAllocator::CodeAllocator(int num_pages_)
    : num_pages(num_pages_),
      num_bytes(num_pages * GRANARY_ARCH_PAGE_FRAME_SIZE),
      slab_sentinel(0, 0, num_bytes + 1, nullptr),
      slab(&slab_sentinel),
      slab_lock() {}

// Allocates some executable code of size `size` with alignment `alignment`.
CachePC CodeAllocator::Allocate(int alignment, int size) {
  int old_offset(0);
  int new_offset(0);
  CachePC addr(nullptr);
  do {
    auto curr_slab = slab;
    old_offset = curr_slab->offset.load(std::memory_order_acquire);
    if (GRANARY_UNLIKELY(old_offset >= num_bytes)) {
      AllocateSlab();
    } else {
      auto aligned_offset = GRANARY_ALIGN_TO(old_offset, alignment);
      new_offset = aligned_offset + size;
      if (curr_slab->offset.compare_exchange_weak(old_offset, new_offset,
                                                  std::memory_order_release) &&
          new_offset <= num_bytes) {
        addr = &(curr_slab->begin[aligned_offset]);
      }
    }
  } while (!addr);
  VALGRIND_MAKE_MEM_DEFINED(addr, size);
  return addr;
}

// Allocate a new slab of memory for executable code.
void CodeAllocator::AllocateSlab(void) {
  FineGrainedLocked locker(&slab_lock);
  if (slab->offset.load(std::memory_order_acquire) < num_bytes) {
    return;  // Two competing allocations; we lost.
  }
  slab = new internal::CodeSlab(num_pages, num_bytes, 0, slab);
}

}  // namespace granary
