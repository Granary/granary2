/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_TINY_SET_H_
#define GRANARY_BASE_TINY_SET_H_

#include "granary/base/tiny_vector.h"

namespace granary {

// Implements a dynamically sized set for a very small number of elements.
//
// Note: This assumes that `T::operator==` is defined.
//
// Note: This does not add default-constructed values to the set.
template <typename T, uintptr_t kMinSize>
class TinySet {
 public:
  TinySet(void)
      : elems() {}

  TinySet(const TinySet<T, kMinSize> &that)
      : elems(that.elems) {}

  bool Contains(T elem) const {
    for (const auto e : elems) {
      if (e == elem) return true;
    }
    return false;
  }

  unsigned long Size(void) const {
    return elems.Size();
  }

  void Add(T elem) {
    if (!Contains(elem)) {
      elems.Append(elem);
    }
  }

  void Remove(T elem) {
    for (auto &e : elems) {
      if (e == elem) {
        e = T();
      }
    }
  }

  typedef TinyVector<T, kMinSize> VecT;
  typedef decltype(static_cast<VecT *>(nullptr)->begin()) Iterator;
  typedef decltype(static_cast<const VecT *>(nullptr)->begin()) ConstIterator;

  Iterator begin(void) {
    return elems.begin();
  }

  Iterator end(void) {
    return elems.end();
  }

  ConstIterator begin(void) const {
    return elems.begin();
  }

  ConstIterator end(void) const {
    return elems.end();
  }

 private:
  VecT elems;
};

}  // namespace granary

#endif  // GRANARY_BASE_TINY_SET_H_
