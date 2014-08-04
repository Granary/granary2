/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "os/slot.h"

extern "C" {

// Per-CPU spill slots.
granary::os::SlotSet *granary_slots(nullptr);

}  // extern C
namespace granary {
namespace os {

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
intptr_t Slot(SlotCategory category, int sub_category) {
  switch (category) {
    case SLOT_VIRTUAL_REGISTER:
      GRANARY_ASSERT(sub_category < arch::MAX_NUM_SPILL_SLOTS);
      return reinterpret_cast<intptr_t>(
          &(granary_slots->spill_slots[sub_category]));
    case SLOT_PRIVATE_STACK:
      return reinterpret_cast<intptr_t>(&(granary_slots->stack_slot));
  }
}

}  // namespace os
}  // namespace granary
