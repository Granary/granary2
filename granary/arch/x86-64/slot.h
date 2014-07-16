/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_64_SLOT_H_
#define GRANARY_ARCH_X86_64_SLOT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/x86-64/operand.h"

namespace granary {
namespace arch {

enum SlotCategory {
  SLOT_VIRTUAL_REGISTER,
  SLOT_PRIVATE_STACK
};

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
intptr_t &Slot(SlotCategory category, int sub_category=0);

// Used to access some kind of private slot, e.g. virtual register spill slot
// as a memory operand.
arch::Operand SlotMemOp(SlotCategory category, int sub_category=0,
                        int width=-1);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_X86_64_SLOT_H_
