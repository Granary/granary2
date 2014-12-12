/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/instruction.h"  // For `arch::Instruction`.

#include "granary/cfg/instruction.h"  // For `NativeInstruction`.

#include "granary/code/ssa.h"  // For `SSAInstruction`, `SSAOperand`.

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
//
// We don't allow copy propagation of the stack pointer, and we require that
// catch issues like `MOV r16, r16` not being copy-propagatable because the
// first (written) operand preserves bytes on write, and therefore appears
// in `uses` instead of `defs`.
bool GetCopiedOperand(const NativeInstruction *instr,
                      SSAInstruction *ssa_instr,
                      SSAOperand **def, SSAOperand **use0, SSAOperand **use1) {
  const auto &ainstr(instr->instruction);
  if (2 != ainstr.num_explicit_ops) return nullptr;

  const auto &dst(ainstr.ops[0]);
  const auto &src(ainstr.ops[1]);

  if (XED_IFORM_LEA_GPRv_AGEN == ainstr.iform) {
    if (src.IsPointer()) return nullptr;
#if 0
    if (src.is_compound) {
      if (src.mem.base.IsStackPointer()) return nullptr;
      if (src.mem.index.IsStackPointer()) return nullptr;
    } else {
      if (src.reg.IsStackPointer()) return nullptr;
    }
#endif
  } else if (XED_IFORM_MOV_GPRv_GPRv_89 == ainstr.iform ||
             XED_IFORM_MOV_GPRv_GPRv_8B == ainstr.iform) {
#if 0
    if (src.reg.IsStackPointer()) return nullptr;
#endif
  } else {
    return nullptr;
  }

#if 0
  if (dst.reg.IsStackPointer()) return nullptr;
#endif

  // Make sure we don't copy propagate things like `MOV r16, r16`.
  if (dst.reg.PreservesBytesOnWrite()) return nullptr;

  // Make sure we don't copy propagate things like `MOV r32, r32`. This could
  // be nasty because we might have:
  //
  //      MOV RBX, 0xFFFFFFFFFFFFFFFF
  //      MOV EAX, EBX
  //      MOV RCX, RAX
  //
  // If we accidentally copy propagated the original RBX into RCX, then we'd
  // have the wrong value!
  if (dst.reg.EffectiveWriteWidth() != dst.reg.ByteWidth()) return nullptr;

  *def = &(ssa_instr->operands[0]);
  *use0 = &(ssa_instr->operands[1]);
  if (3 == ssa_instr->operands.Size()) {
    GRANARY_ASSERT(XED_IFORM_LEA_GPRv_AGEN == ainstr.iform);
    *use1 = &(ssa_instr->operands[2]);
  } else {
    GRANARY_ASSERT(2 == ssa_instr->operands.Size());
  }
  return true;
}

namespace {

static bool ReplaceReg(VirtualRegister &curr_reg, VirtualRegister old_reg,
                       VirtualRegister new_reg) {
  if (curr_reg == old_reg) {
    curr_reg = new_reg.WidenedTo(curr_reg.ByteWidth());
    return true;
  }
  return false;
}

}  // namespace

// Replace the virtual register `old_reg` with the virtual register `new_reg`
// in the operand `op`.
bool ReplaceRegInOperand(Operand *op, VirtualRegister old_reg,
                         VirtualRegister new_reg) {
  if (op->IsRegister()) {
    return ReplaceReg(op->reg, old_reg, new_reg);
  } else if (op->IsMemory() && !op->IsPointer()) {
    if (op->is_compound) {
      auto ret = ReplaceReg(op->mem.base, old_reg, new_reg);
      ret = ReplaceReg(op->mem.index, old_reg, new_reg) || ret;
      return ret;
    } else {
      return ReplaceReg(op->reg, old_reg, new_reg);
    }
  } else {
    return false;
  }
}

// Replace a memory operand with an effective address memory operand.
void ReplaceMemOpWithEffectiveAddress(Operand *mem_op,
                                      const Operand *effective_addr) {
  GRANARY_ASSERT(mem_op->IsMemory());
  GRANARY_ASSERT(effective_addr->IsMemory());
  GRANARY_ASSERT(effective_addr->IsEffectiveAddress());
  *mem_op = *effective_addr;
}

}  // namespace arch
}  // namespace granary
