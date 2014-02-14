/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/driver/dynamorio/relativize.h"

namespace granary {
namespace driver {

// Make a native instruction safe to execute from within the code cache.
// This sometimes results in additional instructions being
void InstructionRelativizer::Relativize(NativeInstruction *native_instr_) {
  native_instr = native_instr_;
  instr = native_instr->instruction.get();
  switch (instr->instruction.opcode) {
    case dynamorio::OP_lea: return RelativizeLEA();
    case dynamorio::OP_push: return RelativizePUSH();
    case dynamorio::OP_pop: return RelativizePOP();
    case dynamorio::OP_jmp:
    case dynamorio::OP_jmp_far:
    case dynamorio::OP_call:
    case dynamorio::OP_call_far: return RelativizeCFI();
  }
  GRANARY_UNUSED(cache_pc);
}

void InstructionRelativizer::RelativizeLEA(void) {}
void InstructionRelativizer::RelativizePUSH(void) {}
void InstructionRelativizer::RelativizePOP(void) {}
void InstructionRelativizer::RelativizeCFI(void) {}

}  // namespace driver
}  // namespace granary
