/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/ir/lir.h"

#include "granary/driver.h"

namespace granary {
namespace lir {
namespace {

// Create a control-flow instruction with a 32-bit PC-relative operand (whose
// effective width is 64 bits). We initialize the relative branch target to
// `0`, but don't mark the instruction as being modified.
static driver::Instruction *MakeCFI(driver::Instruction *instr,
                                    xed_iclass_enum_t iclass,
                                    xed_category_enum_t category,
                                    AppPC target) {
  memset(instr, 0, sizeof *instr);

  instr->iclass = iclass;
  instr->category = category;
  instr->num_explicit_ops = 1;

  // TODO(pag): Re-do me!!

  instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  instr->ops[0].width = arch::ADDRESS_WIDTH_BITS;
  instr->ops[0].addr.as_pc = target;
  instr->ops[0].rw = XED_OPERAND_ACTION_R;

  return instr;
}
}  // namespace

// Call to an existing basic block.
std::unique_ptr<Instruction> Call(BasicBlock *target_block) {
  driver::Instruction instr;
  MakeCFI(&instr, XED_ICLASS_CALL_NEAR, XED_CATEGORY_CALL,
          target_block->StartAppPC());
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&instr, target_block));
}

// Jump to an existing basic block.
std::unique_ptr<Instruction> Jump(BasicBlock *target_block) {
  driver::Instruction instr;
  MakeCFI(&instr, XED_ICLASS_JMP, XED_CATEGORY_UNCOND_BR,
          target_block->StartAppPC());
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&instr, target_block));
}

}  // namespace lir
}  // namespace granary
