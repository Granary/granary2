/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_64_BASE_H_
#define GRANARY_ARCH_X86_64_BASE_H_

#define GRANARY_ARCH_CACHE_LINE_SIZE 64  // In bytes.
#define GRANARY_ARCH_ADDRESS_WIDTH 64  // In bits.

#define GRANARY_ARCH_PAGE_FRAME_SIZE 4096

namespace granary {
namespace arch {

enum {
  PAGE_SIZE_BYTES = 4096,

  CACHE_LINE_SIZE_BYTES = 64,
  CACHE_LINE_SIZE_BITS = 64 * 8,

  ADDRESS_WIDTH_BYTES = 8,
  ADDRESS_WIDTH_BITS = 64,

  // Size of widest general purpose registers.
  GPR_WIDTH_BYTES = ADDRESS_WIDTH_BYTES,
  GPR_WIDTH_BITS = ADDRESS_WIDTH_BITS,

  // Excludes %rsp, excludes %rip
  NUM_GENERAL_PURPOSE_REGISTERS = 14,

  // Byte value with which to poison executable memory. This should normally
  // correspond to something that .
  EXEC_MEMORY_POISON_BYTE = 0xCC
};

// Feature support that guides architecture-oblivious optimization routines.
enum {

  // The maximum width of a relative branch.
  REL_BRANCH_WIDTH_BITS = 32
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_X86_64_BASE_H_
