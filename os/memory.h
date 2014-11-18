/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_MEMORY_H_
#define OS_MEMORY_H_

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/pc.h"

#include "granary/breakpoint.h"

namespace granary {
namespace os {

// Initialize the Granary heap.
void InitHeap(void);

// Destroys the Granary heap.
void ExitHeap(void);

// Allocates `num` number of readable/writable pages.
void *AllocateDataPages(size_t num);

// Frees `num` pages back to the OS.
void FreeDataPages(void *, size_t num);

// Allocates `num` number of executable pages from the block code cache.
CachePC AllocateBlockCachePages(size_t num);

// Frees `num` pages back to the block code cache.
void FreeBlockCachePages(CachePC, size_t num);

// Allocates `num` number of executable pages from the block code cache.
CachePC AllocateEdgeCachePages(size_t num);

// Frees `num` pages back to the block code cache.
void FreeEdgeCachePages(CachePC, size_t num);

// A single page-aligned data structure.
struct alignas(arch::PAGE_SIZE_BYTES) PageFrame {
  uint8_t memory[arch::PAGE_SIZE_BYTES];
};

// Used to dynamically allocate pages from a heap.
template <int kNumPages>
struct DynamicHeap {
 public:
  explicit DynamicHeap(void *heap_)
      : num_allocated_pages(ATOMIC_VAR_INIT(0U)),
        free_pages_lock(),
        heap(reinterpret_cast<PageFrame *>(heap_)) {}

  void *AllocatePages(size_t num);
  void FreePages(void *addr, size_t num);

 private:
  DynamicHeap(void) = delete;

  void *AllocatePagesSlow(size_t num);
  void FreePage(uintptr_t addr);

  enum {
    NUM_PAGES_IN_HEAP = kNumPages,
    NUM_BITS_PER_FREE_SET_SLOT = 64,
    NUM_SLOTS_IN_FREE_SET = NUM_PAGES_IN_HEAP / NUM_BITS_PER_FREE_SET_SLOT
  };

  // Bitset of free pages. Free pages are marked as set bits. This is only
  // queried if no more pages remain to be allocated from the main heap.
  uint64_t free_pages[NUM_SLOTS_IN_FREE_SET];

  // Number of allocated pages.
  std::atomic<size_t> num_allocated_pages;

  // Lock on reading/modifying `free_pages`.
  SpinLock free_pages_lock;

  // Pages in the heap;
  PageFrame *heap;
};

// Implements a fixed-size heap that dishes out memory at the page granularity.
template <int kNumPages>
struct StaticHeap : public DynamicHeap<kNumPages> {
 public:
  StaticHeap(void)
      : DynamicHeap<kNumPages>(&(heap_pages[0])) {}

 protected:
  PageFrame heap_pages[kNumPages];
};

// Perform a slow scan of all free pages and look for a sequence of `num` set
// bits in `free_pages` that can be allocated. This uses first-fit to find
// the requested memory.
//
// Note: This is not able to allocate logically consecutive free pages if those
//       pages cross two slots.
template <int kNumPages>
void *DynamicHeap<kNumPages>::AllocatePagesSlow(size_t num) {
  SpinLockedRegion locker(&free_pages_lock);
  auto i = 0U;
  auto first_set_bit_reset = static_cast<uint32_t>(NUM_BITS_PER_FREE_SET_SLOT);
  auto first_set_bit = first_set_bit_reset;

  for (; i < NUM_SLOTS_IN_FREE_SET; ++i) {
    if (!free_pages[i]) {
      continue;  // Nothing freed on this group of pages.
    }

    first_set_bit = first_set_bit_reset;
    for (auto bit = 0U; bit < NUM_BITS_PER_FREE_SET_SLOT; ++bit) {
      if (free_pages[i] & (1UL << bit)) {
        if (bit < first_set_bit) {  // First free page found.
          first_set_bit = bit;
        }

      // Hit the first `0` bit after a run of `1`s.
      } else if (first_set_bit < bit) {
        if (num <= (bit - first_set_bit)) {
          goto allocate;  // Found at lease `num` contiguous free pages.
        } else {
          first_set_bit = first_set_bit_reset;
        }
      }
    }

    if ((num <= (NUM_BITS_PER_FREE_SET_SLOT - first_set_bit))) {
      goto allocate;
    }
  }

  GRANARY_ASSERT(false);
  return nullptr;

allocate:
  for (auto bit = first_set_bit; bit < (first_set_bit + num); ++bit) {
    free_pages[i] &= ~(1UL << bit);
  }

  // Return the allocated memory.
  return &(heap[first_set_bit + (i * NUM_BITS_PER_FREE_SET_SLOT)]);
}

// Free a page. Assumes that `free_pages_lock` is held.
template <int kNumPages>
void DynamicHeap<kNumPages>::FreePage(uintptr_t addr) {
  auto base = reinterpret_cast<uintptr_t>(&(heap[0]));
  auto slot = (addr - base) / NUM_BITS_PER_FREE_SET_SLOT;
  auto bit = (addr - base) % NUM_BITS_PER_FREE_SET_SLOT;
  free_pages[slot] |= (1UL << bit);
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
template <int kNumPages>
void *DynamicHeap<kNumPages>::AllocatePages(size_t num) {
  auto index = num_allocated_pages.fetch_add(num);
  void *mem = nullptr;
  if (GRANARY_LIKELY(NUM_PAGES_IN_HEAP > (index + num))) {
    mem = &(heap[index]);
  } else {
    mem = AllocatePagesSlow(num);
  }
  return mem;
}

// Frees `num` pages back to the OS.
template <int kNumPages>
void DynamicHeap<kNumPages>::FreePages(void *addr, size_t num) {
  auto addr_uint = reinterpret_cast<uintptr_t>(addr);
  auto num_pages = static_cast<uintptr_t>(num);
  SpinLockedRegion locker(&free_pages_lock);
  for (auto i(0UL); i < num_pages; ++i) {
    FreePage(addr_uint + (i * arch::PAGE_SIZE_BYTES));
  }
}

}  // namespace os
}  // namespace granary

#endif  // OS_MEMORY_H_
