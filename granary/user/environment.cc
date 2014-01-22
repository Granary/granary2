/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/environment.h"
#include "granary/cfg/instruction.h"

namespace granary {

// Annotates the instruction, or adds an annotated instruction into the
// instruction list. This returns the first
void Environment::AnnotateInstruction(Instruction *instr) const {
  GRANARY_UNUSED(instr);
}

}  // namespace granary
