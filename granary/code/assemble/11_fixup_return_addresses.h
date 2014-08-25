/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_11_FIXUP_RETURN_ADDRESSES_H_
#define GRANARY_CODE_ASSEMBLE_11_FIXUP_RETURN_ADDRESSES_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Makes sure that all `IA_RETURN_ADDRESS` annotations are in the correct
// position.
void FixupReturnAddresses(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_11_FIXUP_RETURN_ADDRESSES_H_
