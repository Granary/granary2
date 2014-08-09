/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/container.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

namespace granary {
namespace os {

// A single page-aligned data structure.
struct alignas(arch::PAGE_SIZE_BYTES) PageFrame {
  uint8_t memory[arch::PAGE_SIZE_BYTES];
};

extern "C" {
extern PageFrame *(*linux_module_alloc)(unsigned long);
}

template <int kNumPages>
struct DynamicHeap {
 public:
  explicit DynamicHeap(PageFrame *heap_)
    : num_allocated_pages(ATOMIC_VAR_INIT(0U)),
      free_pages_lock(),
      heap(heap_) {}

  void *AllocatePages(int num);
  void FreePages(void *addr, int num);

 private:
  DynamicHeap(void) = delete;

  void *AllocatePagesSlow(int num);
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
  std::atomic<int> num_allocated_pages;

  // Lock on reading/modifying `free_pages`.
  FineGrainedLock free_pages_lock;

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
void *DynamicHeap<kNumPages>::AllocatePagesSlow(int num_) {
  FineGrainedLocked locker(&free_pages_lock);
  auto i = 0U;
  auto num = static_cast<uint32_t>(num_);
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
void *DynamicHeap<kNumPages>::AllocatePages(int num) {
  auto index = num_allocated_pages.fetch_add(num);
  void *mem = nullptr;
  if (GRANARY_LIKELY(NUM_PAGES_IN_HEAP > index)) {
    mem = &(heap[index]);
  } else {
    mem = AllocatePagesSlow(num);
  }
  os::ProtectPages(mem, num, os::MemoryProtection::READ_WRITE);
  return mem;
}

// Frees `num` pages back to the OS.
template <int kNumPages>
void DynamicHeap<kNumPages>::FreePages(void *addr, int num) {
  auto addr_uint = reinterpret_cast<uintptr_t>(addr);
  auto num_pages = static_cast<uintptr_t>(num);
  FineGrainedLocked locker(&free_pages_lock);
  for (auto i(0UL); i < num_pages; ++i) {
    FreePage(addr_uint + (i * arch::PAGE_SIZE_BYTES));
  }
}

namespace {
enum {
  NUM_RW_PAGES = 1024,  // 4 MB.
  NUM_EXEC_PAGES = 2048  // 8 MB.
};
static Container<StaticHeap<NUM_RW_PAGES>> rw_memory;
static Container<DynamicHeap<NUM_EXEC_PAGES>> exec_memory;
}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {
  rw_memory.Construct();
  exec_memory.Construct(linux_module_alloc(
      NUM_EXEC_PAGES * arch::PAGE_SIZE_BYTES));
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num, MemoryIntent intent) {
  switch (intent) {
    case MemoryIntent::EXECUTABLE:
      return exec_memory->AllocatePages(num);
    case MemoryIntent::READ_WRITE:
      return rw_memory->AllocatePages(num);
  }
}

// Frees `num` pages back to the OS.
void FreePages(void *addr, int num, MemoryIntent intent) {
  switch (intent) {
    case MemoryIntent::EXECUTABLE:
      exec_memory->FreePages(addr, num);
      break;
    case MemoryIntent::READ_WRITE:
      rw_memory->FreePages(addr, num);
      break;
  }
}

// Changes the memory protection of some pages.
void ProtectPages(void *, int, MemoryProtection) {}

}  // namespace os
}  // namespace granary
