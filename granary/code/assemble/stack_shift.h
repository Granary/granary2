/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_STACK_SHIFT_H_
#define GRANARY_CODE_ASSEMBLE_STACK_SHIFT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"

namespace granary {

// Represents a "stack shift" annotation value.
class StackShift {
  int32_t arch_instr_opcode;
  int32_t shift_amount;
} __attribute__((packed));

static_assert(sizeof(StackShift) == sizeof(uintptr_t),
              "Invalid structure packing of `StackShift`.");

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_STACK_SHIFT_H_
