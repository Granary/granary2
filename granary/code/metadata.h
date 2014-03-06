/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_METADATA_H_
#define GRANARY_CODE_METADATA_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"
#include "granary/base/packed_array.h"
#include "granary/metadata.h"

namespace granary {

// Forward declarations.
class BackendMetaData;
class StackMetaData;

// Backend that is managing one of the general purpose registers. These are
// ordered in terms of preference.
enum RegBackend : uint8_t {
  REG_BACKEND_GPR     = 0,  // Stored in a general-purpose register.
  REG_BACKEND_STACK   = 1,  // Spilled on the stack.
  REG_BACKEND_TLS     = 2   // Spilled into thread-local storage.
};

// Something that identifies a register's location within a virtual register
// backend.
union RegLocation {
  // The value of this GPR is stored in another GPR identified by `gpr_num`.
  uint8_t gpr_index;

  // Where is this register located relative to the stack pointer? The
  // calculation to find the register's location is:
  //    `stack pointer + (spill_slot * arch::GPR_WIDTH_BYTES)`
  int8_t stack_slot;

  // Slot in thread-local storage where this register was spilled.
  uint8_t tls_slot;

  // Generic value for zeroing out the value.
  uint8_t value;
} __attribute__((packed));

static_assert(sizeof(RegLocation) == 1,
    "Invalid packing of union `RegLocation`.");

// Meta-data that tracks the backend in which each architectural register is
// stored.
class BackendMetaData : public UnifiableMetaData<BackendMetaData> {
 public:
  // Initializes the meta-data. The default initialization treats all
  // general purpose registers as being backed by themselves.
  BackendMetaData(void);

  // Copy construct.
  BackendMetaData(const BackendMetaData &that);

  // Returns ACCEPT/ADAPT/REJECT depending on if one set of virtual register
  // mappings can unify with another.
  UnificationStatus CanUnifyWith(const BackendMetaData *that) const;

 private:
  // For each general purpose register, this tells us in which backend the
  // register is stored.
  PackedArray<RegBackend, 2, arch::NUM_GENERAL_PURPOSE_REGISTERS> backends;

  // Locations describing where in each backend the register has been saved.
  RegLocation locations[arch::NUM_GENERAL_PURPOSE_REGISTERS];

  // Do the backends/locations represent hard constraints?
  bool is_committed:1;

  // Has the current meta-data been tainted? The idea here is that upon
  // initialization, everything is untainted, but once a change is made, the
  // state becomes tainted. Therefore, untained and uncommitted metadata can
  // be perfectly unified with committed metadata, because there are no
  // hard constraints on the current register scheduling.
  bool is_tainted:1;

  // Is this basic block in the live range of a generic (i.e. LCFG-wide) virtual
  // register? If yes, and if this block has been committed, then an uncommitted
  // block cannot unify with this block, lest control jump into this block and
  // reach a use of an otherwise unitialized generic virtual register.
  bool in_live_range_of_generic_vr:1;

  // Bitmask tracking backends are available for use at the beginning of this
  // basic block.
  uint8_t available_backends:4;

  // How far off (in bytes) the current stack pointer is from what it should
  // be if the program is executing natively.
  //
  // Note: This is only meaningful if the stack backend is available.
  int8_t offset_from_native_sp;

  // The offset from the native stack pointer where the "logical" base of the
  // stack frame is. In kernel space this is always 0; however, in user space,
  // leaf functions can use (without allocating) parts of the stack. Those parts
  // of the stack cannot be used for spilling registers.
  //
  // Note: This is only meaningful if the stack backend is available.
  uint8_t offset_from_logical_sp;

} __attribute__((packed));

}  // namespace granary

#endif  // GRANARY_CODE_METADATA_H_
