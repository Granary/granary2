/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_HASH_H_
#define GRANARY_BASE_HASH_H_

#include "granary/base/base.h"

namespace granary {

// Abstract streaming hash function.
class HashFunction {
 public:
  explicit HashFunction(uint32_t seed_);

  virtual ~HashFunction(void) = default;

  template <typename T>
  inline void Accumulate(const T &bytes_) {
    AccumulateBytes(const_cast<void *>(&bytes_), static_cast<int>(sizeof(T)));
  }

  // Accumulate a sequence of bytes (where the sequence has type `T`) into the
  // hash.
  template <typename T>
  inline void Accumulate(const T *bytes, size_t array_len=1) {
    AccumulateBytes(reinterpret_cast<void *>(const_cast<T *>(bytes)),
                    static_cast<int>(sizeof(T) * array_len));
  }

  // Reset this hash instance to its original state.
  virtual void Reset(void);

  // Finalize the hash function. Calling `Extract32` before finalizing results
  // in undefined behavior.
  virtual void Finalize(void);

  // Extract the hashed value.
  virtual uint32_t Extract32(void);

 protected:

  // Accumulate a single byte into the hash result.
  virtual void AccumulateBytes(void *data, int len);

  const uint32_t seed;
};

}  // namespace granary

#endif  // GRANARY_BASE_HASH_H_
