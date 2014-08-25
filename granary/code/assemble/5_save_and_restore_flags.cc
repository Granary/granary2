/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/metadata.h"

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

// Initialize the set of live in all exit fragments. All other fragments start
// off with null sets of lvie regs on exit.
static void InitLiveRegsOnExit(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<ExitFragment *>(frag)) {
      frag->regs.live_on_entry.ReviveAll();
    }
  }
}

static LiveRegisterTracker LiveRegsOnEntry(Fragment *frag) {
  return frag->regs.live_on_entry;
}

// Returns the live registers on exit from a fragment.
static LiveRegisterTracker LiveRegsOnExit(Fragment *frag) {
  LiveRegisterTracker regs;
  if (IsA<ExitFragment *>(frag)) {
    regs = LiveRegsOnEntry(frag);
  } else if (frag->branch_instr) {
    if (frag->branch_instr->IsConditionalJump()) {
      regs = LiveRegsOnEntry(frag->successors[FRAG_SUCC_FALL_THROUGH]);
      regs.Union(LiveRegsOnEntry(frag->successors[FRAG_SUCC_BRANCH]));
    } else {
      regs = LiveRegsOnEntry(frag->successors[FRAG_SUCC_BRANCH]);
    }
  } else {
    for (auto succ : frag->successors) {
      if (succ) {
        regs.Union(LiveRegsOnEntry(succ));
      }
    }
  }
  return regs;
}

// Analyze the register usage within a block. Returns `true` if the set of live
// registers on entry to this fragment has changed since the last time we
// analyzed this fragment.
static bool AnalyzeFragRegs(Fragment *frag) {
  LiveRegisterTracker regs(LiveRegsOnExit(frag));
  auto changed = !frag->regs.live_on_exit.Equals(regs);
  auto seen_native_instr = false;
  frag->regs.live_on_exit = regs;
  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (seen_native_instr && frag->branch_instr == instr) {
        auto branch = frag->successors[FRAG_SUCC_BRANCH];
        if (frag->branch_instr->IsConditionalJump()) {
          regs.Union(LiveRegsOnEntry(branch));
        } else {
          regs = LiveRegsOnEntry(branch);
        }
      }
      seen_native_instr = true;
      regs.Visit(ninstr);
    }
  }
  changed = changed || !frag->regs.live_on_entry.Equals(regs);
  frag->regs.live_on_entry = regs;
  return changed;
}

// Goes and finds all live regs on entry to a fragment.
static void AnalyzeFragRegs(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      if (!IsA<ExitFragment *>(frag)) {
        changed = AnalyzeFragRegs(frag) || changed;
      }
    }
  }
}

// Tracks which registers are used anywhere in the flag zone.
static void UpdateUsedRegsInFlagZone(FlagZone *zone, CodeFragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    zone->used_regs.Visit(DynamicCast<NativeInstruction *>(instr));
  }
}

// Update the flag zones with the flags and registers used in the various
// fragments that belong to this flag zone, as well as the flags used *after*
// the flag zone.
static void UpdateFlagZones(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto flag_zone = frag->flag_zone.Value()) {
      if (auto code = DynamicCast<CodeFragment *>(frag)) {
        if (CODE_TYPE_APP != code->type) {
          UpdateUsedRegsInFlagZone(flag_zone, code);
        }
      } else if (auto flag_exit = DynamicCast<FlagExitFragment *>(frag)) {
        flag_zone->live_regs.Union(flag_exit->regs.live_on_entry);
      }
    }
  }
}

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
  InitLiveRegsOnExit(frags);
  AnalyzeFragRegs(frags);
  UpdateFlagZones(frags);
  InjectSaveAndRestoreFlags(frags);
}

}  // namespace granary
