/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "os/slot.h"

#include "granary/breakpoint.h"

extern "C" {

// Get the base address of the current thread's TLS. We use this address to
// compute `FS`-based offsets from the TLS base. We assume that the base address
// returned by this function is the address associated with `FS:0`.
extern intptr_t granary_arch_get_segment_base(void);

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
intptr_t Slot(SlotCategory category, int sub_category) {
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
  auto slot_addr = reinterpret_cast<intptr_t>(slot_ptr);
  return slot_addr - granary_arch_get_segment_base();
}

}  // namespace os
}  // namespace granary
