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

// Forward declaration.
class Fragment;

// Build a graph for the SSA definitions associated with the fragments.
//
// Note: This does not cover uses in the traditional sense. That is, we only
//       explicitly maintain SSA form for definitions, and uses that reach
//       PHI nodes. However, no information is explicitly maintained to track
//       which registers a given SSA register depends on, as that information
//       is indirectly maintained by the native instructions themselves.
void TrackSSAVars(Fragment * const frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_6_TRACK_SSA_VARS_H_
