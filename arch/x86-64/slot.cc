/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/base.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/slot.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Used to access some kind of private slot, e.g. virtual register spill slot
// as a memory operand.
arch::Operand SlotMemOp(os::SlotCategory category, int sub_category,
                        int width) {
  arch::Operand op;

  op.type = XED_ENCODER_OPERAND_TYPE_PTR;
  op.segment = GRANARY_IF_USER_ELSE(XED_REG_FS, XED_REG_GS);  // Linux-specific.
  op.is_compound = true;
  op.addr.as_int = os::Slot(category, sub_category);
  op.width = static_cast<int16_t>(0 >= width ? arch::GPR_WIDTH_BITS : width);
  return op;
}

}  // namespace arch
}  // namespace granary