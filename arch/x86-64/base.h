/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_X86_64_BASE_H_
#define ARCH_X86_64_BASE_H_

#define GRANARY_ARCH_CACHE_LINE_SIZE 64  // In bytes.
#define GRANARY_ARCH_ADDRESS_WIDTH 64  // In bits.

#define GRANARY_ARCH_PAGE_FRAME_SIZE 4096

#include "granary/base/base.h"

namespace granary {
namespace arch {

enum {
  PAGE_SIZE_BYTES = 4096,

  // Alignment for blocks allocated in the code cache.
  CODE_ALIGN_BYTES = 1,

  CACHE_LINE_SIZE_BYTES = 64,
  CACHE_LINE_SIZE_BITS = 64 * 8,

  // Size of the code to do a context call.
  CONTEXT_CALL_CODE_SIZE_BYTES = 80,
  INLINE_CALL_CODE_SIZE_BYTES = 128,

  // Upper bound on the size of edge-specific direct edge code. Ideally this
  // should be as small as possible.
  DIRECT_EDGE_CODE_SIZE_BYTES = GRANARY_IF_KERNEL_ELSE(32, 48),
  DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES = 48,

  // Upper bound on the size of indirect edge code. Ideally this should be as
  // small as possible.
  INDIRECT_EDGE_CODE_SIZE_BYTES = 48,
  INDIRECT_EDGE_ENTRY_CODE_SIZE_BYTES = 48,

  STACK_WIDTH_BYTES = 8,
  STACK_WIDTH_BITS = 64,

  ADDRESS_WIDTH_BYTES = 8,
  ADDRESS_WIDTH_BITS = 64,

  QUADWORD_WIDTH_BYTES = 8,
  QUADWORD_WIDTH_BITES = 64,

  DOUBLEWORD_WIDTH_BYTES = 4,
  DOUBLEWORD_WIDTH_BITS = 32,

  WORD_WIDTH_BYTES = 2,
  WORD_WIDTH_BITS = 16,

  BYTE_WIDTH_BYTES = 1,
  BYTE_WIDTH_BITS = 8,

  // TODO(pag): This is OS ABI-specific.
  REDZONE_SIZE_BYTES = GRANARY_IF_USER_ELSE(128, 0),

  // Size of widest general purpose registers.
  GPR_WIDTH_BYTES = ADDRESS_WIDTH_BYTES,
  GPR_WIDTH_BITS = ADDRESS_WIDTH_BITS,

  // Excludes `RSP`, excludes `RIP`.
  NUM_GENERAL_PURPOSE_REGISTERS = 15,

  // Maximum number of spill slots that can be used for spilling GPRs for use
  // by virtual registers.
  MAX_NUM_SPILL_SLOTS = 32,

  // Byte value with which to poison executable memory. This should normally
  // correspond to something that .
  EXEC_MEMORY_POISON_BYTE = 0xCC,

  // The maximum width of a relative branch.
  REL_ADDR_WIDTH_BITS = 32
};

}  // namespace arch
}  // namespace granary

#endif  // ARCH_X86_64_BASE_H_
