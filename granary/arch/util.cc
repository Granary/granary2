/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/util.h"

namespace granary {
namespace arch {

// Returns the bit width of an immediate integer. This assumes sign-extension
// is available for `imm`. That is, if `imm` appears to be a signed negative
// number, or an large unsigned positive number that looks like it could be
// sign-extended from a smaller width, then the smaller width will be returned.
int ImmediateWidthBits(uint64_t imm) {
  enum : uint64_t {
    WIDTH_8   = 0x0FFUL,
    WIDTH_16  = WIDTH_8 | (WIDTH_8 << 8),
    WIDTH_32  = WIDTH_16 | (WIDTH_16 << 16)
  };
  if (!imm) return 8;
  if ((imm | ~WIDTH_8) == imm) return 8;  // Signed.
  if ((imm & WIDTH_8) == imm) return 8;  // Unsigned.

  if ((imm | ~WIDTH_16) == imm) return 16;  // Signed.
  if ((imm & WIDTH_16) == imm) return 16;  // Unsigned.

  if ((imm | ~WIDTH_32) == imm) return 32;  // Signed.
  if ((imm & WIDTH_32) == imm) return 32;  // Unsigned.
  return 64;
}

}  // namespace arch
}  // namespace granary
