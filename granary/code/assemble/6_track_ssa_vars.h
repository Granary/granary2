/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_6_TRACK_SSA_VARS_H_
#define GRANARY_CODE_ASSEMBLE_6_TRACK_SSA_VARS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/cast.h"
#include "granary/base/new.h"

#include "granary/code/register.h"

namespace granary {

// Build a graph for the SSA definitions associated with the fragments.
void TrackSSAVars(FragmentList * const frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_6_TRACK_SSA_VARS_H_
