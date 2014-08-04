/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "os/slot.h"

extern "C" {

// Get the base address of the current CPU's private data. We use this address
// to compute `GS`-based offsets from the CPU-private data base. We assume
// that the base address returned by this function is the address associated
// with `GS:0`.
extern intptr_t granary_arch_get_segment_base(void);

// Per-CPU spill slots.
__attribute__((section(".data.percpu")))
granary::os::SlotSet per_cpu__granary_slots;

}  // extern C
namespace granary {
namespace os {

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
intptr_t &Slot(SlotCategory category, int sub_category) {
  switch (category) {
    case SLOT_VIRTUAL_REGISTER:
      GRANARY_ASSERT(sub_category < arch::MAX_NUM_SPILL_SLOTS);
      return granary_slots.spill_slots[sub_category];
    case SLOT_PRIVATE_STACK:
      return granary_slots.stack_slot;
  }
}

}  // namespace os
}  // namespace granary
