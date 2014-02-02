/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/hash.h"
#include "granary/breakpoint.h"

namespace granary {

HashFunction::HashFunction(uint32_t seed_)
   : seed(seed_) {}

// Reset this hash instance to its original state.
void HashFunction::Reset(void) {
  granary_break_on_fault();
}

// Finalize the hash function. Calling `Extract32` before finalizing results
// in undefined behavior.
void HashFunction::Finalize(void) {
  granary_break_on_fault();
}

// Extract the hashed value.
uint32_t HashFunction::Extract32(void) {
  granary_break_on_fault();
  return 0;
}

// Accumulate a single byte into the hash result.
void HashFunction::AccumulateBytes(void *, int) {
  granary_break_on_fault();
}

}  // namespace granary
