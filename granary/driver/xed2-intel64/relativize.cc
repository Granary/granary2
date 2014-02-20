/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/driver/xed2-intel64/builder.h"
#include "granary/driver/xed2-intel64/relativize.h"

namespace granary {
namespace driver {

// Make a native instruction safe to execute from within the code cache.
// This sometimes results in additional instructions being
void InstructionRelativizer::Relativize(NativeInstruction *native_instr_) {
  native_instr = native_instr_;
  instr = native_instr->instruction.get();
  if (instr->has_pc_rel_op) {
    switch (instr->iclass) {
      case XED_ICLASS_LEA: return RelativizeLEA();
      case XED_ICLASS_PUSH: return RelativizePUSH();
      case XED_ICLASS_POP: return RelativizePOP();
      case XED_ICLASS_JMP:
      case XED_ICLASS_JMP_FAR:
      case XED_ICLASS_CALL_FAR:
      case XED_ICLASS_CALL_NEAR: return RelativizeCFI();
      default: return;
    }
  }
  GRANARY_UNUSED(cache_pc);
}

namespace {
enum {
  _3_75_GB = 4026531840L
};

// Returns true if an address needs relativizing.
static bool AddressNeedsRelativizing(PC relative_pc, PC cache_pc) {
  auto signed_diff = relative_pc - cache_pc;
  auto diff = 0 > signed_diff ? -signed_diff : signed_diff;
  return _3_75_GB < diff;
}
}  // namespace

// Convert a RIP-relative LEA into a MOV <imm64>, reg.
void InstructionRelativizer::RelativizeLEA(void) {
  auto rel_pc = instr->ops[1].rel.pc;
  if (AddressNeedsRelativizing(rel_pc, cache_pc)) {
    auto rel_ptr = reinterpret_cast<uintptr_t>(rel_pc);
    auto dest_reg = instr->ops[0].u.reg;
    MOV_GPRv_IMMv(instr, Register(dest_reg), Immediate(rel_ptr));
  }
}

void InstructionRelativizer::RelativizePUSH(void) {

}

void InstructionRelativizer::RelativizePOP(void) {

}

void InstructionRelativizer::RelativizeCFI(void) {

}

}  // namespace driver
}  // namespace granary
