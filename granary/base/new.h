/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_NEW_H_
#define GRANARY_BASE_NEW_H_

#include <new>

#include "granary/arch/base.h"
#include "granary/base/base.h"

namespace granary {

// Defines a global new allocator for a specific class.
#define GRANARY_DEFINE_NEW_ALLOCATOR(class_name, ...) \
 private: \
  friend class OperatorNewAllocator<class_name>; \
  enum class OperatorNewProperties : size_t __VA_ARGS__ ; \
 public: \
  static void *operator new(std::size_t) { \
    return OperatorNewAllocator<class_name>::Allocate(); \
  } \
  static void operator delete(void *addr, std::size_t) { \
    OperatorNewAllocator<class_name>::Free(addr); \
  } \
  static void *operator new[](std::size_t) = delete; \
  static void operator delete[](void *, std::size_t) = delete


namespace detail {

// Dummy singly-linked list for free objects.
class FreeList {
 public:
  FreeList *next;
};


// Meta-data for a memory slab. The meta-data of each slab knows the range of
// allocation numbers that can be serviced by the allocator.
class SlabList {
 public:
  SlabList(const SlabList *next_slab_, size_t min_allocation_number_,
           size_t slab_number_);

  const SlabList * const next;
  const size_t min_allocation_number;
  const size_t number;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SlabList);
};


// Simple, lock-free allocator. This allocator operates at a page granularity,
// where each page begins with some meta-data (`SlabList`) and then contains
// the (potentially) allocated data on the page.
class SlabAllocator {
 public:
  explicit SlabAllocator(size_t num_allocations_per_slab_,
                         size_t start_offset_,
                         size_t unaligned_size_,
                         size_t aligned_size_);

  void *Allocate(void);
  void Free(void *address);

 private:
  void *AllocateFromFreeList(void);

  const SlabList *AllocateSlab(const SlabList *prev_slab);
  const SlabList *GetOrAllocateSlab(size_t slab_number);

  const size_t num_allocations_per_slab;
  const size_t start_offset;
  const size_t aligned_size;
  const size_t unaligned_size;
  const SlabList slab_list_tail;

  // TODO(pag): Padding between the atomic and non-atomic components?

  std::atomic<const SlabList *> slab_list_head;
  std::atomic<FreeList *> free_list;
  std::atomic<size_t> next_slab_number;
  std::atomic<size_t> next_allocation_number;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SlabAllocator);
};

}  // namespace detail


// Simple, slab-based allocator used to service operators `new` and `delete` for
// simple types. This allocator does not support allocating arrays of objects
template <typename T>
class OperatorNewAllocator {
 public:

  inline static void *Allocate(void) {
    return allocator.Allocate();
  }

  inline static void Free(void *address) {
    allocator.Free(address);
  }

 private:
  // Accesses the properties used to configure the allocator. Supported
  // properties include:
  //
  //    SHARED:     Should all CPUs/threads share this allocator, or should
  //                memory be divided into CPU- or thread-private slabs.
  //
  //    ALIGNMENT:  What should be the minimum alignment of the allocated
  //                objects? The allocator ensures that all objects are
  //                aligned to `MINIMUM_ALIGNMENT` bytes.
  typedef typename T::OperatorNewProperties Properties;

  enum : size_t {
    MIN_ALIGNMENT = static_cast<size_t>(Properties::ALIGNMENT),
    ALIGNMENT = GRANARY_MAX(MIN_ALIGNMENT, alignof(T)),
    OBJECT_SIZE = sizeof(T),
    MIN_OBJECT_SIZE = GRANARY_MAX(OBJECT_SIZE, sizeof(detail::FreeList *)),
    ALIGNED_OBJECT_SIZE = GRANARY_ALIGN_TO(MIN_OBJECT_SIZE, ALIGNMENT),

    // The first offset in a page is for an object.
    ALGINED_SLAB_LIST_SIZE = GRANARY_ALIGN_TO(OBJECT_SIZE, ALIGNMENT),

    // Figure out the number of allocations that can fit into a one-page slab.
    NUM_ALLOCS_FOR_META_DATA = ALGINED_SLAB_LIST_SIZE / ALIGNED_OBJECT_SIZE,
    NUM_ALLOCS_FOR_PAGE = GRANARY_ARCH_PAGE_FRAME_SIZE / ALIGNED_OBJECT_SIZE,
    NUM_ALLOCS_PER_SLAB = NUM_ALLOCS_FOR_PAGE - NUM_ALLOCS_FOR_META_DATA
  };

  OperatorNewAllocator(void) = delete;

  static detail::SlabAllocator allocator;

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(OperatorNewAllocator, (T));
};


// Static initialization of the (typeless) internal slab allocator.
template <typename T>
detail::SlabAllocator OperatorNewAllocator<T>::allocator(
    OperatorNewAllocator<T>::NUM_ALLOCS_PER_SLAB,
    OperatorNewAllocator<T>::ALGINED_SLAB_LIST_SIZE,
    OperatorNewAllocator<T>::ALIGNED_OBJECT_SIZE,
    OperatorNewAllocator<T>::OBJECT_SIZE);

}  // namespace granary

#endif  // GRANARY_BASE_NEW_H_
