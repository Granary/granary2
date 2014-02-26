/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_OPERATOR_H_
#define GRANARY_BASE_OPERATOR_H_

#include "granary/base/base.h"  // For placement new.

namespace granary {

// Initialize some meta-data.
template <typename T>
void Construct(void *mem) {
  new (mem) T;
}

// Initialize some meta-data.
template <typename T>
void CopyConstruct(void *mem, const void *that) {
  new (mem) T(*reinterpret_cast<const T *>(that));
}

// Destroy some meta-data.
template <typename T>
void Destruct(void *mem) {
  reinterpret_cast<T *>(mem)->~T();
}

}  // namespace granary

#endif  // GRANARY_BASE_OPERATOR_H_
