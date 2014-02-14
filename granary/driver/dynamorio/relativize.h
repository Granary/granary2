/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_DYNAMORIO_RELATIVIZE_H_
#define GRANARY_DRIVER_DYNAMORIO_RELATIVIZE_H_

#include "granary/base/base.h"

#include "generated/dynamorio/builder.h"

namespace granary {

// Forward declarations.
class NativeInstruction;

namespace driver {

// Used to relativize native instructions, i.e. make the continue to work,
// even though their locations (native vs. cache) drastically change.
class InstructionRelativizer {
 public:
  // Initialize an instruction relativizer with an estimated location at which
  // instructions will be encoded.
  inline explicit InstructionRelativizer(CacheProgramCounter cache_pc_)
      : builder(),
        cache_pc(cache_pc_),
        native_instr(nullptr),
        instr(nullptr) {}

  // Make a native instruction safe to execute from within the code cache.
  // This sometimes results in additional instructions being
  void Relativize(NativeInstruction *native_instr_);

 private:
  InstructionRelativizer(void) = delete;

  void RelativizeLEA(void);
  void RelativizePUSH(void);
  void RelativizePOP(void);
  void RelativizeCFI(void);

  // Used to build new instructions.
  InstructionBuilder builder;

  // Estimated location at which this instruction will be encoded.
  CacheProgramCounter cache_pc;

  // Instruction currently being relativized.
  NativeInstruction *native_instr;
  DecodedInstruction *instr;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionRelativizer);
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_DYNAMORIO_RELATIVIZE_H_
