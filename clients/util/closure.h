/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_UTIL_CLOSURE_H_
#define CLIENTS_UTIL_CLOSURE_H_

#include <granary.h>

namespace detail {
// Linked list of closures that can be used for a generic hooking mechanism.
class Closure {
 public:
  explicit Closure(uintptr_t callback_addr_);
  ~Closure(void);

  GRANARY_DEFINE_NEW_ALLOCATOR(Closure, {
    SHARED = true,
    ALIGNMENT = 32
  })

  Closure *next;
  uintptr_t callback_addr;

 private:
  Closure(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Closure);
};
}  // namespace detail

template <typename... Args>
class ClosureList {
 public:
  typedef void (CallbackType)(Args...);
  typedef detail::Closure ClosureType;
  typedef granary::LinkedListIterator<ClosureType> ClosureTypeIterator;

  ClosureList(void)
      : first(nullptr),
        next_ptr(&first) {}

  ~ClosureList(void) {
    FreeAll();
  }

  // Reset the closure list to its initial state.
  void Reset(void) {
    FreeAll();
    first = nullptr;
    next_ptr = &first;
  }

  // Add a new closure to the closure list.
  void Add(void (*callback)(Args...)) {
    auto closure = new detail::Closure(reinterpret_cast<uintptr_t>(callback));
    granary::SpinLockedRegion locker(&lock);
    *next_ptr = closure;
    next_ptr = &(closure->next);
  }

  // Apply all closures to some arguments.
  inline void ApplyAll(Args... args) const {
    for (auto closure : ClosureTypeIterator(first)) {
      reinterpret_cast<CallbackType *>(closure->callback_addr)(args...);
    }
  }

  inline bool IsEmpty(void) const {
    return nullptr == first;
  }

 private:
  void FreeAll(void) {
    for (ClosureType *next_closure(nullptr); first; first = next_closure) {
      next_closure = first->next;
      delete first;
    }
  }

  granary::SpinLock lock;
  ClosureType *first;
  ClosureType **next_ptr;
};

#endif  // CLIENTS_UTIL_CLOSURE_H_
