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
  typedef TinySet<T, kMinSize> SelfType;

  TinySet(void)
      : elems(),
        size(0) {}

  TinySet(const TinySet<T, kMinSize> &that)
      : elems(that.elems),
        size(that.size) {}

  bool Contains(T elem) const {
    GRANARY_ASSERT(T() != elem);
    for (const auto e : elems) {
      if (e == elem) return true;
    }
    return false;
  }

  inline SelfType &operator=(const SelfType &that) {
    elems = that.elems;
    size = that.size;
    return *this;
  }

  size_t Size(void) const {
    return size;
  }

  bool Add(T elem) {
    GRANARY_ASSERT(T() != elem);
    if (!Contains(elem)) {
      elems.Append(elem);
      ++size;
      return true;
    }
    return false;
  }

  bool Remove(T elem) {
    GRANARY_ASSERT(T() != elem);
    for (auto &e : elems) {
      if (e == elem) {
        e = T();
        --size;
        return true;
      }
    }
    return false;
  }

  void Union(const SelfType &that) {
    if (&that == this) return;
    for (auto elem : that) {
      Add(elem);
    }
  }

 private:
  typedef TinyVector<T, kMinSize> VecT;

  VecT elems;
  size_t size;

  typedef decltype(elems.begin()) VecIterator;
  typedef decltype(const_cast<const decltype(elems) *>(&elems)->begin())
      ConstVecIterator;

  // Iterator over the entries of the `TinyMap`.
  template <typename VecIteratorType>
  class IteratorImpl {
   public:
    typedef IteratorImpl<VecIteratorType> IteratorImplType;
    typedef typename VecIteratorType::ElementType U;

    inline IteratorImpl(void)
        : it() {}

    inline IteratorImpl(const IteratorImplType &that)  // NOLINT
        : it(that.it) {}

    inline explicit IteratorImpl(VecIteratorType it_)
          : it(it_) {
      Advance();
    }

    inline bool operator!=(const IteratorImplType &that) const {
      return it != that.it;
    }

    inline U operator*(void) const {
      return *it;
    }

    inline U &operator*(void) {
      return *it;
    }

    void operator++(void) {
      ++it;
      Advance();
    }

   private:
    void Advance(void) {
      while (it != VecIteratorType()) {
        if (T() == *it) {
          ++it;  // Skip over empty keys.
        } else {
          break;
        }
      }
    }

    VecIteratorType it;
  };

 public:

  typedef IteratorImpl<VecIterator> Iterator;
  typedef IteratorImpl<ConstVecIterator> ConstIterator;

  Iterator begin(void) {
    return Iterator(elems.begin());
  }

  Iterator end(void) {
    return Iterator();
  }

  ConstIterator begin(void) const {
    return ConstIterator(elems.begin());
  }

  ConstIterator end(void) const {
    return ConstIterator();
  }


};

}  // namespace granary

#endif  // GRANARY_BASE_TINY_SET_H_
