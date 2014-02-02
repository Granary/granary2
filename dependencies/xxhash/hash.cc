// This code derived from https://github.com/jpountz/lz4-java/blob/master/src/xxhash/xxhash.c

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

#include "dependencies/xxhash/hash.h"
#include "granary/base/string.h"  // For `memcpy`.


//**************************************
// Tuning parameters
//**************************************

// Note: Clang and LLVM both define these macros, and Granary depends on clang
//       (for now), so we don't use the original detection endianness detection
//       of xxHash.
#ifndef __BYTE_ORDER__
# error "Missing __BYTE_ORDER__ pre-defined macro."
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define FORCE_NATIVE_FORMAT 1
# define XXH_BIG_ENDIAN 0
#else
# define FORCE_NATIVE_FORMAT 0
# define XXH_BIG_ENDIAN 1
#endif


//**************************************
// Compiler-specific Options & Functions
//**************************************
#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

// Note : under GCC, it may sometimes be faster to enable the (2nd) macro definition, instead of using win32 intrinsic
#if defined(_WIN32)
#  define XXH_rotl32(x,r) _rotl(x,r)
#else
#  define XXH_rotl32(x,r) ((x << r) | (x >> (32 - r)))
#endif

#if defined(_MSC_VER)     // Visual Studio
#  define XXH_swap32 _byteswap_ulong
#elif GCC_VERSION >= 403
#  define XXH_swap32 __builtin_bswap32
#else
static inline unsigned int XXH_swap32 (unsigned int x) {
                        return  ((x << 24) & 0xff000000 ) |
                                ((x <<  8) & 0x00ff0000 ) |
                                ((x >>  8) & 0x0000ff00 ) |
                                ((x >> 24) & 0x000000ff );
                 }
#endif


//**************************************
// Constants
//**************************************
#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4    668265263U
#define PRIME32_5    374761393U

//**************************************
// Macros
//**************************************
#define XXH_LE32(p)  (XXH_BIG_ENDIAN ? XXH_swap32(*(unsigned int*)(p)) : *(unsigned int*)(p))


namespace xxhash {

// Reset this hash instance to its original state.
void HashFunction::Reset(void) {
  v1 = seed + PRIME32_1 + PRIME32_2;
  v2 = seed + PRIME32_2;
  v3 = seed + 0;
  v4 = seed - PRIME32_1;
  total_len = 0;
  memsize = 0;
  h32 = 0;
}

// Finalize the hash function. Calling `Extract32` beforeizing results
// in undefined behavior.
void HashFunction::Finalize(void) {
  unsigned char * p   = (unsigned char*)memory;
  unsigned char* bEnd = (unsigned char*)memory + memsize;
  h32 = 0;

  if (total_len >= 16)
  {
    h32 = XXH_rotl32(v1, 1) + XXH_rotl32(v2, 7) + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
  }
  else
  {
    h32  = seed + PRIME32_5;
  }

  h32 += (unsigned int) total_len;

  while (p<=bEnd-4)
  {
    h32 += XXH_LE32(p) * PRIME32_3;
    h32 = XXH_rotl32(h32, 17) * PRIME32_4 ;
    p+=4;
  }

  while (p<bEnd)
  {
    h32 += (*p) * PRIME32_5;
    h32 = XXH_rotl32(h32, 11) * PRIME32_1 ;
    p++;
  }

  h32 ^= h32 >> 15;
  h32 *= PRIME32_2;
  h32 ^= h32 >> 13;
  h32 *= PRIME32_3;
  h32 ^= h32 >> 16;
}

// Extract the hashed value.
uint32_t HashFunction::Extract32(void) {
  return h32;
}

// Accumulate a single byte into the hash result.
void HashFunction::AccumulateBytes(void *data, int len) {
  const unsigned char* p = (const unsigned char*)data;
  const unsigned char* const bEnd = p + len;

  total_len += len;

  if (memsize + len < 16)   // fill in tmp buffer
  {
    memcpy(memory + memsize, data, len);
    memsize +=  len;
    return;
  }

  if (memsize)   // some data left from previous feed
  {
    memcpy(memory + memsize, data, 16-memsize);
    {
      const unsigned int* p32 = (const unsigned int*)memory;
      v1 += XXH_LE32(p32) * PRIME32_2; v1 = XXH_rotl32(v1, 13); v1 *= PRIME32_1; p32++;
      v2 += XXH_LE32(p32) * PRIME32_2; v2 = XXH_rotl32(v2, 13); v2 *= PRIME32_1; p32++;
      v3 += XXH_LE32(p32) * PRIME32_2; v3 = XXH_rotl32(v3, 13); v3 *= PRIME32_1; p32++;
      v4 += XXH_LE32(p32) * PRIME32_2; v4 = XXH_rotl32(v4, 13); v4 *= PRIME32_1; p32++;
    }
    p += 16-memsize;
    memsize = 0;
  }

  {
    const unsigned char* const limit = bEnd - 16;
    while (p<=limit)
    {
      v1 += XXH_LE32(p) * PRIME32_2; v1 = XXH_rotl32(v1, 13); v1 *= PRIME32_1; p+=4;
      v2 += XXH_LE32(p) * PRIME32_2; v2 = XXH_rotl32(v2, 13); v2 *= PRIME32_1; p+=4;
      v3 += XXH_LE32(p) * PRIME32_2; v3 = XXH_rotl32(v3, 13); v3 *= PRIME32_1; p+=4;
      v4 += XXH_LE32(p) * PRIME32_2; v4 = XXH_rotl32(v4, 13); v4 *= PRIME32_1; p+=4;
    }
  }

  if (p < bEnd)
  {
    memcpy(memory, p, bEnd-p);
    memsize = bEnd-p;
  }

  return;
}

}  // namespace xxhash
