/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"  // For `arch::Instruction`.

#include "granary/cfg/instruction.h"  // For `NativeInstruction`.

#include "granary/code/assemble/ssa.h"  // For `SSAInstruction`, `SSAOperand`.

#include "granary/breakpoint.h"
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
//
// Note: This has an architecture-specific implementation.
SSAOperand *GetCopiedOperand(const NativeInstruction *instr) {
  const auto &instruction(instr->instruction);
  const auto iclass = instruction.iclass;
  auto ssa_instr = GetMetaData<SSAInstruction *>(instr);
  if (XED_ICLASS_MOV == iclass) {

    // We don't allow copy propagation of the stack pointer, and we require that
    // catch issues like `MOV r16, r16` not being copy-propagatable because the
    // first (written) operand preserves bytes on write, and therefore appears
    // in `uses` instead of `defs`.
    if (instruction.ops[0].IsRegister() && instruction.ops[1].IsRegister() &&
        !instruction.ops[1].reg.IsStackPointer() &&
        1UL == ssa_instr->defs.Size()) {
      return &(ssa_instr->uses[0]);
    }

  // The extra check on the size of the `defs` is to handle cases like:
  //      `LEA A, [A, B]`
  // Where from the `iclass` it looks like a copy op, but really it is not.
  // The check on the `defs` size is sufficient because we ensure that if a
  // register appears as both a read and write in the instruction, then the
  // operand that writes to the register is treated as a read/write, and is
  // thus placed into the `uses` operand pack.
  } else if (XED_ICLASS_LEA == iclass && 1UL == ssa_instr->defs.Size()) {
    if (instruction.ops[1].is_compound) {
      auto base = VirtualRegister::FromNative(instruction.ops[1].mem.reg_base);
      if (base.IsStackPointer()) return nullptr;
    } else if (instruction.ops[1].reg.IsStackPointer()) {
      return nullptr;
    }
    return &(ssa_instr->uses[0]);
  }
  return nullptr;
}

// Returns true if we can propagate the register `source` into the place of the
// register `dest`.
extern bool CanPropagate(VirtualRegister source, VirtualRegister dest) {
  return source.BitWidth() == dest.BitWidth() && 32 <= source.BitWidth();
}

}  // namespace granary
