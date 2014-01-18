/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <new>

#ifndef GRANARY_BASE_NEW_H_
#define GRANARY_BASE_NEW_H_

namespace granary {

// Define a global new allocator for a specific class.
#define GRANARY_DEFINE_NEW_ALLOCATOR(class_name, ...) \
 private: \
  template <typename> friend class OperatorNewAllocator; \
  enum class OperatorNewProperties __VA_ARGS__ ; \
 public: \
  static void *operator new(std::size_t) { \
    return OperatorNewAllocator<class_name>::Allocate(); \
  } \
  static void operator delete(void *addr) { \
    OperatorNewAllocator<class_name>::Free(addr); \
  } \
  static void *operator new[](std::size_t) = delete; \
  static void operator delete[](void *) = delete


enum class AllocationKind : int {
  EXECUTABLE_EXTERNAL,
  EXECUTABLE_INTERNAL,
  NOT_EXECUTABLE
};


// Defines a generic allocator helper for overloads of operator new.
template <typename T>
class OperatorNewAllocator {
 public:

  static T *Allocate(void) {
    return nullptr;
  }

  static void Free(void *address) {

  }

 private:
  // Accesses the properties used to configure the allocator. Supported
  // properties include:
  //
  //    SHARED:             Should all CPUs/threads share this allocator, or
  //                        should memory be divided into CPU- or thread-private
  //                        slabs.
  //
  //    MINIMUM_ALIGNMENT:  What should be the minimum alignment of the
  //                        allocated objects? The allocator ensures that all
  //                        objects are aligned to `MINIMUM_ALIGNMENT` bytes.
  //
  //    KIND:
  //      AllocationKind::EXECUTABLE_EXTERNAL
  //      AllocationKind::EXECUTABLE_INTERNAL
  //      AllocationKind::NOT_EXECUTABLE
  typedef typename T::OperatorNewProperties Properties;
};

}  // namespace granary

#endif  // GRANARY_BASE_NEW_H_
