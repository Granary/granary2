/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_RELATIVIZE_H_
#define GRANARY_DRIVER_RELATIVIZE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/pc.h"

namespace granary {

class ControlFlowInstruction;

namespace driver {

// Relativize a control-flow instruction.
void RelativizeCFI(ControlFlowInstruction *cfi, Instruction *instr,
                   PC target_pc, bool target_is_far_away);

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_RELATIVIZE_H_
