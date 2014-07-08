/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"  // For `arch::Instruction`.

#include "granary/cfg/instruction.h"  // For `NativeInstruction`.

#include "granary/code/ssa.h"  // For `SSAInstruction`, `SSAOperand`.

#include "granary/breakpoint.h"
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {
namespace arch {

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
SSAOperand *GetCopiedOperand(const NativeInstruction *instr) {
  auto ssa_instr = GetMetaData<SSAInstruction *>(instr);
  if (!ssa_instr) return nullptr;  // E.g. no operands, or operates on stack.

  if (1UL != ssa_instr->defs.Size() || !ssa_instr->uses.Size()) {
    return nullptr;
  }

  const auto &instruction(instr->instruction);
  const auto iclass = instruction.iclass;

  // We don't allow copy propagation of the stack pointer, and we require that
  // catch issues like `MOV r16, r16` not being copy-propagatable because the
  // first (written) operand preserves bytes on write, and therefore appears
  // in `uses` instead of `defs`.
  VirtualRegister copied_reg;
  if (XED_ICLASS_MOV == iclass) {
    if (!instruction.ops[0].IsRegister()) return nullptr;
    if (!instruction.ops[1].IsRegister()) return nullptr;
    copied_reg = instruction.ops[1].reg;
  } else if (XED_ICLASS_LEA == iclass && 2 == instruction.num_explicit_ops) {
    if (instruction.ops[1].is_compound) {
      copied_reg = VirtualRegister::FromNative(instruction.ops[1].mem.reg_base);
    } else {
      copied_reg = instruction.ops[1].reg;
    }
  } else {
    return nullptr;
  }
  if (copied_reg.IsStackPointer()) return nullptr;
  return &(ssa_instr->uses[0]);
}

// Returns true if we can propagate the register `source` into the place of the
// register `dest`.
extern bool CanPropagate(VirtualRegister source, VirtualRegister dest) {
  return source.BitWidth() == dest.BitWidth() && 32 <= source.BitWidth();
}

}  // namespace arch
}  // namespace granary
