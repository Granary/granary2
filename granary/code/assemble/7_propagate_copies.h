/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_7_PROPAGATE_COPIES_H_
#define GRANARY_CODE_ASSEMBLE_7_PROPAGATE_COPIES_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Perform the following kinds of copy-propagation.
//    1) Register -> register.
//    2) Trivial effective address -> register.
//    3) Register -> base address of memory operand.
//    4) Effective address -> memory arch_operand.
void PropagateRegisterCopies(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_7_PROPAGATE_COPIES_H_
