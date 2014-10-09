/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_ARRAY_H_
#define GRANARY_BASE_ARRAY_H_

#include "granary/base/base.h"
#include "granary/breakpoint.h"

namespace granary {

// Iterator for arrays.
template <typename T>
class ArrayIterator {
 public:
  inline ArrayIterator(T *curr_)
      : curr(curr_) {}

  inline T &operator*(void) const {
    return *curr;
  }

  inline void operator++(void) {
    ++curr;
  }

  inline bool operator!=(const ArrayIterator<T> &that) const {
    return curr != that.curr;
  }

 private:
  ArrayIterator(void) = delete;

  T *curr;
};

// Represents some region of memory as being an array of type T.
template <typename T>
class Array {
 public:
  Array(T *base_, unsigned len_)
      : base(base_),
        len(len_) {}

  inline ArrayIterator<T> begin(void) const {
    return ArrayIterator<T>(base);
  }

  inline ArrayIterator<T> end(void) const {
    return ArrayIterator<T>(base + len);
  }

  inline T &operator[](unsigned i) const {
    GRANARY_ASSERT(i < len);
    return base[i];
  }

 private:
  Array(void) = delete;

  T *base;
  unsigned len;
};

}  // namespace granary

#endif  // GRANARY_BASE_ARRAY_H_
