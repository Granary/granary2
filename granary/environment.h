/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_ENVIRONMENT_H_
#define GRANARY_ENVIRONMENT_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class Instruction;
class DecodedBasicBlock;

// Manages environmental information that changes how Granary behaves. For
// example, in the Linux kernel, the environmental data gives the instruction
// decoder access to the kernel's exception tables, so that it can annotate
// instructions as potentially faulting.
class Environment {
 public:
  Environment(void) = default;

  // Annotates the instruction, or adds an annotated instruction into the
  // instruction list. This returns the first
  void AnnotateInstruction(Instruction *instr) const;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Environment);
};

}  // namespace granary

#endif  // GRANARY_ENVIRONMENT_H_
