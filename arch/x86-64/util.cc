/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/util.h"

namespace granary {
namespace arch {

// Returns the bit width of an immediate integer. This assumes sign-extension
// is available for `imm`. That is, if `imm` appears to be a signed negative
// number, or an large unsigned positive number that looks like it could be
// sign-extended from a smaller width, then the smaller width will be returned.
int ImmediateWidthBits(uint64_t imm) {
  if (!imm) return 8;

  auto leading_zeros = __builtin_clzl(imm);
  if (56 < leading_zeros) return 8;
  if (48 < leading_zeros) return 16;
  if (32 < leading_zeros) return 32;

  leading_zeros = __builtin_clzl(~imm);
  if (56 < leading_zeros) return 8;
  if (48 < leading_zeros) return 16;
  if (32 < leading_zeros) return 32;

  return 64;
}

}  // namespace arch
}  // namespace granary
