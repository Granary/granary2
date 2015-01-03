/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_6_TRACK_VIRTUAL_REGS_H_
#define GRANARY_CODE_ASSEMBLE_6_TRACK_VIRTUAL_REGS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/cast.h"
#include "granary/base/new.h"

#include "granary/code/register.h"

namespace granary {

// Figure out the live VRs on entry/exit from each frag.
void TrackVirtualRegs(FragmentList * const frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_6_TRACK_VIRTUAL_REGS_H_
