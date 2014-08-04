/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OS_SLOT_H_
#define GRANARY_OS_SLOT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "os/slot.h"
#include "arch/base.h"

namespace granary {
namespace os {

enum SlotCategory {
  SLOT_VIRTUAL_REGISTER,
  SLOT_PRIVATE_STACK
};

struct SlotSet {
  // Used for spilling general-purpose registers, so that a spilled GPR can be
  // used to hold the value of a virtual register.
  intptr_t spill_slots[arch::MAX_NUM_SPILL_SLOTS];

  // Pointer to a thread- or CPU-private stack.
  intptr_t stack_slot;
};

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
intptr_t Slot(os::SlotCategory category, int sub_category=0);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_OS_SLOT_H_
