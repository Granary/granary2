/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_REFCOUNT_H_
#define GRANARY_BASE_REFCOUNT_H_

#include "granary/base/base.h"
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
    count += 2;
  }

  // Mark this object as being permanently held onto by some object. This
  // permanence lives "outside" the normal reference chain, insofar as if
  // object A permanently holds onto B, but does not `Acquire` B, then then
  // all else being equal, B will have `NumReferences() = 0`.
  inline void MarkAsPermanent(void) {
    count |= 1;
  }

  inline void Release(void) {
    count -= 2;
    GRANARY_IF_DEBUG( granary_break_on_fault_if(0 > count); )
  }

  // Returns the number of objects referring to this object.
  inline int NumReferences(void) const {
    return count / 2;
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
