/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BITSET_H_
#define GRANARY_BASE_BITSET_H_

#include "granary/base/packed_array.h"

namespace granary {

template <unsigned long kNumBits>
using BitSet = PackedArray<bool, 1, kNumBits>;

}

#endif  // GRANARY_BASE_BITSET_H_
