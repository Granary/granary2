/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_CONTAINER_H_
#define GRANARY_BASE_CONTAINER_H_

#include "granary/base/base.h"

namespace granary {

// Generic container for some data, where we can late-initialize the data in
// the container.
template <typename T>
class Container {
 public:
  Container(void) = default;

  // Destroy the contained object.
  ~Container(void) {
    operator->()->~T();
  }

  // Construct the contained object.
  template <typename... Args>
  void Construct(Args... args) {
    new (operator->()) T(args...);
  }

  inline T *operator->(void) {
    return reinterpret_cast<T *>(&(storage[0]));
  }

  inline const T *operator->(void) const {
    return reinterpret_cast<const T *>(&(storage[0]));
  }

 private:
  alignas(T) uint8_t storage[sizeof(T)];

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(Container, (T));
};

}  // namespace granary

#endif  // GRANARY_BASE_CONTAINER_H_
