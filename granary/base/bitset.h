/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BITSET_H_
#define GRANARY_BASE_BITSET_H_

#include "granary/base/packed_array.h"
#include "granary/base/type_trait.h"

namespace granary {

// Represents a "fast" bitset that is backed by an integral constant.
template <typename StorageT>
class FastBitSet {
 public:
  typedef FastBitSet<StorageT> SelfType;

  inline FastBitSet(void) {
    storage[0] = static_cast<StorageT>(0U);
  }

  inline bool Get(int i) const {
    return Get(static_cast<unsigned>(i));
  }

  inline bool Get(unsigned i) const {
    return storage[0] & (1U << i);
  }

  inline void Set(int i, bool val) {
    Set(static_cast<unsigned>(i), val);
  }

  inline void Set(unsigned i, bool val) {
    if (val) {
      storage[0] |= static_cast<StorageT>(1U << i);
    } else {
      storage[0] &= static_cast<StorageT>(~(1U << i));
    }
  }

  inline void Copy(const SelfType &that) {
    storage[0] = that.storage[0];
  }

  // Set all bits in the bitset to true or false.
  inline void SetAll(bool val) {
    if (val) {
      storage[0] = static_cast<StorageT>(~static_cast<StorageT>(0U));
    } else {
      storage[0] = static_cast<StorageT>(0U);
    }
  }

 protected:
  StorageT storage[1];
};

// Represents a "slow" bitset that is backed by an array of integers.
template <unsigned long kNumBits>
class BitSet : public PackedArray<bool, 1, kNumBits> {
 public:
  inline BitSet(void) {
    SetAll(false);
  }

  // Set all bits in the bitset to true or false.
  inline void SetAll(bool val) {
    memset(&(this->storage[0]), val ? 0xFF : 0, sizeof this->storage);
  }
};

#define GRANARY_SPECIALIZE_BITSET(num_bits, type) \
  template <> \
  class BitSet<num_bits> : public FastBitSet<type> {};

GRANARY_SPECIALIZE_BITSET(1, uint8_t)
GRANARY_SPECIALIZE_BITSET(2, uint8_t)
GRANARY_SPECIALIZE_BITSET(3, uint8_t)
GRANARY_SPECIALIZE_BITSET(4, uint8_t)
GRANARY_SPECIALIZE_BITSET(5, uint8_t)
GRANARY_SPECIALIZE_BITSET(6, uint8_t)
GRANARY_SPECIALIZE_BITSET(7, uint8_t)
GRANARY_SPECIALIZE_BITSET(8, uint8_t)

GRANARY_SPECIALIZE_BITSET(9, uint16_t)
GRANARY_SPECIALIZE_BITSET(10, uint16_t)
GRANARY_SPECIALIZE_BITSET(11, uint16_t)
GRANARY_SPECIALIZE_BITSET(12, uint16_t)
GRANARY_SPECIALIZE_BITSET(13, uint16_t)
GRANARY_SPECIALIZE_BITSET(14, uint16_t)
GRANARY_SPECIALIZE_BITSET(15, uint16_t)
GRANARY_SPECIALIZE_BITSET(16, uint16_t)

GRANARY_SPECIALIZE_BITSET(17, uint32_t)
GRANARY_SPECIALIZE_BITSET(18, uint32_t)
GRANARY_SPECIALIZE_BITSET(19, uint32_t)
GRANARY_SPECIALIZE_BITSET(20, uint32_t)
GRANARY_SPECIALIZE_BITSET(21, uint32_t)
GRANARY_SPECIALIZE_BITSET(22, uint32_t)
GRANARY_SPECIALIZE_BITSET(23, uint32_t)
GRANARY_SPECIALIZE_BITSET(24, uint32_t)
GRANARY_SPECIALIZE_BITSET(25, uint32_t)
GRANARY_SPECIALIZE_BITSET(26, uint32_t)
GRANARY_SPECIALIZE_BITSET(27, uint32_t)
GRANARY_SPECIALIZE_BITSET(28, uint32_t)
GRANARY_SPECIALIZE_BITSET(29, uint32_t)
GRANARY_SPECIALIZE_BITSET(30, uint32_t)
GRANARY_SPECIALIZE_BITSET(31, uint32_t)
GRANARY_SPECIALIZE_BITSET(32, uint32_t)

GRANARY_SPECIALIZE_BITSET(33, uint64_t)
GRANARY_SPECIALIZE_BITSET(34, uint64_t)
GRANARY_SPECIALIZE_BITSET(35, uint64_t)
GRANARY_SPECIALIZE_BITSET(36, uint64_t)
GRANARY_SPECIALIZE_BITSET(37, uint64_t)
GRANARY_SPECIALIZE_BITSET(38, uint64_t)
GRANARY_SPECIALIZE_BITSET(39, uint64_t)
GRANARY_SPECIALIZE_BITSET(40, uint64_t)
GRANARY_SPECIALIZE_BITSET(41, uint64_t)
GRANARY_SPECIALIZE_BITSET(42, uint64_t)
GRANARY_SPECIALIZE_BITSET(43, uint64_t)
GRANARY_SPECIALIZE_BITSET(44, uint64_t)
GRANARY_SPECIALIZE_BITSET(45, uint64_t)
GRANARY_SPECIALIZE_BITSET(46, uint64_t)
GRANARY_SPECIALIZE_BITSET(47, uint64_t)
GRANARY_SPECIALIZE_BITSET(48, uint64_t)
GRANARY_SPECIALIZE_BITSET(49, uint64_t)
GRANARY_SPECIALIZE_BITSET(50, uint64_t)
GRANARY_SPECIALIZE_BITSET(51, uint64_t)
GRANARY_SPECIALIZE_BITSET(52, uint64_t)
GRANARY_SPECIALIZE_BITSET(53, uint64_t)
GRANARY_SPECIALIZE_BITSET(54, uint64_t)
GRANARY_SPECIALIZE_BITSET(55, uint64_t)
GRANARY_SPECIALIZE_BITSET(56, uint64_t)
GRANARY_SPECIALIZE_BITSET(57, uint64_t)
GRANARY_SPECIALIZE_BITSET(58, uint64_t)
GRANARY_SPECIALIZE_BITSET(59, uint64_t)
GRANARY_SPECIALIZE_BITSET(60, uint64_t)
GRANARY_SPECIALIZE_BITSET(61, uint64_t)
GRANARY_SPECIALIZE_BITSET(62, uint64_t)
GRANARY_SPECIALIZE_BITSET(63, uint64_t)
GRANARY_SPECIALIZE_BITSET(64, uint64_t)

#undef GRANARY_SPECIALIZE_BITSET

}  // namespace granary

#endif  // GRANARY_BASE_BITSET_H_
