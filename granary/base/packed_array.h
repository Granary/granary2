/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_PACKED_ARRAY_H_
#define GRANARY_BASE_PACKED_ARRAY_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/string.h"

namespace granary {

// Implements a packed, fixed-size array of some small (less than 1 byte in
// size) data structure.
template <typename T, unsigned long kSizeBits, unsigned long kNumElems>
class PackedArray {
 public:
  PackedArray(void) {
    memset(&(storage[0]), 0, sizeof storage);
  }

  // Access the `i`th element in the packed array.
  T Get(unsigned i) const {
    auto index = IndexOf(i);
    auto shift = ShiftOf(i);
    auto byte_val = static_cast<uint8_t>((storage[index] >> shift) & BIT_MASK);
    return static_cast<T>(byte_val);
  }

  // Modify the `i`th element in the packed array.
  void Set(unsigned i, T val) {
    auto byte_val = static_cast<uint8_t>(
        static_cast<uint8_t>(val) & BIT_MASK);
    auto index = IndexOf(i);
    auto shift = ShiftOf(i);
    auto old_val = static_cast<uint8_t>(storage[index] & ~(BIT_MASK << shift));
    storage[index] = old_val | static_cast<uint8_t>(byte_val << shift);
  }

 private:
  enum {
    SIZE = GRANARY_ALIGN_TO(kSizeBits, 2),
    NUM_ELEMS = GRANARY_ALIGN_TO(kNumElems, 8),
    NUM_PER_BYTE = 8 / SIZE,
    NUM_BYTES = NUM_ELEMS / NUM_PER_BYTE,
    BIT_MASK = static_cast<uint8_t>(~(0xFFU << kSizeBits))
  };

  static_assert(1 == sizeof(T), "Type `T` must be at most 1 byte in size.");
  static_assert(8 > kSizeBits, "Type `T` must have fewer than 8 bits.");
  static_assert((NUM_BYTES * NUM_PER_BYTE) >= kNumElems,
      "Could not determine the number of bytes of storage to allocate for "
      "`kNumElems` of `kSizeBits`-sized objects.");

  // Return the index of the `i`th element.
  inline unsigned IndexOf(unsigned i) const {
    return i / NUM_PER_BYTE;
  }

  // Return the bit shift needed for the `i`th element.
  inline unsigned ShiftOf(unsigned i) const {
    return (i % NUM_PER_BYTE) * kSizeBits;
  }

  uint8_t storage[NUM_BYTES];

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(PackedArray,
                                            (T, kSizeBits, kNumElems));
};

}  // namespace granary

#endif  // GRANARY_BASE_PACKED_ARRAY_H_
