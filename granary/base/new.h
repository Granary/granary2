/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_NEW_H_
#define GRANARY_BASE_NEW_H_
#ifdef GRANARY_INTERNAL

#include <new>

#include "granary/arch/base.h"
#include "granary/base/base.h"

namespace granary {

// Redefines operators `new` and `delete` as deleted.
# define GRANARY_DISABLE_NEW_ALLOCATOR(class_name) \
  inline static void *operator new(std::size_t) { return nullptr; } \
  inline static void operator delete(void *) {}

// Defines a global new allocator for a specific class.
# define GRANARY_DEFINE_NEW_ALLOCATOR(class_name, ...) \
 private: \
  friend class OperatorNewAllocator<class_name>; \
  enum class OperatorNewProperties : size_t __VA_ARGS__ ; \
 public: \
  inline static void *operator new(std::size_t, void *address) { \
    return address; \
  } \
  static void *operator new(std::size_t) { \
    void *address(OperatorNewAllocator<class_name>::Allocate()); \
    VALGRIND_MALLOCLIKE_BLOCK(address, sizeof(class_name), 0, 0); \
    return address; \
  } \
  static void operator delete(void *address) { \
    OperatorNewAllocator<class_name>::Free(address); \
    VALGRIND_FREELIKE_BLOCK(address, sizeof(class_name)); \
  } \
  static void *operator new[](std::size_t) = delete; \
  static void operator delete[](void *) = delete;


namespace internal {

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

  alignas(GRANARY_ARCH_CACHE_LINE_SIZE) \
      std::atomic<const SlabList *> slab_list_head;

  std::atomic<FreeList *> free_list;
  std::atomic<size_t> next_slab_number;
  std::atomic<size_t> next_allocation_number;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SlabAllocator);
};

}  // namespace internal


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
    MIN_OBJECT_SIZE = GRANARY_MAX(OBJECT_SIZE, sizeof(internal::FreeList *)),
    ALIGNED_OBJECT_SIZE = GRANARY_ALIGN_TO(MIN_OBJECT_SIZE, ALIGNMENT),

    // The first offset in a page is for an object.
    ALGINED_SLAB_LIST_SIZE = GRANARY_ALIGN_TO(
        sizeof(internal::SlabList), ALIGNMENT),

    // Figure out the number of allocations that can fit into a one-page slab.
    MAX_ALLOCATABLE_SPACE = GRANARY_ARCH_PAGE_FRAME_SIZE -
                            ALGINED_SLAB_LIST_SIZE,
    NUM_ALLOCS_PER_SLAB = MAX_ALLOCATABLE_SPACE / ALIGNED_OBJECT_SIZE,

    // Figure out how much space can actually be used.
    PAGE_USAGE = ALGINED_SLAB_LIST_SIZE +
                 (NUM_ALLOCS_PER_SLAB * ALIGNED_OBJECT_SIZE)
  };

  static_assert(OBJECT_SIZE <= ALIGNED_OBJECT_SIZE,
      "Error computing the aligned object size.");

  static_assert(PAGE_USAGE <= GRANARY_ARCH_PAGE_FRAME_SIZE,
      "Error computing the layout of meta-data and objects on page frames.");

  OperatorNewAllocator(void) = delete;

  static internal::SlabAllocator allocator GRANARY_EARLY_GLOBAL;

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(OperatorNewAllocator, (T));
};


// Static initialization of the (typeless) internal slab allocator.
template <typename T>
internal::SlabAllocator OperatorNewAllocator<T>::allocator(
    OperatorNewAllocator<T>::NUM_ALLOCS_PER_SLAB,
    OperatorNewAllocator<T>::ALGINED_SLAB_LIST_SIZE,
    OperatorNewAllocator<T>::ALIGNED_OBJECT_SIZE,
    OperatorNewAllocator<T>::OBJECT_SIZE);

}  // namespace granary

#else
# define GRANARY_DISABLE_NEW_ALLOCATOR(class_name)
# define GRANARY_DEFINE_NEW_ALLOCATOR(class_name, ...)
#endif  // GRANARY_INTERNAL
#endif  // GRANARY_BASE_NEW_H_
