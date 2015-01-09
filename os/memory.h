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
CachePC AllocateCodePages(size_t num);

// Frees `num` pages back to the block code cache.
void FreeCodePages(CachePC, size_t num);

// A single page-aligned data structure.
struct alignas(arch::PAGE_SIZE_BYTES) PageFrame {
  uint8_t memory[arch::PAGE_SIZE_BYTES];
};

// Used to dynamically allocate pages from a heap.
template <size_t kNumPages>
struct PageAllocator {
 public:
  explicit PageAllocator(void *heap_)
      : num_allocated_pages(ATOMIC_VAR_INIT(0U)),
        free_pages_lock(),
        heap(reinterpret_cast<PageFrame *>(heap_)) {
    memset(free_pages, 0, sizeof free_pages);
  }

  void *AllocatePages(size_t num);
  void FreePages(void *mem, size_t num);

  void *BeginAddress(void) const {
    return heap;
  }
  void *EndAddress(void) const {
    return &(heap[kNumPages]);
  }

 private:
  PageAllocator(void) = delete;

  void *AllocatePagesSlow(size_t num);
  void FreePage(uintptr_t addr);

  enum {
    kNumPagesInHeap = kNumPages,
    kNumBitsPerFreeSetSlot = 64,
    kNumSlotsInFreeSet = (kNumPagesInHeap / kNumBitsPerFreeSetSlot) + 1
  };

  // Bitset of free pages. Free pages are marked as set bits. This is only
  // queried if no more pages remain to be allocated from the main heap.
  uint64_t free_pages[kNumSlotsInFreeSet];

  // Number of allocated pages.
  std::atomic<size_t> num_allocated_pages;

  // Lock on reading/modifying `free_pages`.
  SpinLock free_pages_lock;

  // Pages in the heap;
  PageFrame * const heap;
};

// Perform a slow scan of all free pages and look for a sequence of `num` set
// bits in `free_pages` that can be allocated. This uses first-fit to find
// the requested memory.
//
// Note: This is not able to allocate logically consecutive free pages if those
//       pages cross two slots.
template <size_t kNumPages>
void *PageAllocator<kNumPages>::AllocatePagesSlow(size_t num) {
  SpinLockedRegion locker(&free_pages_lock);
  auto i = 0U;
  auto first_set_bit_reset = static_cast<uint32_t>(kNumBitsPerFreeSetSlot);
  auto first_set_bit = first_set_bit_reset;

  for (; i < kNumSlotsInFreeSet; ++i) {
    if (!free_pages[i]) {
      continue;  // Nothing freed on this group of pages.
    }
    first_set_bit = first_set_bit_reset;
    for (auto bit = 0U; bit < kNumBitsPerFreeSetSlot; ++bit) {
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
    if ((num <= (kNumBitsPerFreeSetSlot - first_set_bit))) {
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
  return &(heap[first_set_bit + (i * kNumBitsPerFreeSetSlot)]);
}

// Free a page. Assumes that `free_pages_lock` is held.
template <size_t kNumPages>
void PageAllocator<kNumPages>::FreePage(uintptr_t addr) {
  auto base = reinterpret_cast<uintptr_t>(&(heap[0]));
  auto page = (addr - base) / arch::PAGE_SIZE_BYTES;
  auto slot = page / kNumBitsPerFreeSetSlot;
  auto bit = page % kNumBitsPerFreeSetSlot;
  free_pages[slot] |= (1UL << bit);
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
template <size_t kNumPages>
void *PageAllocator<kNumPages>::AllocatePages(size_t num) {
  auto index = num_allocated_pages.fetch_add(num);
  void *mem = nullptr;
  if (GRANARY_LIKELY(kNumPagesInHeap > (index + num))) {
    mem = &(heap[index]);
  } else {
    mem = AllocatePagesSlow(num);
  }
  GRANARY_IF_DEBUG( auto addr = reinterpret_cast<uint8_t *>(mem); );
  GRANARY_IF_DEBUG( auto heap_addr = reinterpret_cast<uint8_t *>(&(heap[0])));
  GRANARY_ASSERT(&(heap_addr[0]) <= addr &&
                 (&(heap_addr[kNumPages * arch::PAGE_SIZE_BYTES]) >=
                  &(addr[num * arch::PAGE_SIZE_BYTES])));
  return mem;
}

// Frees `num` pages back to the OS.
template <size_t kNumPages>
void PageAllocator<kNumPages>::FreePages(void *mem, size_t num) {
  GRANARY_IF_DEBUG( auto addr = reinterpret_cast<uint8_t *>(mem); );
  GRANARY_IF_DEBUG( auto heap_addr = reinterpret_cast<uint8_t *>(&(heap[0])));
  GRANARY_ASSERT(&(heap_addr[0]) <= addr &&
                 (&(heap_addr[kNumPages * arch::PAGE_SIZE_BYTES]) >=
                  &(addr[num * arch::PAGE_SIZE_BYTES])));
  auto addr_uint = reinterpret_cast<uintptr_t>(mem);
  SpinLockedRegion locker(&free_pages_lock);
  for (auto i = 0UL; i < num; ++i) {
    FreePage(addr_uint + (i * arch::PAGE_SIZE_BYTES));
  }
}

// The type of memory allocated by a particular page allocator.
enum MemoryType {
  kMemoryTypeRW,
  kMemoryTypeRWX
};

// An allocator for some statically specified number of pages of a specific
// type.
template <size_t kNumPages, typename Name, MemoryType kMemoryType>
class StaticPageAllocator;

template <size_t kNumPages, typename Name>
class StaticPageAllocator<kNumPages, Name, kMemoryTypeRW>
    : public PageAllocator<kNumPages> {
 public:
  StaticPageAllocator(void)
      : PageAllocator<kNumPages>(&(heap_pages[0])) {}

 protected:
  static PageFrame heap_pages[kNumPages];
};

template <size_t kNumPages, typename Name>
PageFrame __attribute__((section(".bss.granary_unprotected")))
StaticPageAllocator<kNumPages, Name, kMemoryTypeRW>::heap_pages[kNumPages];

// Implements a fixed-size heap that dishes out memory at the page granularity.
template <size_t kNumPages, typename Name>
class StaticPageAllocator<kNumPages, Name, kMemoryTypeRWX>
    : public PageAllocator<kNumPages> {
 public:
  StaticPageAllocator(void)
      : PageAllocator<kNumPages>(&(pages[0])) {}

 protected:
  static PageFrame pages[kNumPages];
};

template <size_t kNumPages, typename Name>
PageFrame __attribute__((section(".writable_text")))
StaticPageAllocator<kNumPages, Name, kMemoryTypeRWX>::pages[kNumPages];

}  // namespace os
}  // namespace granary

#endif  // OS_MEMORY_H_
