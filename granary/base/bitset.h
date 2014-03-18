/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BITSET_H_
#define GRANARY_BASE_BITSET_H_

#include "granary/base/packed_array.h"

namespace granary {

// Represents a packed bitset of a fixed size.
template <unsigned long kNumBits>
class BitSet : public PackedArray<bool, 1, kNumBits> {
 public:

  // Set all bits in the bitset to true or false.
  inline void SetAll(bool val) {
    memset(&(this->storage[0]), val ? 0xFF : 0, sizeof this->storage);
  }
};

}  // namespace granary

#endif  // GRANARY_BASE_BITSET_H_
