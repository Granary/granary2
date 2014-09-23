/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_UTIL_H_
#define ARCH_UTIL_H_

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/type_trait.h"

namespace granary {
namespace arch {
namespace detail {
// Relative address displacement suitable for rel-`num_bits` branches. E.g.
// x86 uses rel32 branch displacements, so `num_bits == 32`.
template <unsigned num_bits>
struct RelOffset {
  enum : ptrdiff_t {
    // Maximum possible amount of space that can be addressed by a `num_bits`
    // offset.
    MAX = 1ULL << num_bits,

    // Relative offsets are signed displacements, so we can only go up or down.
    // Therefore, an offset 2^num_bits > O > 2^(num_bits - 1) is too far away
    // because it can't be represented by a signed num_bits integer.
    SIGNED_MAX = MAX >> 1,

    // Remove a hefty portion of slack to account for a bad estimate of
    // `Relativizer::cache_pc`.
    MAX_VALUE = SIGNED_MAX - (SIGNED_MAX >> 4),
    MIN_VALUE = -SIGNED_MAX
  };
};
}  // namespace detail

inline static constexpr ptrdiff_t MaxRelativeOffset(void) {
  return detail::RelOffset<REL_ADDR_WIDTH_BITS>::MAX_VALUE;
}

inline static constexpr ptrdiff_t MinRelativeOffset(void) {
  return detail::RelOffset<REL_ADDR_WIDTH_BITS>::MIN_VALUE;
}

template <typename A, typename B>
inline static bool AddrIsOffsetReachable(A source, B dest) {
  auto source_addr = UnsafeCast<intptr_t>(source);
  auto dest_addr = UnsafeCast<intptr_t>(dest);
  auto diff = source_addr - dest_addr;
  return MinRelativeOffset() <= diff && diff <= MaxRelativeOffset();
}

#ifdef GRANARY_ECLIPSE
template <typename T> int ImmediateWidthBits(T);
#else

// Returns the bit width of an immediate integer. This assumes sign-extension
// is available for `imm`. That is, if `imm` appears to be a signed negative
// number, or an large unsigned positive number that looks like it could be
// sign-extended from a smaller width, then the smaller width will be returned.
int ImmediateWidthBits(uint64_t imm);

template <
  typename T,
  typename EnableIf<!TypesAreEqual<uint64_t,T>::RESULT &&
                    !IsSignedInteger<T>::RESULT>::Type=0
>
inline static int ImmediateWidthBits(T imm) {
  return ImmediateWidthBits(static_cast<uint64_t>(imm));
}

template <
  typename T,
  typename EnableIf<!TypesAreEqual<uint64_t,T>::RESULT &&
                    IsSignedInteger<T>::RESULT>::Type=0
>
inline static int ImmediateWidthBits(T imm) {
  // Make sure to sign-extend first.
  return ImmediateWidthBits(static_cast<uint64_t>(static_cast<int64_t>(imm)));
}

#endif

}  // namespace arch
}  // namespace granary

#endif  // ARCH_UTIL_H_
