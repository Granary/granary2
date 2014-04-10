/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_LOOKUP_TABLE_H_
#define GRANARY_BASE_LOOKUP_TABLE_H_

#include "granary/base/base.h"

namespace granary {

// Forward declaration of operations table for a lookup table.
//
// Note: Must define the following methods:
//      static K KeyForValue(V);
template <typename K, typename V>
class LookupTableOperations;

// Simple look up table for values, where values know their keys.
template <typename K, typename V, unsigned long kSize>
class FixedSizeLookupTable {
 public:
  FixedSizeLookupTable(void) = default;

  V *Find(const K key) {
    V *sentinel(nullptr);
    for (auto &val : values) {
      if (V() == val) {
        sentinel = &val;
      } else if (key == Ops::KeyForValue(val)) {
        return &val;
      }
    }
    return sentinel;
  }

  inline V *begin(void) {
    return &(values[0]);
  }

  inline V *end(void) {
    return &(values[kSize]);
  }

 private:
  typedef LookupTableOperations<K, V> Ops;

  V values[kSize];

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(FixedSizeLookupTable,
                                            (K, V, kSize));
};

}  // namespace granary

#endif  // GRANARY_BASE_LOOKUP_TABLE_H_
