/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_10_ADD_CONNECTING_JUMPS_H_
#define GRANARY_CODE_ASSEMBLE_10_ADD_CONNECTING_JUMPS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Adds connectign (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_10_ADD_CONNECTING_JUMPS_H_
