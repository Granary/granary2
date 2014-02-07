/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"
#include "granary/memory.h"

extern "C" {

// Linux kernel interfaces for changing the page protection of memory.
// From: arch/x86/mm/pageattr.c.
//
// TODO(pag): These are not cross-platform.
// TODO(pag): These APIs do not provide mutual exclusion over modifying page
//            protection. This could lead to issues both where two instances
//            of Granary modify the protections of nearby memory, and where
//            the Granary and the kernel compete to modify some mappings.
extern int set_memory_x(unsigned long addr, int numpages);
extern int set_memory_nx(unsigned long addr, int numpages);

extern int set_memory_ro(unsigned long addr, int numpages);
extern int set_memory_rw(unsigned long addr, int numpages);
}

namespace granary {
namespace {

enum : uint32_t {
  NUM_PAGES_IN_HEAP = 64U,
  NUM_BITS_PER_FREE_SET_SLOT = 32U,
  NUM_SLOTS_IN_FREE_SET = NUM_PAGES_IN_HEAP / NUM_BITS_PER_FREE_SET_SLOT
};

struct {
  alignas(GRANARY_ARCH_PAGE_FRAME_SIZE)
  uint8_t memory[GRANARY_ARCH_PAGE_FRAME_SIZE];
} static heap[NUM_PAGES_IN_HEAP];

// Bitset of free pages. Free pages are marked as set bits. This is only
// queried if no more pages remain to be allocated from the main heap.
static uint32_t free_pages[NUM_SLOTS_IN_FREE_SET] = {0};

// Lock on reading/modifying `free_pages`.
static FineGrainedLock free_pages_lock;

// Number of allocated pages.
std::atomic<unsigned long> num_allocated_pages = ATOMIC_VAR_INIT(0);

// Perform a slow scan of all free pages and look for a sequence of `num` set
// bits in `free_pages` that can be allocated. This uses first-fit to find
// the requested memory.
//
// Note: This is not able to allocate logically consecutive free pages if those
//       pages cross two slots.
static void *AllocatePagesSlow(uint32_t num) {
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
static void FreePage(uintptr_t addr) {
  auto base = reinterpret_cast<uintptr_t>(&(heap[0]));
  auto slot = (addr - base) / NUM_BITS_PER_FREE_SET_SLOT;
  auto bit = (addr - base) % NUM_BITS_PER_FREE_SET_SLOT;
  free_pages[slot] |= (1 << bit);
}

}  // namespace

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num) {
  auto index = num_allocated_pages.fetch_add(static_cast<unsigned long>(num));
  void *mem = nullptr;
  if (GRANARY_LIKELY(NUM_PAGES_IN_HEAP > index)) {
    mem = &(heap[index]);
  } else {
    mem = AllocatePagesSlow(static_cast<uint32_t>(num));
  }
  ProtectPages(mem, num, MemoryProtection::MEMORY_READ_WRITE);
  return mem;
}

// Frees `num` pages back to the OS.
void FreePages(void *addr, int num) {
  auto addr_uint = reinterpret_cast<uintptr_t>(addr);
  auto num_pages = static_cast<uintptr_t>(num);
  FineGrainedLocked locker(&free_pages_lock);
  for (auto i(0UL); i < num_pages; ++i) {
    FreePage(addr_uint + (i * GRANARY_ARCH_PAGE_FRAME_SIZE));
  }
}

// Changes the memory protection of some pages.
void ProtectPages(void *addr_, int num, MemoryProtection prot) {
  auto addr = reinterpret_cast<unsigned long>(addr_);
  if (MemoryProtection::MEMORY_EXECUTABLE == prot) {
    set_memory_ro(addr, num);
    set_memory_x(addr, num);
  } else if (MemoryProtection::MEMORY_READ_ONLY == prot) {
    set_memory_ro(addr, num);
  } else if (MemoryProtection::MEMORY_READ_WRITE == prot) {
    set_memory_rw(addr, num);
  }
}

}  // namespace granary
