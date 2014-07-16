/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/base.h"
#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/slot.h"

extern "C" {

// Get the base address of the current thread's TLS. We use this address to
// compute `FS`-based offsets from the TLS base. We assume that the base address
// returned by this function is the address associated with `FS:0`.
extern intptr_t granary_arch_get_segment_base(void);

}  // extern C

namespace granary {
namespace arch {
namespace {

struct SlotSet {
  // Used for spilling general-purpose registers, so that a spilled GPR can be
  // used to hold the value of a virtual register.
  intptr_t spill_slots[MAX_NUM_SPILL_SLOTS];

  // Pointer to a thread- or CPU-private stack.
  intptr_t stack_slot;
};

#if defined(GRANARY_WHERE_kernel) || !defined(GRANARY_WHERE_user)
# error "TODO(pag): Implement `EdgeSlot` and `EdgeSlotOffset` for kernel space."
#else

// Per-thread edge slots.
//
// Note: This depends on a load-time TLS implementation, as is the case on
//       systems like Linux.
//
// TODO(pag): Eventually this should be made context-specific. One way of
//            doing this would be to have an upper bound on the number of
//            concurrently live contexts, and then have this be an array of
//            `EdgeSlotSet`, indexed by context id.
static __thread __attribute__((tls_model("initial-exec"))) SlotSet SLOTS;

#endif  // User-space implementation of edge spill slots.
}  // namespace

// Access the value of some kind of private slot (by reference). This is an
// instance of the requested slot, although many such instances might actually
// exist.
intptr_t &Slot(SlotCategory category, int sub_category) {
  switch (category) {
    case SLOT_VIRTUAL_REGISTER:
      return SLOTS.spill_slots[sub_category];
    case SLOT_PRIVATE_STACK:
      return SLOTS.stack_slot;
  }
}

namespace {
// Returns the offset of one of the edge slots.
static intptr_t SlotOffset(SlotCategory category, int sub_category) {
  auto this_slot = &(Slot(category, sub_category));
  auto this_slot_addr = reinterpret_cast<intptr_t>(this_slot);
  return this_slot_addr - granary_arch_get_segment_base();
}
}  // namespace

// Used to access some kind of private slot, e.g. virtual register spill slot
// as a memory operand.
arch::Operand SlotMemOp(SlotCategory category, int sub_category, int width) {
  arch::Operand op;

  op.type = XED_ENCODER_OPERAND_TYPE_PTR;
  op.segment = GRANARY_IF_USER_ELSE(XED_REG_FS, XED_REG_GS);  // Linux-specific.
  op.is_compound = true;
  op.addr.as_int = SlotOffset(category, sub_category);
  op.width = static_cast<int16_t>(0 >= width ? arch::GPR_WIDTH_BITS : width);
  return op;
}

}  // namespace arch
}  // namespace granary
