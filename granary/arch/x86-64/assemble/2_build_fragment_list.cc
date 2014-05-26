/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"

#include "granary/code/assemble/fragment.h"

namespace granary {
namespace arch {
// Table of all implicit operands.
extern const Operand * const IMPLICIT_OPERANDS[];

// Number of implicit operands for each iclass.
extern const int NUM_IMPLICIT_OPERANDS[];

}  // namespace arch

namespace {

bool HintFragment(const arch::Operand &op) {
  if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
    if (op.is_compound) {
      return XED_REG_RAX == op.mem.reg_base ||
             XED_REG_RAX == op.mem.reg_index;
    }
  } else if (XED_ENCODER_OPERAND_TYPE_REG != op.type) {
    return false;
  }
  if (op.reg.IsGeneralPurpose()) {
    auto reg = op.reg;
    reg.Widen(arch::GPR_WIDTH_BYTES);
    return XED_REG_RAX == reg.EncodeToNative();
  }
  return false;
}

}  // namespace

// Try to add a flag split hint to a code fragment.
void TryAddFlagSplitHint(CodeFragment *frag, const NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  for (auto &op : ainstr.ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    if (HintFragment(op)) {
      frag->attr.has_flag_split_hint = true;
      return;
    }
  }
  auto implicit_ops = arch::IMPLICIT_OPERANDS[ainstr.iclass];
  for (auto i = 0; i < arch::NUM_IMPLICIT_OPERANDS[ainstr.iclass]; ++i) {
    if (HintFragment(implicit_ops[i])) {
      frag->attr.has_flag_split_hint = true;
      return;
    }
  }
}

// Returns true if this instruction can change the interrupt enabled state on
// this CPU.
bool ChangesInterruptDeliveryState(const NativeInstruction *instr) {
  auto iclass = instr->instruction.iclass;

  // Note: We ignore `POPF/Q` because it will mark the stack as valid, and
  //       therefore virtual register allocation around a `POPF/Q` will use
  //       stack allocation, and not use something like per-CPU or per-thread
  //       data.
  return XED_ICLASS_STI == iclass || XED_ICLASS_CLI == iclass;
}

}  // namespace granary
