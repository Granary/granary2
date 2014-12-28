/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_SLOT_H_
#define OS_SLOT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "os/slot.h"
#include "arch/base.h"

namespace granary {
namespace os {

enum SlotCategory : size_t {
  SLOT_VIRTUAL_REGISTER,
  SLOT_PRIVATE_STACK,
  SLOT_SAVED_FLAGS
};

struct SlotSet {
  // Pointer to a thread- or CPU-private stack.
  uintptr_t stack_slot;

  // Saved flags.
  uint64_t flags;

  // Used for spilling general-purpose registers, so that a spilled GPR can be
  // used to hold the value of a virtual register.
  uintptr_t spill_slots[arch::MAX_NUM_SPILL_SLOTS];
};

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
uintptr_t Slot(os::SlotCategory category, size_t sub_category=0);

}  // namespace os
}  // namespace granary

#endif  // OS_SLOT_H_
