/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_HASH_H_
#define GRANARY_BASE_HASH_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"

namespace granary {

// Abstract streaming hash function.
class HashFunction {
 public:
  virtual ~HashFunction(void) = default;

  template <typename T>
  inline void Accumulate(const T &bytes_) {
    Accumulate(&bytes_);
  }

  // Accumulate a sequence of bytes (where the sequence has type `T`) into the
  // hash.
  template <typename T>
  inline void Accumulate(const T *bytes_, size_t array_len=1) {
    const uint8_t *bytes(UnsafeCast<const uint8_t *>(bytes_));
    for (size_t i(0), len(sizeof(T) * array_len); i < len; ++i) {
      AccumulateByte(bytes[i]);
    }
  }

  virtual uint32_t Extract32(void) { return 0; }
  virtual uint64_t Extract64(void) { return 0; }

 protected:

  // Accumulate a single byte into the hash result.
  virtual void AccumulateByte(uint8_t) = 0;
};

}  // namespace granary

#endif  // GRANARY_BASE_HASH_H_
