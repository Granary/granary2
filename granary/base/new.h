/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_NEW_H_
#define GRANARY_BASE_NEW_H_

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"

namespace granary {

// Defines an in-line global new allocator for a specific class.
#define GRANARY_DEFINE_NEW_ALLOCATOR(class_name, ...) \
 private: \
  friend class ::granary::OperatorNewAllocator<class_name>; \
  enum class OperatorNewProperties : size_t __VA_ARGS__ ; \
 public: \
  static void *operator new(std::size_t, void *address) { \
    return address; \
  } \
  static void *operator new(std::size_t) { \
    return ::granary::OperatorNewAllocator<class_name>::Allocate(); \
  } \
  static void operator delete(void *address) { \
    ::granary::OperatorNewAllocator<class_name>::Free(address); \
  } \
  static void *operator new[](std::size_t) = delete; \
  static void operator delete[](void *) = delete;

// Internal-only definition of new allocators. When seen externally, the
// symbols are deleted.
#ifdef GRANARY_INTERNAL
# define GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR GRANARY_DEFINE_NEW_ALLOCATOR
# define GRANARY_DECLARE_INTERNAL_NEW_ALLOCATOR GRANARY_DECLARE_NEW_ALLOCATOR
#else
# define GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(...)
# define GRANARY_DECLARE_INTERNAL_NEW_ALLOCATOR(...)
#endif

// Declares an out-of-line global new allocator for a specific class. This
// is useful for exporting allocators from granary to clients without actually
// exposing the size of the class.
#define GRANARY_DECLARE_NEW_ALLOCATOR(class_name, ...) \
 GRANARY_IF_INTERNAL( private: \
  friend class ::granary::OperatorNewAllocator<class_name>; \
  enum class OperatorNewProperties : size_t __VA_ARGS__ ; ) \
 public: \
  static void *operator new(std::size_t, void *); \
  static void *operator new(std::size_t); \
  static void operator delete(void *address); \
  static void *operator new[](std::size_t) = delete; \
  static void operator delete[](void *) = delete;

// Defines an out-of-line global new allocator for a specific class.
#define GRANARY_IMPLEMENT_NEW_ALLOCATOR(class_name) \
  void *class_name::operator new(std::size_t, void *address) { \
    return address; \
  } \
  void *class_name::operator new(std::size_t) { \
    return ::granary::OperatorNewAllocator<class_name>::Allocate(); \
  } \
  void class_name::operator delete(void *address) { \
    ::granary::OperatorNewAllocator<class_name>::Free(address); \
  }

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
  explicit SlabList(const SlabList *next_slab_);
  const SlabList * const next;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SlabList);
};

enum {
  kNewAllocatorNumPagesPerSlab = 2,
  kNewAllocatorNumBytesPerSlab = arch::PAGE_SIZE_BYTES *
                                 kNewAllocatorNumPagesPerSlab
};

// Simple, lock-free allocator. This allocator operates at a page granularity,
// where each page begins with some meta-data (`SlabList`) and then contains
// the (potentially) allocated data on the page.
class SlabAllocator {
 public:
  SlabAllocator(size_t start_offset_, size_t max_offset_,
                size_t allocation_size_, size_t object_size_);

  ~SlabAllocator(void);

  void *Allocate(void);
  void Free(void *address);

 private:
  void *AllocateFromFreeList(void);

  const SlabList *SlabForAllocation(void);

  size_t offset;
  const size_t start_offset;
  const size_t max_offset;
  const size_t allocation_size;
  const size_t object_size;

  alignas(arch::CACHE_LINE_SIZE_BYTES) SpinLock slab_list_lock;
  const SlabList *slab_list;

  alignas(arch::CACHE_LINE_SIZE_BYTES) SpinLock free_list_lock;
  FreeList *free_list;

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
    OBJECT_SIZE = GRANARY_MAX(sizeof(T), sizeof(internal::FreeList *)),
    REQUESTED_ALIGNMENT = static_cast<size_t>(Properties::ALIGNMENT),
    OBJECT_ALIGNMENT = alignof(T),
    MINIMUM_ALIGNMENT = GRANARY_MAX(REQUESTED_ALIGNMENT, OBJECT_ALIGNMENT),
    ALIGNED_SIZE = GRANARY_ALIGN_TO(sizeof(T), MINIMUM_ALIGNMENT),
    START_OFFSET = GRANARY_ALIGN_TO(sizeof(internal::SlabList),
                                    MINIMUM_ALIGNMENT),
    NUM_OBJS_PER_SLAB = (internal::kNewAllocatorNumBytesPerSlab -
                         START_OFFSET - (ALIGNED_SIZE - 1)) / ALIGNED_SIZE,
    END_OFFSET = START_OFFSET + (NUM_OBJS_PER_SLAB * ALIGNED_SIZE)
  };

  static_assert(alignof(T) <= MINIMUM_ALIGNMENT,
      "Error computing the alignment of the object.");

  static_assert(sizeof(T) <= ALIGNED_SIZE,
      "Error computing the aligned object size.");

  static_assert(END_OFFSET <= internal::kNewAllocatorNumBytesPerSlab,
      "Error computing the layout of meta-data and objects on page frames.");

  OperatorNewAllocator(void) = delete;

  static internal::SlabAllocator allocator GRANARY_EARLY_GLOBAL;

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(OperatorNewAllocator, (T));
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"

// Static initialization of the (typeless) internal slab allocator.
template <typename T>
internal::SlabAllocator OperatorNewAllocator<T>::allocator(
    OperatorNewAllocator<T>::START_OFFSET,
    OperatorNewAllocator<T>::END_OFFSET,
    OperatorNewAllocator<T>::ALIGNED_SIZE,
    sizeof(T));

#pragma clang diagnostic pop

}  // namespace granary

#endif  // GRANARY_BASE_NEW_H_
