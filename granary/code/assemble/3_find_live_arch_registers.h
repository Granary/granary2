/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_3_FIND_LIVE_ARCH_REGISTERS_H_
#define GRANARY_CODE_ASSEMBLE_3_FIND_LIVE_ARCH_REGISTERS_H_

#if 0

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class Fragment;

// Calculate the live registers on entry to every fragment.
void FindLiveEntryRegsToFrags(Fragment * const frags);

}  // namespace granary

#endif

#endif  // GRANARY_CODE_ASSEMBLE_3_FIND_LIVE_ARCH_REGISTERS_H_
