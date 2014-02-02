/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BLOOM_FILTER_H_
#define GRANARY_BASE_BLOOM_FILTER_H_

#include "granary/base/base.h"
#include "granary/base/string.h"

namespace granary {

// Very simple statically-sized bloom filter. The number of bits *must* be a
// multiple of `NUM_BITS_PER_SLOT` (which is 32).
//
// It is expected that users of this template class will be sane with regards
// to passing in the same number of hash values to both `Add` and
// `MightContain`.
template <unsigned kNumBits>
class BloomFilter {
 public:
  BloomFilter(void) {
    Clear();
  }

  void Clear(void) {
    memset(&(slots[0]), 0, kNumBits / 8);
  }

  // Adds the value represented by the three hashed keys to the bloom filter.
  void Add(std::initializer_list<uint32_t> keys) {
    for (auto key : keys) {
      auto modded_hash = key % kNumBits;
      auto bit = modded_hash % NUM_BITS_PER_SLOT;
      auto slot = modded_hash / NUM_BITS_PER_SLOT;
      slots[slot] |= (1 << bit);
    }
  }

  // Returns true if the bloom filter might contain the value represented by
  // the three hashed keys.
  bool MightContain(std::initializer_list<uint32_t> keys) {
    for (auto key : keys) {
      auto modded_hash = key % kNumBits;
      auto bit = modded_hash % NUM_BITS_PER_SLOT;
      auto slot = modded_hash / NUM_BITS_PER_SLOT;
      if (!(slots[slot] & (1 << bit))) {
        return false;
      }
    }
    return true;
  }

 private:
  enum {
    NUM_BITS_PER_SLOT = 32,
    NUM_SLOTS = kNumBits / NUM_BITS_PER_SLOT
  };

  static_assert(0 == (kNumBits % NUM_BITS_PER_SLOT),
      "BloomFilter template must be instantiated with `kNumBits` as a "
      "multiple of `NUM_BITS_PER_SLOT`.");

  uint32_t slots[NUM_SLOTS];
};

}  // namespace granary

#endif  // GRANARY_BASE_BLOOM_FILTER_H_
