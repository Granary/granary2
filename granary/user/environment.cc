/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/environment.h"

namespace granary {

// Annotates the instruction, or adds an annotated instruction into the
// instruction list. This returns the first
void Environment::AnnotateInstruction(Instruction *instr) const {
  GRANARY_UNUSED(instr);
}

}  // namespace granary
