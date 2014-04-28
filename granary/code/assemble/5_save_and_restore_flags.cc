/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/metadata.h"

#include "granary/metadata.h"
#include "granary/util.h"

// TODO(pag): Implement "smart" saving/restoring of flags. For example:
//
//                  <instrumentation I1, kills F1>
//                  <native, doesn't r/w flags>
//                  <instrumentation I2, kills F1>
//                  <native, reads F1>
//
//            Then we could be clever about saving F1 before I1 and restoring
//            F1 after I2 if and only if I1 and I2 are within the same fragment
//            partition.

namespace granary {

// Returns the architectural register that is potentially killed by the
// instructions injected to save/restore flags.
//
// Note: This must return a register with width `arch::GPR_WIDTH_BYTES` if the
//       returned register is valid.
//
// Note: This has an architecture-specific implementation.
VirtualRegister FlagKillReg(void);

// Inserts instructions that saves the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
void InjectSaveFlags(FlagEntryFragment *frag);

// Inserts instructions that restore the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
void InjectRestoreFlags(FlagExitFragment *frag);

namespace {

// Initialize the set of live in all exit fragments. All other fragments start
// off with null sets of lvie regs on exit.
static void InitLiveRegsOnExit(FragmentList *frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (auto exit_frag = DynamicCast<ExitFragment *>(frag)) {
      switch (exit_frag->kind) {
        case FRAG_EXIT_NATIVE:
          frag->regs.live_on_entry.ReviveAll();
          break;

        case FRAG_EXIT_FUTURE_BLOCK:
        case FRAG_EXIT_EXISTING_BLOCK:
          auto meta = MetaDataCast<RegisterMetaData *>(exit_frag->block_meta);
          if (meta) {
            frag->regs.live_on_entry = meta->live_regs;
          } else {
            frag->regs.live_on_entry.ReviveAll();  // Return w/o meta case.
          }
          break;
      }
    }
  }
}

// Analyze the register usage within a block. Returns `true` if the set of live
// registers on entry to this fragment has changed since the last time we
// analyzed this fragment.
static bool AnalyzeFragRegs(Fragment *frag) {
  LiveRegisterTracker regs;
  if (auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH]) {
    regs = fall_through->regs.live_on_entry;
    if (auto branch = frag->successors[FRAG_SUCC_BRANCH]) {
      if (frag->branch_instr->IsConditionalJump()) {
        regs.Union(branch->regs.live_on_entry);
      } else {
        regs = branch->regs.live_on_entry;
      }
    }
  }
  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    regs.Visit(DynamicCast<NativeInstruction *>(instr));
  }
  auto changed = !frag->regs.live_on_entry.Equals(regs);
  frag->regs.live_on_entry = regs;
  return changed;
}

// Goes and finds all live regs on entry to a fragment.
static void AnalyzeFragRegs(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentIterator(frags)) {
      if (!IsA<ExitFragment *>(frag)) {
        changed = AnalyzeFragRegs(frag) || changed;
      }
    }
  }
}

#ifdef GRANARY_DEBUG
// Try to verify the sanity of the input fragment graph based on the
// prior step that injects partition and flag entry/exit fragments.
static void VerifyFragment(Fragment *frag) {
  if (IsA<CodeFragment *>(frag)) {
    return;
  }

  auto succ = frag->successors[0];
  GRANARY_ASSERT(nullptr == frag->successors[1]);
  if (IsA<ExitFragment *>(frag)) {
    GRANARY_ASSERT(nullptr == succ);
    return;
  }

  GRANARY_ASSERT(nullptr != succ);
  auto code_succ = DynamicCast<CodeFragment *>(succ);
  if (IsA<PartitionEntryFragment *>(frag)) {
    if (code_succ) {
      GRANARY_ASSERT(code_succ->attr.is_app_code);
    }
  } else if (IsA<FlagEntryFragment *>(frag)) {
    GRANARY_ASSERT(!IsA<FlagExitFragment *>(succ));

  } else if (IsA<FlagExitFragment *>(frag)) {
    if (!IsA<PartitionExitFragment *>(frag)) {
      GRANARY_ASSERT(code_succ);
      GRANARY_ASSERT(code_succ->attr.is_app_code);
    }
  }
}
#endif

// Identify the "flag zones" by making sure every fragment
static void IdentifyFlagZones(FragmentList *frags) {
  for (auto frag : FragmentIterator(frags)) {
    GRANARY_IF_DEBUG( VerifyFragment(frag); )
    if (IsA<CodeFragment *>(frag) || IsA<FlagEntryFragment *>(frag)) {
      for (auto succ : frag->successors) {
        if (succ && !IsA<PartitionExitFragment *>(succ)) {
          frag->flag_zone.Union(frag, succ);
        }
      }
    }
  }
}

// Allocate flag zone structures for each distinct flag zone.
static void AllocateFlagZones(FragmentList *frags,
                              LocalControlFlowGraph *cfg) {
  for (auto frag : FragmentIterator(frags)) {
    if (IsA<FlagEntryFragment *>(frag)) {
      auto &flag_zone(frag->flag_zone.Value());
      if (!flag_zone) {
        flag_zone = new FlagZone(
            cfg->AllocateVirtualRegister(arch::GPR_WIDTH_BYTES), FlagKillReg());
      }
    }
  }
#ifdef GRANARY_DEBUG
  // Quick and easy verification of the flag zones.
  for (auto frag : FragmentIterator(frags)) {
    if (IsA<FlagExitFragment *>(frag)) {
      GRANARY_ASSERT(nullptr != frag->flag_zone.Value());
    }
  }
#endif  // GRANARY_DEBUG
}

// Update the flag zones with the flags and registers used in the various
// fragments that belong to this flag zone, as well as the flags used *after*
// the flag zone.
static void UpdateFlagZones(FragmentList *frags) {
  for (auto frag : FragmentIterator(frags)) {
    auto &flag_zone(frag->flag_zone.Value());
    if (flag_zone) {
      if (auto code = DynamicCast<CodeFragment *>(frag)) {
        flag_zone->killed_flags |= code->flags.all_written_flags;
        for (auto succ : frag->successors) {
          if (IsA<FlagExitFragment *>(succ)) {
            flag_zone->live_flags |= code->flags.exit_live_flags;
          }
        }
      } else if (auto flag_exit = DynamicCast<FlagExitFragment *>(frag)) {
        flag_zone->live_regs.Union(flag_exit->regs.live_on_entry);
      }
    }
  }
}

// Injects architecture-specific code that saves and restores the flags within
// flag entry and exit fragments.
static void SaveAndRestoreFlags(FragmentList *frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (auto flag_entry = DynamicCast<FlagEntryFragment *>(frag)) {
      InjectSaveFlags(flag_entry);
    } else if (auto flag_exit = DynamicCast<FlagExitFragment *>(frag)) {
      InjectRestoreFlags(flag_exit);
    }
  }
}

// Frees all flag zone data structures.
static void FreeFlagZones(FragmentList *frags) {
  for (auto frag : FragmentIterator(frags)) {
    auto &flag_zone(frag->flag_zone.Value());
    if (flag_zone) {
      delete flag_zone;
      flag_zone = nullptr;
    }
  }
}

}  // namespace

// Insert flags saving code into `FRAG_TYPE_FLAG_ENTRY` fragments, and flag
// restoring code into `FRAG_TYPE_FLAG_EXIT` code. We only insert code to save
// and restore flags if it is necessary.
void SaveAndRestoreFlags(LocalControlFlowGraph *cfg, FragmentList *frags) {
  InitLiveRegsOnExit(frags);
  AnalyzeFragRegs(frags);
  IdentifyFlagZones(frags);
  AllocateFlagZones(frags, cfg);
  UpdateFlagZones(frags);
  SaveAndRestoreFlags(frags);
  FreeFlagZones(frags);
}

}  // namespace granary
