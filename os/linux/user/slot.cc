/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "os/slot.h"
#include "os/thread.h"

#include "granary/breakpoint.h"

extern "C" {

// Per-thread spill slots.
//
// Note: This depends on a load-time TLS implementation, as is the case on
//       systems like Linux.
__thread __attribute__((tls_model("initial-exec")))
granary::os::SlotSet granary_slots;

}  // extern C
namespace granary {
namespace os {

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
uintptr_t Slot(SlotCategory category, int sub_category) {
  void *slot_ptr(nullptr);
  switch (category) {
    case SLOT_VIRTUAL_REGISTER:
      GRANARY_ASSERT(sub_category < arch::MAX_NUM_SPILL_SLOTS);
      slot_ptr = &(granary_slots.spill_slots[sub_category]);
      break;
    case SLOT_PRIVATE_STACK:
      slot_ptr = &(granary_slots.stack_slot);
      break;
    case SLOT_SAVED_FLAGS:
      slot_ptr = &(granary_slots.flags);
      break;
  }
  return reinterpret_cast<uintptr_t>(slot_ptr) - ThreadBase();
}

}  // namespace os
}  // namespace granary
