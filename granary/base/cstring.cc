/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

// Used to prevent lowering to libc-provided variants.
#define ACCESS_LVALUE(x) \
  reinterpret_cast<volatile decltype(x) &>(x)

#define ACCESS_RVALUE(x) \
  *reinterpret_cast<volatile std::remove_reference<decltype(x)>::type *>(&(x))

namespace {
static void copy_forward(void * __restrict dest, const void * __restrict src,
                         unsigned long num_bytes) {
  auto dest_bytes = reinterpret_cast<uint8_t * __restrict>(dest);
  auto src_bytes = reinterpret_cast<const uint8_t * __restrict>(src);
  for (auto i = 0UL; i < num_bytes; ++i) {
    dest_bytes[i] = ACCESS_RVALUE(src_bytes[i]);
  }
}

static void copy_backward(void * __restrict dest, const void * __restrict src,
                          unsigned long num_bytes) {
  auto dest_bytes = reinterpret_cast<uint8_t * __restrict>(dest);
  auto src_bytes = reinterpret_cast<const uint8_t * __restrict>(src);
  for (auto i = 1UL; i <= num_bytes; ++i) {  // Copy backwards.
    auto j = num_bytes - i;
    ACCESS_LVALUE(dest_bytes[j]) = src_bytes[j];
  }
}
}  // namespace
extern "C" {

void *memmove(void *dest, const void *src, unsigned long num_bytes) {
  if (GRANARY_LIKELY(num_bytes && dest != src)) {
    if (dest < src) {
      copy_forward(dest, src, num_bytes);
    } else {
      copy_backward(dest, src, num_bytes);
    }
  }
  return dest;
}

void *memcpy(void * __restrict dest, const void * __restrict src,
             unsigned long num_bytes) {
  return memmove(dest, src, num_bytes);
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
  if (GRANARY_UNLIKELY(p1 == p2)) return 0;
  for (auto i = 0UL; i < num_bytes; ++i) {
    auto p1_i = ACCESS_RVALUE(p1[i]);
    auto p2_i = ACCESS_RVALUE(p2[i]);
    if (p1_i != p2_i) {
      return static_cast<int>(p1_i - p2_i);
    }
  }
  return 0;
}

int strcmp(const char *s1, const char *s2) {
  auto p1 = reinterpret_cast<const unsigned char *>(s1);
  auto p2 = reinterpret_cast<const unsigned char *>(s2);
  if (GRANARY_UNLIKELY(p1 == p2)) return 0;
  if (GRANARY_UNLIKELY(!p1)) return -1;
  if (GRANARY_UNLIKELY(!p2)) return 1;
  for (auto i = 0UL; ; ++i) {
    auto p1_i = ACCESS_RVALUE(p1[i]);
    auto p2_i = ACCESS_RVALUE(p2[i]);
    if (p1_i != p2_i) {
      return static_cast<int>(p1_i - p2_i);
    } else if (!p1_i) {
      break;
    }
  }
  return 0;
}

char *strcpy(char *dest, const char *source) {
  if (GRANARY_LIKELY(dest != source)) {
    for (auto i = 0;; ++i) {
      auto chr = source[i];
      ACCESS_LVALUE(dest[i]) = chr;
      if (!chr) break;
    }
  }
  return dest;
}

unsigned long strlen(const char *str) {
  auto len = 0UL;
  if (GRANARY_LIKELY(nullptr != str)) {
    for (; ACCESS_RVALUE(*str); ++str, ++len) {}
  }
  return len;
}

}  // extern C
