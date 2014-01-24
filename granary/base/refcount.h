/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_REFCOUNT_H_
#define GRANARY_BASE_REFCOUNT_H_

#include "granary/breakpoint.h"

namespace granary {

// Implements intrusive reference counting for an object. The concept here is
// that some object owns a reference counted object
class UnownedCountedObject {
#ifdef GRANARY_INTERNAL
 public:
  virtual ~UnownedCountedObject(void) = default;
  inline UnownedCountedObject(void)
      : count(0) {}

  inline void Acquire(void) {
    ++count;
  }

  inline void Release(void) {
    granary_break_on_fault_if(0 > --count);
  }

  inline bool CanDestroy(void) const {
    return 0 >= count;
  }
#endif  // GRANARY_INTERNAL

 private:
  int count;
};

}  // namespace granary

#endif  // GRANARY_BASE_REFCOUNT_H_
