/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"
#include "granary/base/new.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

namespace granary {
namespace internal {

// Initialize a new slab list. Once initialized, slab lists are never changed.
SlabList::SlabList(const SlabList *next_slab_, size_t min_allocation_number_,
                   size_t slab_number_)
    : next(next_slab_),
      min_allocation_number(min_allocation_number_),
      number(slab_number_) {}

// Initialize the slab allocator. The slab allocator starts off having the slab
// list head point to a dummy slab, so that the allocator itself can avoid NULL
// checks.
SlabAllocator::SlabAllocator(size_t num_allocations_per_slab_,
                             size_t start_offset_,
                             size_t alignment_,
                             size_t aligned_size_,
                             size_t unaligned_size_)
    : num_allocations_per_slab(num_allocations_per_slab_),
      start_offset(start_offset_),
      alignment(alignment_),
      aligned_size(aligned_size_),
      unaligned_size(unaligned_size_),
      slab_list_tail(nullptr, num_allocations_per_slab_, 0),
      slab_list_head(ATOMIC_VAR_INIT(&slab_list_tail)),
      free_list(ATOMIC_VAR_INIT(nullptr)),
      slab_lock(),
      next_slab_number(1),
      next_allocation_number(num_allocations_per_slab_) {

  // Sanity check the inputs.
  GRANARY_ASSERT(0 == (aligned_size_ % alignment_));
  GRANARY_ASSERT(aligned_size_ >= unaligned_size_);
  GRANARY_ASSERT(internal::SLAB_ALLOCATOR_SLAB_SIZE_BYTES >=
                 (aligned_size_ * num_allocations_per_slab_ + start_offset_));

  // Used only when `GRANARY_WITH_VALGRIND` is set.
  GRANARY_UNUSED(unaligned_size);
  GRANARY_UNUSED(alignment);

  // Used only when `GRANARY_WITH_VALGRIND` isn't set.
  GRANARY_UNUSED(num_allocations_per_slab);
  GRANARY_UNUSED(start_offset);
  GRANARY_UNUSED(aligned_size);
  GRANARY_UNUSED(next_slab_number);
  GRANARY_UNUSED(next_allocation_number);
}

#ifndef GRANARY_WITH_VALGRIND

// Constants that define how we will initialize various chunks of memory.
enum {
  UNALLOCATED_MEMORY_POISON = 0xAB,
  DEALLOCATED_MEMORY_POISON = 0xCD,
  UNINITIALIZED_MEMORY_POISON = 0xEF,
};

// Allocate a new slab of memory for this object. The backing memory of the
// slab is initialized to `UNALLOCATED_MEMORY_POISON`.
const SlabList *SlabAllocator::AllocateSlab(const SlabList *prev_slab) {
  void *slab_memory(os::AllocateDataPages(SLAB_ALLOCATOR_SLAB_SIZE_PAGES));
  checked_memset(slab_memory, UNALLOCATED_MEMORY_POISON,
                 SLAB_ALLOCATOR_SLAB_SIZE_BYTES);
  VALGRIND_MAKE_MEM_NOACCESS(slab_memory, SLAB_ALLOCATOR_SLAB_SIZE_BYTES);
  return new (slab_memory) SlabList(
      prev_slab,
      next_slab_number * num_allocations_per_slab,
      next_slab_number);
}

// Get a pointer into the slab list. This potentially allocates a new slab.
// The slab pointer returned might not be the exact slab for the particular
// allocation.
const SlabList *SlabAllocator::GetOrAllocateSlab(size_t slab_number) {
  auto slab = slab_list_head.load(std::memory_order_acquire);
  if (GRANARY_UNLIKELY(!slab) || slab_number > slab->number) {
    GRANARY_ASSERT(next_slab_number == slab_number);
    slab = AllocateSlab(slab);
    GRANARY_ASSERT(slab->number == slab_number);
    next_slab_number += 1;
    slab_list_head.store(slab, std::memory_order_release);
  }
  for (; slab->number > slab_number; slab = slab->next) {}
  return slab;
}

#ifdef GRANARY_TARGET_debug
namespace {
static bool MemoryNotInUse(void *mem, size_t num_bytes) {
  auto bytes = reinterpret_cast<uint8_t *>(mem);
  for (auto i = 0UL; i < num_bytes; ++i) {
    if (bytes[i] != static_cast<uint8_t>(UNALLOCATED_MEMORY_POISON) &&
        bytes[i] != static_cast<uint8_t>(DEALLOCATED_MEMORY_POISON)) {
      return false;
    }
  }
  return true;
}
}  // namespace
#endif  // GRANARY_TARGET_debug

// Allocate some memory from the slab allocator.
void *SlabAllocator::Allocate(void) {
  void *address(AllocateFromFreeList());
  if (!address) {
    SpinLockedRegion locker(&slab_lock);
    auto slab_number = next_allocation_number / num_allocations_per_slab;
    auto slab = GetOrAllocateSlab(slab_number);
    auto index = next_allocation_number - slab->min_allocation_number;
    address = UnsafeCast<char *>(slab) + start_offset + (index * aligned_size);
    next_allocation_number += 1;
  }
  GRANARY_ASSERT(MemoryNotInUse(address, aligned_size));
  checked_memset(address, UNINITIALIZED_MEMORY_POISON, aligned_size);
  VALGRIND_MALLOCLIKE_BLOCK(address, unaligned_size, 0, 1);
  return address;
}

// Free some memory that was allocated from the slab allocator.
void SlabAllocator::Free(void *address) {
  VALGRIND_FREELIKE_BLOCK(address, unaligned_size);
  checked_memset(address, DEALLOCATED_MEMORY_POISON, aligned_size);

  FreeList *list(reinterpret_cast<FreeList *>(address));
  FreeList *next(nullptr);
  do {
    next = free_list.load(std::memory_order_relaxed);
    list->next = next;
  } while (!free_list.compare_exchange_strong(next, list));
}

// For those cases where a slab allocator is non-global.
void SlabAllocator::Destroy(void) {
  const SlabList *slab = slab_list_head.exchange(nullptr);
  const SlabList *next_slab(nullptr);
  for (; slab; slab = next_slab) {
    next_slab = slab->next;
    os::FreeDataPages(const_cast<void *>(reinterpret_cast<const void *>(slab)),
                      SLAB_ALLOCATOR_SLAB_SIZE_PAGES);
  }
}

// Allocate an object from the free list, if possible. Returns `nullptr` if
// no object can be allocated.
void *SlabAllocator::AllocateFromFreeList(void) {
  FreeList *head(nullptr);
  FreeList *next(nullptr);
  do {
    head = free_list.load(std::memory_order_relaxed);
    if (!head) {
      return nullptr;
    }
    next = head->next;
  } while (!free_list.compare_exchange_strong(head, next));
#ifdef GRANARY_TARGET_debug
  if (head) {
    // Maintain the invariant that is checked by `MemoryNotInUse`.
    memset(head, DEALLOCATED_MEMORY_POISON, sizeof (void *));
  }
#endif  // GRANARY_TARGET_debug
  return head;
}

#else

const SlabList *SlabAllocator::AllocateSlab(const SlabList *) {
  return nullptr;
}

const SlabList *SlabAllocator::GetOrAllocateSlab(size_t) {
  return nullptr;
}

extern "C" {

void *malloc(size_t size);
void free(void *);

}  // extern C

// Allocate some memory from the slab allocator.
void *SlabAllocator::Allocate(void) {
  return malloc(unaligned_size);
}

// Free some memory that was allocated from the slab allocator.
void SlabAllocator::Free(void *address) {
  free(address);
}

void SlabAllocator::Destroy(void) {}
void *SlabAllocator::AllocateFromFreeList(void) {
  return nullptr;
}


#endif  // GRANARY_WITH_VALGRIND

}  // namespace internal
}  // namespace granary
