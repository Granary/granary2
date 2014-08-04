/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

namespace granary {
namespace os {

// A single page-aligned data structure.
struct alignas(arch::PAGE_SIZE_BYTES) PageFrame {
  uint8_t memory[arch::PAGE_SIZE_BYTES];
};

// Implements a fixed-size heap that dishes out memory at the page granularity.
template <int kNumPages>
struct StaticHeap {
 public:
  StaticHeap(void);

  void *AllocatePages(int num);
  void FreePages(void *addr, int num);

 private:

  void *AllocatePagesSlow(int num);
  void FreePage(uintptr_t addr);

  enum : uint32_t {
    NUM_PAGES_IN_HEAP = kNumPages,
    NUM_BITS_PER_FREE_SET_SLOT = 32U,
    NUM_SLOTS_IN_FREE_SET = NUM_PAGES_IN_HEAP / NUM_BITS_PER_FREE_SET_SLOT
  };

  // Bitset of free pages. Free pages are marked as set bits. This is only
  // queried if no more pages remain to be allocated from the main heap.
  uint32_t free_pages[NUM_SLOTS_IN_FREE_SET];

  // Number of allocated pages.
  std::atomic<int> num_allocated_pages;

  // Lock on reading/modifying `free_pages`.
  FineGrainedLock free_pages_lock;

  PageFrame heap[kNumPages];
};

template <unsigned kNumPages>
StaticHeap<kNumPages>::StaticHeap(void)
    : num_allocated_pages(ATOMIC_VAR_INIT(0U)),
      free_pages_lock() {}

// Perform a slow scan of all free pages and look for a sequence of `num` set
// bits in `free_pages` that can be allocated. This uses first-fit to find
// the requested memory.
//
// Note: This is not able to allocate logically consecutive free pages if those
//       pages cross two slots.
template <int kNumPages>
void *StaticHeap<kNumPages>::AllocatePagesSlow(int num) {
  FineGrainedLocked locker(&free_pages_lock);
  auto i = 0U;
  auto first_set_bit_reset = static_cast<uint32_t>(NUM_BITS_PER_FREE_SET_SLOT);
  auto first_set_bit = first_set_bit_reset;

  for (; i < NUM_SLOTS_IN_FREE_SET; ++i) {
    if (!free_pages[i]) {
      continue;  // Nothing freed on this group of pages.
    }

    first_set_bit = first_set_bit_reset;
    for (auto bit = 0U; bit < NUM_BITS_PER_FREE_SET_SLOT; ++bit) {
      if (free_pages[i] & (1 << bit)) {
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

  granary_break_on_fault();
  return nullptr;

allocate:
  for (auto bit = first_set_bit; bit < (first_set_bit + num); ++bit) {
    free_pages[i] &= ~(1 << bit);
  }

  // Return the allocated memory.
  return &(heap[first_set_bit + (i * NUM_BITS_PER_FREE_SET_SLOT)]);
}

// Free a page. Assumes that `free_pages_lock` is held.
template <int kNumPages>
void StaticHeap<kNumPages>::FreePage(uintptr_t addr) {
  auto base = reinterpret_cast<uintptr_t>(&(heap[0]));
  auto slot = (addr - base) / NUM_BITS_PER_FREE_SET_SLOT;
  auto bit = (addr - base) % NUM_BITS_PER_FREE_SET_SLOT;
  free_pages[slot] |= (1 << bit);
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
template <int kNumPages>
void *StaticHeap<kNumPages>::AllocatePages(int num) {
  auto index = num_allocated_pages.fetch_add(static_cast<unsigned long>(num));
  void *mem = nullptr;
  if (GRANARY_LIKELY(NUM_PAGES_IN_HEAP > index)) {
    mem = &(heap[index]);
  } else {
    mem = AllocatePagesSlow(static_cast<uint32_t>(num));
  }
  os::ProtectPages(mem, num, os::MemoryProtection::READ_WRITE);
  return mem;
}

// Frees `num` pages back to the OS.
template <int kNumPages>
void StaticHeap<kNumPages>::FreePages(void *addr, int num) {
  auto addr_uint = reinterpret_cast<uintptr_t>(addr);
  auto num_pages = static_cast<uintptr_t>(num);
  FineGrainedLocked locker(&free_pages_lock);
  for (auto i(0UL); i < num_pages; ++i) {
    FreePage(addr_uint + (i * GRANARY_ARCH_PAGE_FRAME_SIZE));
  }
}

namespace {
static StaticHeap<64> rw_memory;
static StaticHeap<1024> exec_memory;  // 4 MB.
static StaticHeap<64> staging_memory;
}  // namespace

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num, MemoryIntent intent) {
  switch (intent) {
    case MemoryIntent::EXECUTABLE:
      return exec_memory.AllocatePages(num);
    case MemoryIntent::READ_WRITE:
      return rw_memory.AllocatePages(num);
  }
}

// Frees `num` pages back to the OS.
void FreePages(void *addr, int num, MemoryIntent intent) {
  switch (intent) {
    case MemoryIntent::EXECUTABLE:
      exec_memory.FreePages(addr, num);
      break;
    case MemoryIntent::READ_WRITE:
      rw_memory.FreePages(addr, num);
      break;
  }
}

// Changes the memory protection of some pages.
void ProtectPages(void *, int, MemoryProtection) {
  // TODO(pag): Implement me.
}

}  // namespace os
}  // namespace granary
