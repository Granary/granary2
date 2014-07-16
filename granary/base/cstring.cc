/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

// Used to prevent lowering to libc-provided variants.
#define ACCESS_LVALUE(x) \
  reinterpret_cast<volatile decltype(x) &>(x)

#define ACCESS_RVALUE(x) \
  *reinterpret_cast<volatile std::remove_reference<decltype(x)>::type *>(&(x))

extern "C" {

void *memcpy(void * __restrict dest, const void * __restrict src,
                     unsigned long num_bytes) {
  auto dest_bytes = reinterpret_cast<uint8_t * __restrict>(dest);
  auto src_bytes = reinterpret_cast<const uint8_t * __restrict>(src);
  for (auto i = 0UL; i < num_bytes; ++i) {
    dest_bytes[i] = ACCESS_RVALUE(src_bytes[i]);
  }
  return dest;
}

void *memset(void *dest, int val_, unsigned long num_bytes) {
  auto dest_bytes = reinterpret_cast<uint8_t *>(dest);
  auto val = static_cast<uint8_t>(val_);
  for (auto i = 0UL; i < num_bytes; ++i) {
    ACCESS_LVALUE(dest_bytes[i]) = val;
  }
  return dest;
}

int memcmp(const void * __restrict p1_, const void * __restrict p2_,
           unsigned long num_bytes) {
  auto p1 = reinterpret_cast<const unsigned char * __restrict>(p1_);
  auto p2 = reinterpret_cast<const unsigned char * __restrict>(p2_);
  for (auto i = 0UL; i < num_bytes; ++i) {
    auto p1_i = ACCESS_RVALUE(p1[i]);
    auto p2_i = ACCESS_RVALUE(p2[i]);
    if (p1_i != p2_i) {
      return p1_i - p2_i;
    }
  }
  return 0;
}

void *memmove(void *dest, const void *src, unsigned long num_bytes) {
  if (num_bytes) {
    if (dest <= src) {
      return memcpy(dest, src, num_bytes);
    } else {
      auto dest_bytes = reinterpret_cast<uint8_t *>(dest);
      auto src_bytes = reinterpret_cast<const uint8_t *>(src);
      for (auto i = 0UL; i < num_bytes; ++i) {  // Copy backwards.
        dest_bytes[num_bytes - i] = ACCESS_RVALUE(src_bytes[num_bytes - i]);
      }
    }
  }
  return dest;
}

int strcmp(const char *s1, const char *s2) {
  auto p1 = reinterpret_cast<const unsigned char *>(s1);
  auto p2 = reinterpret_cast<const unsigned char *>(s2);
  while (*p1 == *p2++) {
    if (*p1++ == 0) return 0;
  }
  return static_cast<int>(ACCESS_RVALUE(*p1) - ACCESS_RVALUE(p2[-1]));
}

char *strcpy(char *dest, const char *source) {
  auto ret_dest = dest;
  for (; ; ++source) {
    auto curr_source = ACCESS_RVALUE(*source);
    if (!curr_source) break;
    *dest++ = curr_source;
  }
  *dest = '\0';
  return ret_dest;
}

unsigned long strlen(const char *str) {
  auto len = 0UL;
  for (; ACCESS_RVALUE(*str); ++str, ++len) {}
  return len;
}

}  // extern C
