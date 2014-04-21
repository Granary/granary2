/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/big_vector.h"
#include "granary/base/list.h"
#include "granary/base/lock.h"
#include "granary/base/string.h"

#include "granary/memory.h"

namespace granary {
namespace {

// Get a pointer to an object within a slab.
inline static void *GetObjectPointer(void *base_pointer, size_t offset) {
  return &(reinterpret_cast<char *>(base_pointer)[offset]);
}

}  // namespace
namespace detail {

// The backing data behind a big vector.
class BigVectorSlab {
 public:
  explicit BigVectorSlab(size_t align);

  BigVectorSlab *next;
  void *first;
};

typedef LinkedListIterator<BigVectorSlab> SlabIterator;

// Initialize a big vector slab, with the number of pages
BigVectorSlab::BigVectorSlab(size_t align)
    : next(nullptr) {
  auto this_addr = reinterpret_cast<uintptr_t>(this);
  auto begin_addr = this_addr + sizeof(*this);
  auto first_addr = GRANARY_ALIGN_TO(begin_addr, align);
  first = reinterpret_cast<void *>(first_addr);
}

// Initialize the big vector.
BigVectorImpl::BigVectorImpl(size_t align_, size_t size_)
    : slabs(nullptr),
      next_slab(&slabs),
      align(align_),
      size(size_) {
  auto begin = GRANARY_ALIGN_TO(sizeof(BigVectorSlab), align);
  auto remaining = arch::PAGE_SIZE_BYTES - begin;
  num_objs_per_slab = remaining / size;
}

// Free all backing slabs.
BigVectorImpl::~BigVectorImpl(void) {
  for (BigVectorSlab *next(nullptr); slabs; slabs = next) {
    next = slabs->next;
    FreePages(slabs, 1, MemoryIntent::READ_WRITE);
  }
}

// Find a pointer to the first element in a slab that contains the element
// at index `index`.
void *BigVectorImpl::FindObjectPointer(size_t index) {
  for (auto offset = index; ; offset = index) {
    for (auto slab : SlabIterator(slabs)) {
      if (GRANARY_LIKELY(offset < num_objs_per_slab)) {
        return GetObjectPointer(slab->first, offset * size);
      } else {
        offset -= num_objs_per_slab;
      }
    }
    AllocateSlab();
  }
  return nullptr;
}

// Allocate a new slab.
void BigVectorImpl::AllocateSlab(void) {
  auto mem = AllocatePages(1, MemoryIntent::READ_WRITE);
  memset(mem, 0, arch::PAGE_SIZE_BYTES);
  auto slab = new (mem) detail::BigVectorSlab(align);

  // Chain it in.
  *next_slab = slab;
  next_slab = &(slab->next);
}

}  // namespace detail
}  // namespace granary
