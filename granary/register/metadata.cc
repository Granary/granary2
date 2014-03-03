/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/register/metadata.h"

namespace granary {

enum : uint8_t {
  DEFAULT_AVAILABLE_BACKENDS = (1 << REG_BACKEND_GPR)
                               GRANARY_IF_USER( | (1 << REG_BACKEND_TLS)),
};

// Initializes the meta-data. The default initialization treats all
// general purpose registers as being backed by themselves.
BackendMetaData::BackendMetaData(void)
    : is_committed(false),
      is_tainted(false),
      in_live_range_of_generic_vr(false),
      available_backends(DEFAULT_AVAILABLE_BACKENDS),
      offset_from_native_sp(0),
      offset_from_logical_sp(0) {
  for (auto i = 0U; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
    backends.Set(i, REG_BACKEND_GPR);
    locations[i].gpr_index = static_cast<uint8_t>(i);
  }
  GRANARY_UNUSED(offset_from_native_sp);
  GRANARY_UNUSED(offset_from_logical_sp);
}

// Copy construct.
BackendMetaData::BackendMetaData(const BackendMetaData &that) {
  memcpy(this, &that, sizeof that);
}

// Returns ACCEPT/ADAPT/REJECT depending on if one set of virtual register
// mappings can unify with another.
UnificationStatus BackendMetaData::CanUnifyWith(
    const BackendMetaData *meta) const {

  // The available register backends don't match. This is important because
  // it must be safe to access each backend where a register might be stored
  // (in the case of adapting).
  if (available_backends != meta->available_backends) {
    return UnificationStatus::REJECT;
  }

  // This shouldn't really come up in practice. It represents a misuse of the
  // binary operator, i.e. we should only compare `(this,that)` where:
  //      (uncommitted, uncommited)
  //      (uncommitted, committed)
  if (is_committed) {
    return UnificationStatus::REJECT;

  // Try to see if we can unify with an existing basic block.
  } else if (meta->is_committed) {
    if (meta->in_live_range_of_generic_vr) {
      return UnificationStatus::REJECT;
    } else if (!is_tainted) {
      return UnificationStatus::ACCEPT;  // No constraints on our block yet.
    } else {
      return UnificationStatus::ADAPT;  // We've already constrained our block.
    }
  }

  // If there is any discrepancy (in terms of where a GPR is stored) then ADAPT.
  for (auto i = 0U; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
    if (backends.Get(i) != meta->backends.Get(i) ||
        locations[i].value != meta->locations[i].value) {
      return UnificationStatus::ADAPT;
    }
  }

  return UnificationStatus::ACCEPT;
}

}  // namespace granary
