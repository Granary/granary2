/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_64_SELECT_H_
#define GRANARY_ARCH_X86_64_SELECT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/x86-64/instruction.h"

namespace granary {
namespace arch {

// Returns the `xed_inst_t` instance associated with this instruction. This
// won't necessarily return a perfect selection. That is, all that is required
// of the returned selection is that the types of the operands match
// (independent of the sizes of operands).
const xed_inst_t *SelectInstruction(const Instruction *instr);

}  // namespace arch
}  // namespace granary


#endif  // GRANARY_ARCH_X86_64_SELECT_H_
