/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/breakpoint.h"

// Used to prevent lowering to libc-provided variants.
#define ACCESS_ONCE(x) \
  *reinterpret_cast<volatile std::remove_reference<decltype(x)>::type *>(&(x))

namespace {
static void copy_forward(void * __restrict dest, const void * __restrict src,
                         unsigned long num_bytes) {
  auto dest_bytes = reinterpret_cast<uint8_t * __restrict>(dest);
  auto src_bytes = reinterpret_cast<const uint8_t * __restrict>(src);
  for (auto i = 0UL; i < num_bytes; ++i) {
    dest_bytes[i] = ACCESS_ONCE(src_bytes[i]);
  }
}

static void copy_backward(void * __restrict dest, const void * __restrict src,
                          unsigned long num_bytes) {
  auto dest_bytes = reinterpret_cast<uint8_t * __restrict>(dest);
  auto src_bytes = reinterpret_cast<const uint8_t * __restrict>(src);
  for (auto i = 1UL; i <= num_bytes; ++i) {  // Copy backwards.
    auto j = num_bytes - i;
    ACCESS_ONCE(dest_bytes[j]) = src_bytes[j];
  }
}
}  // namespace
extern "C" {

// Define the boundaries of the primary `.bss` sections. There is a special
// "unprotected" `.bss` section (e.g. that can be used as a load-time allocated
// statically-sized heap) that does not fall into these bounds.
//
// The purpose of these are to detect corruption of Granary's global data
// structures by the allocators.
//
// Note: These symbols are defined by `linker.lds`.
GRANARY_IF_DEBUG( extern char granary_begin_protected_bss; )
GRANARY_IF_DEBUG( extern char granary_end_protected_bss; )

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

void *checked_memset(void *dest, int val_, unsigned long num_bytes) {
#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
  const auto begin_addr = reinterpret_cast<char *>(dest);
  const auto end_addr = begin_addr + num_bytes;
  GRANARY_ASSERT(begin_addr < &granary_begin_protected_bss ||
                 begin_addr >= &granary_end_protected_bss);
  GRANARY_ASSERT(end_addr < &granary_begin_protected_bss ||
                 end_addr >= &granary_end_protected_bss);
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test
  return memset(dest, val_, num_bytes);
}

void *memset(void *dest, int val_, unsigned long num_bytes) {
  GRANARY_ASSERT(nullptr != dest);
  auto dest_bytes = reinterpret_cast<uint8_t *>(dest);
  auto val = static_cast<uint8_t>(val_);
  for (auto i = 0UL; i < num_bytes; ++i) {
    ACCESS_ONCE(dest_bytes[i]) = val;
  }
  return dest;
}

int memcmp(const void * __restrict p1_, const void * __restrict p2_,
           unsigned long num_bytes) {
  auto p1 = reinterpret_cast<const unsigned char * __restrict>(p1_);
  auto p2 = reinterpret_cast<const unsigned char * __restrict>(p2_);
  if (GRANARY_UNLIKELY(p1 == p2)) return 0;
  for (auto i = 0UL; i < num_bytes; ++i) {
    auto p1_i = ACCESS_ONCE(p1[i]);
    auto p2_i = ACCESS_ONCE(p2[i]);
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
    auto p1_i = ACCESS_ONCE(p1[i]);
    auto p2_i = ACCESS_ONCE(p2[i]);
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
      ACCESS_ONCE(dest[i]) = chr;
      if (!chr) break;
    }
  }
  return dest;
}

char *strncpy(char *dest, const char *source, unsigned long max_len) {
  if (GRANARY_LIKELY(dest != source)) {
    auto i = 0UL;
    for (; i < max_len; ++i) {
      auto chr = source[i];
      ACCESS_ONCE(dest[i]) = chr;
      if (!chr) break;
    }
    if (i >= max_len && max_len) {
      dest[max_len - 1] = '\0';
    }
  }
  return dest;
}


unsigned long strlen(const char *str) {
  auto len = 0UL;
  if (GRANARY_LIKELY(nullptr != str)) {
    for (; ACCESS_ONCE(*str); ++str, ++len) {}
  }
  return len;
}

}  // extern C
