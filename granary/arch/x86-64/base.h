/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_ARCH_X86_64_BASE_H_
#define GRANARY_ARCH_X86_64_BASE_H_

#define GRANARY_ARCH_CACHE_LINE_SIZE 64

#define GRANARY_ARCH_PAGE_FRAME_SIZE 4096

#define GRANARY_ARCH_EXEC_POISON 0xCC

namespace granary {
namespace arch {

// Feature support that guides architecture-oblivious optimization routines.
enum {

  // TODO(pag): - Need to formalize this a bit more. For example, for PC-rel, is
  //              it relative to the PC of the CTI, or the next PC after the
  //              CTI.
  //            - Also, should these be grouped into categories, so that it's
  //              easy to talk about the PC-rel support (8, 16, 24, 32) for a
  //              particular kind of instruction.
  //            - Finally, should probably have a way of tracking the current
  //              PC-rel state of a CTI.

  SUPPORTS_REL8_COND_JUMP = true,
  SUPPORTS_REL8_DIRECT_JUMP = true,
  SUPPORTS_REL8_DIRECT_CALL = false,

  SUPPORTS_REL16_COND_JUMP = false,
  SUPPORTS_REL16_DIRECT_JUMP = false,
  SUPPORTS_REL16_DIRECT_CALL = false,

  SUPPORTS_REL24_COND_JUMP = false,
  SUPPORTS_REL24_DIRECT_JUMP = false,
  SUPPORTS_REL24_DIRECT_CALL = false,

  SUPPORTS_REL32_COND_JUMP = true,
  SUPPORTS_REL32_DIRECT_JUMP = true,
  SUPPORTS_REL32_DIRECT_CALL = false
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_X86_64_BASE_H_
