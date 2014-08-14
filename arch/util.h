/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_UTIL_H_
#define ARCH_UTIL_H_

#include "granary/base/base.h"
#include "granary/base/type_trait.h"

namespace granary {
namespace arch {

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
