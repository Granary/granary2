/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/metadata.h"

#include "granary/util.h"

namespace granary {

// Tells us if we can unify our (uncommitted) meta-data with some existing
// meta-data.
UnificationStatus StackMetaData::CanUnifyWith(const StackMetaData &that) const {

  // If our block has no information, then just blindly accept the other
  // block. In this case, we don't want to generate excessive numbers of
  // versions of the block.
  //
  // The concern here is this can lead to undefined behavior if, at assembly
  // time, the fragment colorer decides that a successor to the block with
  // this meta-data is using an undefined stack, and this block is using a
  // defined one. In this case, we hope for the best.
  if (!has_stack_hint) {
    if (that.behaves_like_callstack) {
      has_stack_hint = true;
      behaves_like_callstack = true;
    }

  // TODO(pag): This might be overly aggressive. In future we'll see if this
  //            is really required.
  } else if (that.behaves_like_callstack) {
    behaves_like_callstack = true;
  }

  return UnificationStatus::ACCEPT;
}

}  // namespace granary
