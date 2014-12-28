/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_X86_64_EARLY_MANGLE_H_
#define ARCH_X86_64_EARLY_MANGLE_H_

#include "granary/code/register.h"

namespace granary {

class DecodedBlock;

namespace arch {

class Instruction;

struct EarlyMangler {
 public:
  enum {
    kMaxNumVirtRegs = 4
  };

  // Initialize the early mangler.
  EarlyMangler(DecodedBlock *block_);

  // Allocate a virtual register.
  VirtualRegister AllocateVirtualRegister(
        size_t num_bytes=arch::GPR_WIDTH_BYTES);

  // Perform "early" mangling of some instructions. This is primary to make the
  // task of virtual register allocation tractable.
  void MangleDecodedInstruction(Instruction *instr,
                                bool is_recursive_call=false);

  // Which of `regs` should next be used?
  size_t reg_num;

  // Pre-allocated virtual registers for use by different instructions. We re-
  // use virtual registers in mangling to simplify later register spill slot
  // sharing.
  VirtualRegister regs[kMaxNumVirtRegs];

  // What block is being instrumented?
  DecodedBlock * const block;

 private:
  EarlyMangler(void) = delete;
};

}  // namespace arch
}  // namespace granary

#endif  // ARCH_X86_64_EARLY_MANGLE_H_
