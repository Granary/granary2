/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_5_SAVE_AND_RESTORE_FLAGS_H_
#define GRANARY_CODE_ASSEMBLE_5_SAVE_AND_RESTORE_FLAGS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Insert flags saving code into `FRAG_TYPE_FLAG_ENTRY` fragments, and flag
// restoring code into `FRAG_TYPE_FLAG_EXIT` code. We only insert code to save
// and restore flags if it is necessary.
void SaveAndRestoreFlags(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_5_SAVE_AND_RESTORE_FLAGS_H_
