/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/code/assemble/5_save_and_restore_flags.h"

#include "granary/metadata.h"
#include "granary/util.h"

namespace granary {
namespace arch {

// Inserts instructions that saves the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
extern void InjectSaveFlags(Fragment *frag);

// Inserts instructions that restore the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
extern void InjectRestoreFlags(Fragment *frag);

}  // namespace arch
namespace {

// Injects architecture-specific code that saves and restores the flags within
// flag entry and exit fragments.
static void InjectSaveAndRestoreFlags(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto flag_entry = DynamicCast<FlagEntryFragment *>(frag)) {
      arch::InjectSaveFlags(flag_entry);
    } else if (auto flag_exit = DynamicCast<FlagExitFragment *>(frag)) {
      arch::InjectRestoreFlags(flag_exit);
    }
  }
}

}  // namespace

// Insert flags saving code into `FRAG_TYPE_FLAG_ENTRY` fragments, and flag
// restoring code into `FRAG_TYPE_FLAG_EXIT` code. We only insert code to save
// and restore flags if it is necessary.
void SaveAndRestoreFlags(FragmentList *frags) {
  InjectSaveAndRestoreFlags(frags);
}

}  // namespace granary
