/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"
#include "granary/base/new.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/memory.h"

#include <cstdlib>

namespace granary {
namespace internal {

// Constants that define how we will initialize various chunks of memory.
enum {
  UNALLOCATED_MEMORY_POISON = 0xAB,
  DEALLOCATED_MEMORY_POISON = 0xBC,
  UNINITIALIZED_MEMORY_POISON = 0xCD,
};


#ifndef GRANARY_WITH_VALGRIND  // Use slab-style allocation.

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
                             size_t aligned_size_,
                             size_t unaligned_size_)
    : num_allocations_per_slab(num_allocations_per_slab_),
      start_offset(start_offset_),
      aligned_size(aligned_size_),
      unaligned_size(unaligned_size_),
      slab_list_tail(nullptr, num_allocations_per_slab_, 0),
      slab_list_head(ATOMIC_VAR_INIT(&slab_list_tail)),
      free_list(ATOMIC_VAR_INIT(nullptr)),
      next_slab_number(ATOMIC_VAR_INIT(1)),
      next_allocation_number(ATOMIC_VAR_INIT(num_allocations_per_slab_)) {

  GRANARY_UNUSED(unaligned_size);
}

// Allocate a new slab of memory for this object. The backing memory of the
// slab is initialized to `UNALLOCATED_MEMORY_POISON`.
const SlabList *SlabAllocator::AllocateSlab(const SlabList *prev_slab) {
  const size_t slab_number(next_slab_number.fetch_add(1));
  void *slab_memory(AllocatePages(SLAB_ALLOCATOR_SLAB_SIZE_PAGES));
  memset(slab_memory, UNALLOCATED_MEMORY_POISON,
         SLAB_ALLOCATOR_SLAB_SIZE_BYTES);
  VALGRIND_MAKE_MEM_UNDEFINED(
      reinterpret_cast<char *>(slab_memory) + start_offset,
      (SLAB_ALLOCATOR_SLAB_SIZE_BYTES - start_offset));
  return new (slab_memory) SlabList(
      prev_slab,
      slab_number * num_allocations_per_slab,
      slab_number);
}

// Get a pointer into the slab list. This potentially allocates a new slab.
// The slab pointer returned might not be the exact slab for the particular
// allocation.
const SlabList *SlabAllocator::GetOrAllocateSlab(size_t slab_number) {
  for (const SlabList *slab(nullptr);;) {
    slab = slab_list_head.load(std::memory_order_relaxed);
    if (GRANARY_LIKELY(slab->number >= slab_number)) {
      return slab;
    }

    // TODO(pag): In user space, this could behave poorly if the thread that is
    //            assigned to allocate the next slab is interrupted during the
    //            process of allocating the slab.
    if ((slab->number + 1) == slab_number) {
      slab = AllocateSlab(slab);
      slab_list_head.store(slab, std::memory_order_seq_cst);
      return slab;
    }
  }
  GRANARY_ASSERT(false);
}

// Allocate some memory from the slab allocator.
void *SlabAllocator::Allocate(void) {
  void *address(AllocateFromFreeList());
  if (!address) {
    const size_t allocation_number(next_allocation_number.fetch_add(1));
    const size_t slab_number(allocation_number / num_allocations_per_slab);
    const SlabList *slab(GetOrAllocateSlab(slab_number));

    while (GRANARY_UNLIKELY(slab->number > slab_number)) {
      slab = slab->next;
    }

    const size_t index((allocation_number - slab->min_allocation_number));
    address = UnsafeCast<char *>(slab) + start_offset + (index * aligned_size);
  }

  VALGRIND_MAKE_MEM_DEFINED(address, aligned_size);
  memset(address, UNINITIALIZED_MEMORY_POISON, aligned_size);
  VALGRIND_MAKE_MEM_UNDEFINED(address, aligned_size);
  return address;
}

// Free some memory that was allocated from the slab allocator.
void SlabAllocator::Free(void *address) {
  VALGRIND_MAKE_MEM_DEFINED(address, aligned_size);
  memset(address, DEALLOCATED_MEMORY_POISON, aligned_size);
  VALGRIND_MAKE_MEM_UNDEFINED(address, aligned_size);
  FreeList *list(reinterpret_cast<FreeList *>(address));
  FreeList *next(nullptr);

  do {
    next = free_list.load(std::memory_order_relaxed);
    VALGRIND_MAKE_MEM_DEFINED(list, sizeof *list);
    list->next = next;
    VALGRIND_MAKE_MEM_UNDEFINED(list, sizeof *list);
  } while (!free_list.compare_exchange_strong(next, list));
}

// For those cases where a slab allocator is non-global.
void SlabAllocator::Destroy(void) {
  const SlabList *slab = slab_list_head.exchange(nullptr);
  const SlabList *next_slab(nullptr);
  for (; slab; slab = next_slab) {
    next_slab = slab->next;
    FreePages(const_cast<void *>(reinterpret_cast<const void *>(slab)),
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
    VALGRIND_MAKE_MEM_DEFINED(head, sizeof *head);
    next = head->next;
    VALGRIND_MAKE_MEM_UNDEFINED(head, sizeof *head);
  } while (!free_list.compare_exchange_strong(head, next));
  return head;
}

#else	 // Use `malloc` and `free` to play nice with Valgrind.

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
                             size_t aligned_size_,
                             size_t unaligned_size_)
    : num_allocations_per_slab(num_allocations_per_slab_),
      start_offset(start_offset_),
      aligned_size(aligned_size_),
      unaligned_size(unaligned_size_),
      slab_list_tail(nullptr, num_allocations_per_slab_, 0),
      slab_list_head(ATOMIC_VAR_INIT(&slab_list_tail)),
      free_list(ATOMIC_VAR_INIT(nullptr)),
      next_slab_number(ATOMIC_VAR_INIT(1)),
      next_allocation_number(ATOMIC_VAR_INIT(num_allocations_per_slab_)) {

  GRANARY_UNUSED(unaligned_size);
}

// Allocate some memory from heap.
void *SlabAllocator::Allocate(void) {
  return malloc(aligned_size);
}

// Free some memory from heap.
void SlabAllocator::Free(void *address) {
  memset(address, internal::DEALLOCATED_MEMORY_POISON, aligned_size);
  free(address);
}

void SlabAllocator::Destroy(void) {}

#endif  // GRANARY_WITH_VALGRIND

}  // namespace internal
}  // namespace granary
