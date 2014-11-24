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
SlabList::SlabList(const SlabList *next_slab_)
    : next(next_slab_) {}

// Initialize the slab allocator.
SlabAllocator::SlabAllocator(size_t start_offset_, size_t max_offset_,
                             size_t allocation_size_)
    : offset(max_offset_),
      start_offset(start_offset_),
      max_offset(max_offset_),
      allocation_size(allocation_size_),
      slab_list_lock(),
      slab_list(nullptr),
      free_list_lock(),
      free_list(nullptr) {}

#ifndef GRANARY_WITH_VALGRIND

// Constants that define how we will initialize various chunks of memory.
enum {
  UNALLOCATED_MEMORY_POISON = 0xAB,
  DEALLOCATED_MEMORY_POISON = 0xCD,
  UNINITIALIZED_MEMORY_POISON = 0xEF,
};

// Allocate a new slab of memory for this object. The backing memory of the
// slab is initialized to `UNALLOCATED_MEMORY_POISON`.
const SlabList *SlabAllocator::AllocateSlab(const SlabList *next_slab) {
  void *slab_memory(os::AllocateDataPages(SLAB_ALLOCATOR_SLAB_SIZE_PAGES));
  checked_memset(slab_memory, UNALLOCATED_MEMORY_POISON,
                 SLAB_ALLOCATOR_SLAB_SIZE_BYTES);
  return new (slab_memory) SlabList(next_slab);
}

// Get a pointer into the slab list. This potentially allocates a new slab.
const SlabList *SlabAllocator::SlabForAllocation(void) {
  if (GRANARY_UNLIKELY(offset >= max_offset)) {
    offset = start_offset;
    slab_list = AllocateSlab(slab_list);
  }
  return slab_list;
}

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
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
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test

// Allocate some memory from the slab allocator.
void *SlabAllocator::Allocate(void) {
  auto address = AllocateFromFreeList();
  if (!address) {
    SpinLockedRegion locker(&slab_list_lock);
    auto slab = SlabForAllocation();
    auto addr = reinterpret_cast<uintptr_t>(slab) + offset;
    offset += allocation_size;
    address = reinterpret_cast<void *>(addr);
  }
  GRANARY_ASSERT(MemoryNotInUse(address, allocation_size));
  checked_memset(address, UNINITIALIZED_MEMORY_POISON, allocation_size);
  VALGRIND_MALLOCLIKE_BLOCK(address, unaligned_size, 0, 1);
  return address;
}

// Free some memory that was allocated from the slab allocator.
void SlabAllocator::Free(void *address) {
  VALGRIND_FREELIKE_BLOCK(address, unaligned_size);
  checked_memset(address, DEALLOCATED_MEMORY_POISON, allocation_size);

  SpinLockedRegion locker(&free_list_lock);
  auto list = reinterpret_cast<FreeList *>(address);
  list->next = free_list;
  free_list = list;
}

// For those cases where a slab allocator is non-global.
void SlabAllocator::Destroy(void) {
  auto slab = slab_list;
  for (const SlabList *next_slab(nullptr); slab; slab = next_slab) {
    next_slab = slab->next;
    auto slab_addr = const_cast<void *>(
        reinterpret_cast<const void *>(slab_list));
    os::FreeDataPages(slab_addr, SLAB_ALLOCATOR_SLAB_SIZE_PAGES);
  }
  slab_list = nullptr;
}

// Allocate an object from the free list, if possible. Returns `nullptr` if
// no object can be allocated.
void *SlabAllocator::AllocateFromFreeList(void) {
  void *head(nullptr);
  {
    SpinLockedRegion locker(&free_list_lock);
    if (!free_list) return nullptr;
    auto list = free_list;
    free_list = list->next;
    head = list;
  }
#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
  // Maintain the invariant that is checked by `MemoryNotInUse`.
  memset(head, DEALLOCATED_MEMORY_POISON, sizeof (FreeList *));
#endif  // GRANARY_TARGET_debug
  return head;
}

#else

const SlabList *SlabAllocator::AllocateSlab(const SlabList *) {
  return nullptr;
}

const SlabList *SlabAllocator::SlabForAllocation(void) {
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
