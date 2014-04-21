/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_RANGE_H_
#define GRANARY_BASE_RANGE_H_

namespace granary {

template <typename T>
class ArrayRangeIterator {
 public:
  inline ArrayRangeIterator(void)
      : begin_(nullptr),
        end_(nullptr) {}

  inline ArrayRangeIterator(T *begin__, T *end__)
      : begin_(begin__),
        end_(end__) {}

  template <unsigned long kSize>
  inline explicit ArrayRangeIterator(T (&arr)[kSize])
      : begin_(&(arr[0])),
        end_(&(arr[kSize])) {}

  template <typename U>
  inline explicit ArrayRangeIterator(U &iterable)
      : begin_(iterable.begin()),
        end_(iterable.end()) {}

  inline T *begin(void) const {
    return begin_;
  }
  inline T *end(void) const {
    return end_;
  }

 private:
  T * const begin_;
  T * const end_;
};

}  // namespace granary

#endif  // GRANARY_BASE_RANGE_H_
