/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/code/fragment.h"

namespace granary {
namespace arch {
// Table of all implicit operands.
extern const Operand * const IMPLICIT_OPERANDS[];

// Number of implicit operands for each iclass.
extern const int NUM_IMPLICIT_OPERANDS[];

namespace {

// Returns true if `reg` is one of the registers encompassed by `RAX`.
static bool HintFragment(int reg) {
  switch (static_cast<xed_reg_enum_t>(reg)) {
    case XED_REG_AL:
    case XED_REG_AH:
    case XED_REG_AX:
    case XED_REG_EAX:
    case XED_REG_RAX:
      return true;
    default:
      return false;
  }
}

// Returns true if `op` uses `RAX` (or some other version of it).
static bool HintFragment(const arch::Operand &op) {
  if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
    if (op.is_compound) {
      return HintFragment(op.mem.reg_base) || HintFragment(op.mem.reg_index);
    } else {
      // Handled by `op.reg` case below.
    }
  } else if (XED_ENCODER_OPERAND_TYPE_REG != op.type) {
    return false;
  }
  if (op.reg.IsGeneralPurpose() && op.reg.IsNative()) {
    return HintFragment(op.reg.EncodeToNative());
  }
  return false;
}

}  // namespace

// Does this instruction hint that the fragment should be split before the next
// modification of the flags?
bool InstructionHintsAtFlagSplit(const NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  for (auto &op : ainstr.ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    if (HintFragment(op)) {
      return true;
    }
  }
  auto implicit_ops = arch::IMPLICIT_OPERANDS[ainstr.iclass];
  for (auto i = 0; i < arch::NUM_IMPLICIT_OPERANDS[ainstr.iclass]; ++i) {
    if (HintFragment(implicit_ops[i])) {
      return true;
    }
  }
  return false;
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

}  // namespace arch
}  // namespace granary
