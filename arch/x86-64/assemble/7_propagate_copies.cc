/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/instruction.h"  // For `arch::Instruction`.

#include "granary/cfg/instruction.h"  // For `NativeInstruction`.

#include "granary/code/ssa.h"  // For `SSAInstruction`, `SSAOperand`.

#include "granary/breakpoint.h"
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {
namespace arch {

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
SSAOperand *GetCopiedOperand(const NativeInstruction *instr,
                             SSAInstruction *ssa_instr) {
  if (1UL != ssa_instr->defs.Size()) return nullptr;
  if (!ssa_instr->uses.Size()) return nullptr;

  const auto &ainstr(instr->instruction);
  if (ainstr.is_save_restore) return nullptr;

  GRANARY_IF_DEBUG( const auto &op0(ainstr.ops[0]);)
  const auto &op1(ainstr.ops[1]);

  // We don't allow copy propagation of the stack pointer, and we require that
  // catch issues like `MOV r16, r16` not being copy-propagatable because the
  // first (written) operand preserves bytes on write, and therefore appears
  // in `uses` instead of `defs`.
  VirtualRegister copied_reg;
  if (XED_IFORM_MOV_GPRv_GPRv_89 == ainstr.iform ||
      XED_IFORM_MOV_GPRv_GPRv_8B == ainstr.iform) {
    GRANARY_ASSERT(op0.IsRegister());
    GRANARY_ASSERT(op1.IsRegister());
    if (op1.reg.IsStackPointer()) return nullptr;

  } else if (XED_ICLASS_LEA == ainstr.iclass &&
             2 == ainstr.num_explicit_ops) {
    if (op1.is_compound) {
      auto base_reg = VirtualRegister::FromNative(op1.mem.reg_base);
      if (base_reg.IsStackPointer()) return nullptr;

      auto index_reg = VirtualRegister::FromNative(op1.mem.reg_base);
      if (index_reg.IsStackPointer()) return nullptr;
    } else {
      if (op1.reg.IsStackPointer()) return nullptr;
    }
  } else {
    return nullptr;
  }

  // This shouldn't come up because we'll see that `op0.reg` is actually a
  // `READ_WRITE` use, and therefore `0 == ssa_instr->defs.Size()`.
  GRANARY_ASSERT(!op0.reg.PreservesBytesOnWrite());

  return &(ssa_instr->uses[0]);
}

// Returns true if we can propagate the register `source` into the place of the
// register `dest`.
extern bool CanPropagate(VirtualRegister source, VirtualRegister dest) {
  return source.BitWidth() == dest.BitWidth() && 32 <= source.BitWidth();
}

}  // namespace arch
}  // namespace granary
