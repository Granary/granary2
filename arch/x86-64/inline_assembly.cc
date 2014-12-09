/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/operand.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/lir.h"

#include "granary/code/inline_assembly.h"

namespace granary {
namespace lir {

// Gives access to one of the registers defined within the inline assembly.
//
// This is a bit tricky because inline assembly is only parsed later. The
// solution employed is to "pre allocated" the virtual register number when
// it's requested here, then use that later when the virtual register is
// needed.
RegisterOperand InlineAssembly::Register(DecodedBasicBlock *block,
                                         int reg_num) const {
  auto &aop(*(scope->vars[reg_num]));
  if (!scope->var_is_initialized[reg_num]) {
    scope->var_is_initialized[reg_num] = true;
    aop.reg = block->AllocateVirtualRegister();
    aop.width = arch::GPR_WIDTH_BITS;
    aop.rw = XED_OPERAND_ACTION_R;
    aop.type = XED_ENCODER_OPERAND_TYPE_REG;
    aop.is_explicit = true;
  } else {
    GRANARY_ASSERT(aop.IsRegister());
  }
  return RegisterOperand(&aop);
}

}  // namespace lir
}  // namespace granary
