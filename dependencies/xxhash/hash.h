/*
   xxHash - Fast Hash algorithm
   Copyright (C) 2012, Yann Collet.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  You can contact the author at :
  - xxHash source repository : http://code.google.com/p/xxhash/
*/

#ifndef DEPENDENCIES_XXHASH_HASH_H_
#define DEPENDENCIES_XXHASH_HASH_H_

#include "granary/base/hash.h"

namespace xxhash {

// Wraps Yann Collet's xxHash in Granary's `HashFunction` interface.
class HashFunction final : public granary::HashFunction {
 public:
  virtual ~HashFunction(void) = default;

  inline HashFunction(uint32_t seed_)
      : granary::HashFunction(seed_) {
    Reset();
  }

  // Reset this hash instance to its original state.
  virtual void Reset(void);

  // Finalize the hash function. Calling `Extract32` beforeizing results
  // in undefined behavior.
  virtual void Finalize(void);

  // Extract the hashed value.
  virtual uint32_t Extract32(void);

 protected:

  // Accumulate a single byte into the hash result.
  virtual void AccumulateBytes(void *data, int len);

 private:
  unsigned int v1;
  unsigned int v2;
  unsigned int v3;
  unsigned int v4;
  unsigned long long total_len;
  char memory[16];
  int memsize;
  unsigned int h32;
};

}  // namespace xxhash

#endif  // DEPENDENCIES_XXHASH_HASH_H_
