/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_CONTAINER_H_
#define GRANARY_BASE_CONTAINER_H_

#include "granary/base/base.h"
#include "granary/base/string.h"

namespace granary {

// Generic container for some data, where we can late-initialize the data in
// the container.
template <typename T>
class Container {
 public:
  Container(void) = default;
  ~Container(void) = default;

  // Destroy the contained object.
  void Destroy(void) {
    operator->()->~T();
    memset(storage, 0xAB, sizeof(T));
  }

  // Construct the contained object.
  template <typename... Args>
  void Construct(Args&&... args) {
    new (operator->()) T(std::forward<Args>(args)...);
  }

  inline T *operator->(void) {
    return reinterpret_cast<T *>(&(storage[0]));
  }

  inline T *AddressOf(void) {
    return reinterpret_cast<T *>(&(storage[0]));
  }

  inline const T *operator->(void) const {
    return reinterpret_cast<const T *>(&(storage[0]));
  }

 private:
  alignas(T) uint8_t storage[sizeof(T)];

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(Container, (T));
};

// Semi-generic container for an internal Granary+ type, where the specifics
// of that type are not published to non-`GRANARY_INTERNAL` code.
template <typename T, unsigned long kSize, unsigned long kAlign=8>
class OpaqueContainer {
 public:
  typedef OpaqueContainer<T, kSize, kAlign> SelfT;

  OpaqueContainer(void) = default;
  OpaqueContainer(const SelfT &that) = default;  // NOLINT
  OpaqueContainer(SelfT &&that) = default;  // NOLINT

#ifdef GRANARY_INTERNAL
  // Copy construct the contained object.
  explicit OpaqueContainer(const T &that) {
    new (operator->()) T(that);
  }

  // Construct the contained object.
  template <typename... Args>
  void Construct(Args... args) {
    new (operator->()) T(args...);
  }

  inline T *AddressOf(void) {
    return reinterpret_cast<T *>(&(storage[0]));
  }

  inline const T *AddressOf(void) const {
    return reinterpret_cast<const T *>(&(storage[0]));
  }

  inline T *operator->(void) {
    return reinterpret_cast<T *>(&(storage[0]));
  }

  inline const T *operator->(void) const {
    return reinterpret_cast<const T *>(&(storage[0]));
  }
#endif  // GRANARY_INTERNAL

 private:
  alignas(kAlign) uint8_t storage[kSize];
};

}  // namespace granary

#endif  // GRANARY_BASE_CONTAINER_H_
