/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/new.h"
#include "granary/base/string.h"

#include "granary/code/allocate.h"

#include "granary/module.h"

#include "os/memory.h"

namespace granary {
namespace internal {

class CodeSlab {
 public:
  CodeSlab(Module *code_module, int num_pages, int num_bytes, int offset_,
           CodeSlab *next_);

  std::atomic<int> offset;

  alignas(arch::CACHE_LINE_SIZE_BYTES) CachePC begin;
  CodeSlab *next;

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeSlab, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 private:
  CodeSlab(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeSlab);
};

// Initialize the metadata about a generic code slab.
CodeSlab::CodeSlab(Module *module, int num_pages, int num_bytes,
                   int offset_, CodeSlab *next_)
    : offset(ATOMIC_VAR_INIT(offset_)),
      begin(nullptr),
      next(next_) {
  if (GRANARY_LIKELY(0 < num_pages)) {
    begin = reinterpret_cast<CachePC>(
        os::AllocatePages(num_pages, os::MemoryIntent::EXECUTABLE));
    memset(begin, arch::EXEC_MEMORY_POISON_BYTE,
           static_cast<unsigned long>(num_bytes));
    VALGRIND_MAKE_MEM_UNDEFINED(begin, num_bytes);

    // Add the slab to the module that is meant to represent all code allocated
    // by this allocator.
    if (GRANARY_LIKELY(nullptr != module)) {
      auto begin_addr = reinterpret_cast<uintptr_t>(begin);
      auto end_addr = begin_addr +
                      static_cast<uintptr_t>(num_pages * arch::PAGE_SIZE_BYTES);
      module->AddRange(
          begin_addr, end_addr, begin_addr,
          MODULE_READABLE | MODULE_WRITABLE | MODULE_EXECUTABLE);
    }
  }
}

namespace {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
// A "dummy" slab that is at the end of the slab list.
GRANARY_EARLY_GLOBAL
static CodeSlab kSlabSentinel(nullptr, 0, 0, std::numeric_limits<int>::max(),
                              nullptr);
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
    os::FreePages(slab_->begin, num_pages, os::MemoryIntent::EXECUTABLE);
    delete slab_;
  }
}

// Allocates some executable code of size `size` with alignment `alignment`.
CachePC CodeAllocator::Allocate(Module *module, int alignment, int size) {
  int old_offset(0);
  int new_offset(0);
  CachePC addr(nullptr);
  do {
    auto curr_slab = slab.load(std::memory_order_acquire);
    old_offset = curr_slab->offset.load(std::memory_order_acquire);
    if (GRANARY_UNLIKELY(old_offset >= num_bytes)) {
      AllocateSlab(module);
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
void CodeAllocator::AllocateSlab(Module *module) {
  FineGrainedLocked locker(&slab_lock);
  auto curr_slab = slab.load(std::memory_order_acquire);
  if (curr_slab->offset.load(std::memory_order_acquire) < num_bytes) {
    // The lock was contended, and then someone allocated. Now we've gone and
    // acquired the lock, but a big enough slab has already been allocated.
    return;
  }
  slab.store(new internal::CodeSlab(module, num_pages, num_bytes,
                                    0, curr_slab));
}

}  // namespace granary
