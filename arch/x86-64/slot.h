/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_X86_64_SLOT_H_
#define ARCH_X86_64_SLOT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "os/slot.h"

#include "arch/x86-64/operand.h"

namespace granary {
namespace arch {

// Used to access some kind of private slot, e.g. virtual register spill slot
// as a memory operand.
arch::Operand SlotMemOp(os::SlotCategory category, int sub_category=0,
                        int width=-1);

}  // namespace arch
}  // namespace granary

#endif  // ARCH_X86_64_SLOT_H_
