/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_TINY_VECTOR_H_
#define GRANARY_BASE_TINY_VECTOR_H_

#include "granary/base/new.h"
#include "granary/base/string.h"
#include "granary/base/type_trait.h"

namespace granary {

// Implements a resizable array for a very small number of elements. TinyVectors
// guarantee that at least `kMinSize` can be placed in the vector before any
// dynamic allocation occurs.
template <typename T, uintptr_t kMinSize>
class TinyVector {
 public:
  typedef TinyVector<T, kMinSize> SelfType;

  // Iterator over the elements of a `TinyVector`.
  template <typename S, typename U>
  class Iterator {
   public:
    inline Iterator(void)
        : vec(nullptr),
          i(0) {}

    bool operator!=(const Iterator<S, U> &that) const {
      return vec != that.vec || i != that.i;
    }

    void operator++(void) {
      ++i;
      if (i < kMinSize) {
        // We don't have more values, so we're done iterating.
        if (vec->num_elems <= i) {
          vec = nullptr;
          i = 0;
        }

      // Last element in a full vector.
      } else if (kMinSize == vec->num_elems) {
        vec = nullptr;
        i = 0;

      // Looks like `vec` has a `next` pointer, follow it.
      } else {
        vec = vec->next;
        if (!vec->num_elems) {  // The `next` vector has nothing in it.
          vec = nullptr;
        }
        i = 0;
      }
    }

    inline U &operator*(void) {
      return vec->elems[i];
    }

   private:
    template <typename, unsigned long> friend class TinyVector;

    inline Iterator(S *vec_, uintptr_t i_)
        : vec(vec_),
          i(i_) {}

    S *vec;
    uintptr_t i;
  };

  TinyVector(void)
      : num_elems(0) {
    if (std::is_trivial<T>()) {
      memset(elems, 0, sizeof elems);
    }
  }

  ~TinyVector(void) {
    if (num_elems > kMinSize) {
      delete next;
      next = nullptr;
    }
  }

  template <typename I, typename EnableIf<IsInteger<I>::RESULT>::Type=0>
  inline T &operator[](I index) {
    return ElementAt(static_cast<uintptr_t>(index));
  }

  template <typename I, typename EnableIf<IsInteger<I>::RESULT>::Type=0>
  inline const T &operator[](I index) const {
    return ElementAt(static_cast<uintptr_t>(index));
  }

  // Returns the current size of the tiny vector.
  unsigned long Size(void) const {
    auto size = 0UL;
    auto curr = this;
    for (; curr; ) {
      if (curr->num_elems > kMinSize) {
        size += kMinSize;
        curr = curr->next;
      } else {
        size += curr->num_elems;
        break;
      }
    }
    return size;
  }

  // Append a value to the end of this tiny vector.
  T &Append(T val) {
    if (kMinSize > num_elems) {
      elems[num_elems++] = val;
      return elems[num_elems - 1];
    } else if (kMinSize == num_elems) {  // Need to grow!
      next = new SelfType;
    }
    return next->Append(val);
  }

  inline Iterator<SelfType, T> begin(void) {
    return Iterator<SelfType, T>(num_elems ? this : nullptr, 0);
  }

  inline Iterator<SelfType, T> end(void) {
    return Iterator<SelfType, T>();
  }

  inline Iterator<const SelfType, const T> begin(void) const {
    return Iterator<const SelfType, const T>(num_elems ? this : nullptr, 0);
  }

  inline Iterator<const SelfType, const T> end(void) const {
    return Iterator<const SelfType, const T>();
  }

  GRANARY_DEFINE_NEW_ALLOCATOR(SelfType, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  // Returns the element at a specific index.
  T &ElementAt(uintptr_t index) {
    auto curr = this;
    while (index > kMinSize) {
      curr = curr->next;
      index -= kMinSize;
    }
    return curr->elems[index];
  }

  // Returns the element at a specific index.
  const T &ElementAt(uintptr_t index) const {
    auto curr = this;
    while (index > kMinSize) {
      curr = curr->next;
      index -= kMinSize;
    }
    return curr->elems[index];
  }

  T elems[kMinSize];

  union {
    uintptr_t num_elems;
    SelfType *next;
  };
};

}  // namespace granary

#endif  // GRANARY_BASE_TINY_VECTOR_H_
