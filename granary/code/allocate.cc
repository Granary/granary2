/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"
#include "granary/base/string.h"
#include "granary/code/allocate.h"
#include "granary/memory.h"

namespace granary {
namespace internal {

// Initialize the metadata about a generic code slab.
CodeSlab::CodeSlab(int num_pages, int num_bytes, int offset_, CodeSlab *next_)
    : begin(nullptr),
      next(next_),
      offset(ATOMIC_VAR_INIT(offset_)) {
  if (GRANARY_LIKELY(0 < num_pages)) {
    begin = reinterpret_cast<CachePC>(
        AllocatePages(num_pages, MemoryIntent::EXECUTABLE));
    memset(begin, arch::EXEC_MEMORY_POISON_BYTE,
           static_cast<unsigned long>(num_bytes));
    VALGRIND_MAKE_MEM_UNDEFINED(begin, num_bytes);
  }
}

namespace {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
// A "dummy" slab that is at the end of the slab list.
GRANARY_EARLY_GLOBAL
static CodeSlab kSlabSentinel(0, 0, std::numeric_limits<int>::max(), nullptr);
#pragma clang diagnostic pop
}  // namespace
}  // namespace internal

CodeAllocator::CodeAllocator(int num_pages_)
    : num_pages(num_pages_),
      num_bytes(num_pages * GRANARY_ARCH_PAGE_FRAME_SIZE),
      slab_lock(),
      slab(ATOMIC_VAR_INIT(&internal::kSlabSentinel)) {}

CodeAllocator::~CodeAllocator(void) {
  internal::CodeSlab *next_slab(nullptr);
  auto slab_ = slab.exchange(nullptr);
  for (; slab_ && &internal::kSlabSentinel != slab_; slab_ = next_slab) {
    next_slab = slab_->next;
    FreePages(slab_->begin, num_pages, MemoryIntent::EXECUTABLE);
    delete slab_;
  }
}

// Allocates some executable code of size `size` with alignment `alignment`.
CachePC CodeAllocator::Allocate(int alignment, int size) {
  int old_offset(0);
  int new_offset(0);
  CachePC addr(nullptr);
  do {
    auto curr_slab = slab.load(std::memory_order_acquire);
    old_offset = curr_slab->offset.load(std::memory_order_acquire);
    if (GRANARY_UNLIKELY(old_offset >= num_bytes)) {
      AllocateSlab();
    } else {
      auto aligned_offset = GRANARY_ALIGN_TO(old_offset, alignment);
      new_offset = aligned_offset + size;
      if (curr_slab->offset.compare_exchange_weak(old_offset, new_offset,
                                                  std::memory_order_acq_rel) &&
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
  auto curr_slab = slab.load(std::memory_order_acquire);
  if (curr_slab->offset.load(std::memory_order_acquire) < num_bytes) {
    // The lock was contended, and then someone allocated. Now we've gone and
    // acquired the lock, but a big enough slab has already been allocated.
    return;
  }
  slab.store(new internal::CodeSlab(num_pages, num_bytes, 0, curr_slab));
}

}  // namespace granary
