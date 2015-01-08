/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_TINY_VECTOR_H_
#define GRANARY_BASE_TINY_VECTOR_H_

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/string.h"
#include "granary/base/type_trait.h"

#include "granary/breakpoint.h"

namespace granary {

// Implements a dynamically sized array for a very small number of elements.
// `TinyVector`s guarantee that at least `kMinSize` can be placed in the vector
// before any dynamic memory allocation occurs.
//
// Note: This vector guarantees that elements in the vector will not be moved.
//       Therefore, a pointer to an element in the vector will remain the
//       same across vector resize operations.
template <typename T, uintptr_t kMinSize>
class TinyVector {
 public:
  typedef TinyVector<T, kMinSize> SelfType;

  // Iterator over the elements of a `TinyVector`.
  template <typename S, typename U>
  class Iterator {
   public:
    typedef U ElementType;

    inline Iterator(void)
        : vec(nullptr),
          i(0) {}

    bool operator!=(const Iterator<S, U> &that) const {
      return vec != that.vec || i != that.i;
    }

    void operator++(void) {
      GRANARY_ASSERT(nullptr != vec);
      ++i;
      if (i < kMinSize) {
        // We don't have more values, so we're done iterating.
        if (vec->num_elems <= i) {
          vec = nullptr;
          i = 0;
        }

      // `i >= kMinSize`; Last element in a full vector.
      } else if (kMinSize == vec->num_elems) {
        vec = nullptr;
        i = 0;

      // `i >= kMinSize`; Looks like `vec` has a `next` pointer, follow it.
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

    inline U operator*(void) const {
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
    static_assert(offsetof(SelfType, num_elems) == offsetof(SelfType, next),
                  "Invalid structure packing of `TinyVector<T>`.");
    if (std::is_scalar<T>() || std::is_trivial<T>()) {
      memset(elems, 0, sizeof elems);
    }
  }

  TinyVector(const TinyVector<T, kMinSize> &that)
      : num_elems(0) {
    if (std::is_scalar<T>() || std::is_trivial<T>()) {
      memset(elems, 0, sizeof elems);
    }
    for (auto &elem : that) {
      Append(elem);
    }
  }

  ~TinyVector(void) {
    if (num_elems > kMinSize) {
      delete next;
    }
    num_elems = 0;
  }

  SelfType &operator=(const SelfType &that) {
    if (this != &that) {
      if (num_elems) {
        this->~TinyVector();
      }
      if (that.num_elems) {
        new (this) SelfType(that);
      } else {
        new (this) SelfType;
      }
    }
    return *this;
  }

  // Clear out the entire vector.
  void Clear(void) {
    if (num_elems) {
      this->~TinyVector();
      new (this) SelfType;
    }
  }

  // For getting around syntax highlighting issues in Eclipse.
#ifdef GRANARY_ECLIPSE
  template <typename U> T &operator[](U);
  template <typename U> const T &operator[](U) const;
#else
  template <typename I, typename EnableIf<IsInteger<I>::RESULT>::Type=0>
  inline T &operator[](I index) {
    return ElementAt(static_cast<uintptr_t>(index));
  }

  template <typename I, typename EnableIf<IsInteger<I>::RESULT>::Type=0>
  inline const T &operator[](I index) const {
    return ElementAt(static_cast<uintptr_t>(index));
  }
#endif  // GRANARY_ECLIPSE

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
  T &Append(const T &val) {
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
    kAlignment = 1
  })

 private:
  // Returns the element at a specific index.
  //
  // Note: This function doesn't do overflow checking!
  T &ElementAt(uintptr_t index) {
    auto curr = this;
    while (index >= kMinSize) {
      curr = curr->next;
      index -= kMinSize;
    }
    return curr->elems[index];
  }

  // Returns the element at a specific index.
  //
  // Note: This function doesn't do overflow checking!
  const T &ElementAt(uintptr_t index) const {
    auto curr = this;
    while (index >= kMinSize) {
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
