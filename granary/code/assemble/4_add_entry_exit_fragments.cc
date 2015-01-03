/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/assemble/4_add_entry_exit_fragments.h"

#include "granary/util.h"

namespace granary {
namespace arch {

// Visits an instructions within the fragment and revives/kills architecture-
// specific flags stored in the `FlagUsageInfo` object.
//
// Note: This has an architecture-specific implementation.
extern void VisitInstructionFlags(const arch::Instruction &instr,
                                  FlagUsageInfo *flags);

// Returns a bitmap representing all arithmetic flags being live.
//
// Note: This has an architecture-specific implementation.
extern uint32_t AllArithmeticFlags(void);

}  // namespace arch
namespace {

// Converts instrumentation fragments into application fragments where the
// flags usage permits.
static void PropagateFragKinds(FragmentList *frags) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (kFragmentKindInvalid != code_frag->kind) continue;

      // Try to copy it from any successor.
      auto has_code_succ = false;
      for (auto succ : frag->successors) {
        if (auto code_succ = DynamicCast<CodeFragment *>(succ)) {
          code_frag->kind = code_succ->kind;
          has_code_succ = true;
        }
      }

      if (kFragmentKindInvalid == code_frag->kind) {
        // Motivation: The previous fragment (if any) might be an
        //             instrumentation fragment, so make this an
        //             instrumentation fragment.
        if (has_code_succ) {
          code_frag->kind = kFragmentKindInst;

        // Motivation: The only other option is that the next fragment is an
        //             `ExitFragment`, so make this fragment into an
        //             application fragment.
        } else {
          code_frag->kind = kFragmentKindApp;
        }
      }
    } else if (kFragmentKindInvalid == frag->kind) {
      frag->kind = kFragmentKindApp;
    }
  }
}

// Used to track live instrumentation and application flags within a fragment.
struct LiveFlags {
  LiveFlags(void)
      : app_flags(0),
        inst_flags(0) {}

  LiveFlags(uint32_t a, uint32_t i)
      : app_flags(a),
        inst_flags(i) {}

  uint32_t app_flags;
  uint32_t inst_flags;
};

static LiveFlags LiveFlagsOnEntry(Fragment *frag) {
  if (IsA<ExitFragment *>(frag)) {
    return LiveFlags(arch::AllArithmeticFlags(), 0);
  } else if (frag) {
    return LiveFlags(frag->app_flags.entry_live_flags,
                     frag->inst_flags.entry_live_flags);
  } else {
    return LiveFlags();
  }
}

static LiveFlags LiveFlagsOnExit(Fragment *frag) {
  if (IsA<ExitFragment *>(frag)) {
    return LiveFlags(arch::AllArithmeticFlags(), 0);
  } else if (frag->branch_instr) {
    if (!frag->branch_instr->IsConditionalJump()) {
      return LiveFlagsOnEntry(frag->successors[kFragSuccBranch]);
    }
  }

  auto fall_live = LiveFlagsOnEntry(frag->successors[kFragSuccFallThrough]);
  auto branch_live = LiveFlagsOnEntry(frag->successors[kFragSuccBranch]);
  return {
    fall_live.app_flags | branch_live.app_flags,
    fall_live.inst_flags | branch_live.inst_flags
  };
}

static void InitFlagsUse(FragmentList *frags) {
  auto all_flags = arch::AllArithmeticFlags();
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (auto exit_frag = DynamicCast<ExitFragment *>(frag)) {
      auto &flags(exit_frag->app_flags);
      flags.all_read_flags = all_flags;
      flags.all_written_flags = all_flags;
      flags.entry_live_flags = all_flags;
      flags.exit_live_flags = all_flags;
    }
  }
}

// Returns `true` if any updates were made to what flags are live on entry/exit,
// and what flags are read/written anywhere within the fragment.
static bool TryUpdateFlagsUse(Fragment *frag) {
  GRANARY_ASSERT(kFragmentKindInvalid != frag->kind);

  FlagUsageInfo *flags(nullptr);
  FlagUsageInfo new_flags;
  auto exit_flags = LiveFlagsOnExit(frag);

  if (kFragmentKindApp == frag->kind) {
    flags = &(frag->app_flags);
    new_flags = *flags;
    new_flags.exit_live_flags = exit_flags.app_flags;

  } else {
    flags = &(frag->inst_flags);
    new_flags = *flags;
    new_flags.exit_live_flags = exit_flags.inst_flags;

    // Propagate application flags through instrumentation fragments.
    frag->app_flags.exit_live_flags |= exit_flags.app_flags;
    frag->app_flags.entry_live_flags |= exit_flags.app_flags;
  }

  new_flags.entry_live_flags = new_flags.exit_live_flags;

  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      arch::VisitInstructionFlags(ninstr->instruction, &new_flags);
    }
  }

  auto ret = 0 != memcmp(flags, &new_flags, sizeof new_flags);
  *flags = new_flags;
  return ret;
}

// Analyze the flags use of the fragments within the fragment control-flow
// graph. This performs a backward data-flow pass.
//
// The key things that are collected are:
//      1)  Live app flags on entry/exit to a fragment. These are collected for
//          both app and inst fragments, so that we can know what flags need
//          to be saved/restored within a instrumentation code flag region.
//      2)  Live inst flags on entry/exit to an instrumented fragment. This
//          isn't strictly needed, but we compute it because we use the same
//          method to compute inst and app live flags.
//      3)  Flags that are read and/or written *anywhere* within a fragment,
//          regardless of if they are live on entry/exit. This is so that we
//          know all flags that are potentially modified by instructions so
//          that we can know exactly which app flags inst fragments clobber.
static void AnalyzeFlagsUse(FragmentList *frags) {
  InitFlagsUse(frags);
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      changed = TryUpdateFlagsUse(frag) || changed;
    }
  }
}

// Group fragments together into flag zones.
static void CombineFlagZones(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto code_frag = DynamicCast<CodeFragment *>(frag);
    if (!code_frag) continue;

    for (auto succ : frag->successors) {
      auto code_succ = DynamicCast<CodeFragment *>(succ);
      if (!code_succ) continue;
      if (frag->partition != succ->partition) continue;

      // Try to convert app frags into inst frags.
      if (code_frag->kind != code_succ->kind) {
        if (kFragmentKindApp != code_succ->kind) continue;
        if (code_succ->branch_instr) continue;
        if (code_succ->app_flags.all_read_flags) continue;
        if (code_succ->app_flags.all_written_flags) continue;
        code_succ->kind = kFragmentKindInst;
      }
      code_frag->flag_zone.Union(code_succ->flag_zone);
    }
  }
}

// Update the flag zones with the flags and registers used in the various
// fragments that belong to this flag zone, as well as the flags used *after*
// the flag zone.
static void UpdateFlagZones(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (kFragmentKindInst != frag->kind) continue;
    auto &flag_zone(frag->flag_zone.Value());
#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
    if (auto code = DynamicCast<CodeFragment *>(frag)) {
      GRANARY_ASSERT(code->attr.modifies_flags ==
                     !!frag->inst_flags.all_written_flags);
    }
#endif  // GRANARY_TARGET_debug
    flag_zone.killed_flags |= frag->inst_flags.all_written_flags;
    flag_zone.live_flags |= frag->app_flags.exit_live_flags;
  }
}

// Returns `true` if `frag` is an instrumented fragment that belongs to a
// flag zone where at least one of the flags is killed.
static bool IsFlagInstCode(Fragment *frag) {
  if (kFragmentKindInst != frag->kind) return false;
  auto zone = frag->flag_zone.Value();
  return zone.killed_flags;
}

// Returns true if the transition between `curr` and `next` represents a flags
// entry point.
static bool IsFlagEntry(Fragment *curr, Fragment *next) {
  if (IsA<FlagEntryFragment *>(curr)) return false;
  if (IsA<FlagEntryFragment *>(next)) return false;
  if (curr->partition != next->partition) return false;
  if (curr->flag_zone == next->flag_zone) return false;
  return IsFlagInstCode(next);
}

// Returns true if the transition between `curr` and `next` represents a flags
// exit point.
static bool IsFlagExit(Fragment *curr, Fragment *next) {
  if (IsA<FlagExitFragment *>(curr)) return false;
  if (IsA<FlagExitFragment *>(next)) return false;
  if (curr->partition != next->partition) return false;
  if (curr->flag_zone == next->flag_zone) return false;
  return IsFlagInstCode(curr);
}

// Returns true if the transition between `curr` and `next` represents a
// partition entry point.
//
// Note: We only need to consider that curr/next are one of:
//            1)  CodeFragment
//            2)  ExitFragment
//            3)  PartitionEntryFragment
//            4)  NonLocalEntryFragment.
static bool IsPartitionEntry(Fragment * const curr, Fragment * const next) {
  if (IsA<PartitionEntryFragment *>(curr)) return false;
  if (IsA<PartitionEntryFragment *>(next)) return false;
  if (IsA<ExitFragment *>(next)) return false;

  // Special case.
  if (IsA<NonLocalEntryFragment *>(curr)) {
    return !IsA<ControlFlowInstruction *>(next->branch_instr);
  }

  GRANARY_ASSERT(IsA<CodeFragment *>(curr) && IsA<CodeFragment *>(next));

  if (IsA<ControlFlowInstruction *>(next->branch_instr)) {
    return false;
  }

  return curr->partition != next->partition;
}

// Returns true if the transition between `curr` and `next` represents a
// partition exit point.
//
// Note: We only need to consider that curr/next are one of:
//            1)  CodeFragment
//            2)  ExitFragment
//            3)  PartitionEntryFragment
//            4)  PartitionExitFragment
static bool IsPartitionExit(Fragment * const curr, Fragment * const next) {
  if (IsA<PartitionExitFragment *>(curr)) return false;
  if (IsA<PartitionExitFragment *>(next)) return false;
  if (IsA<PartitionEntryFragment *>(curr)) return false;
  if (IsA<ControlFlowInstruction *>(curr->branch_instr)) return false;
  return curr->partition != next->partition;
}

// Reset the pass-specific "back link" pointer that is used to re-use entry and
// exit fragments.
static void ResetEntryExitFrag(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    frag->entry_exit_frag = nullptr;
  }
}

// Conditionally add an exit fragment, and try to be slightly smart about not
// making redundant fragments (e.g. redundant entry/exit fragments).
static void AddExitFragment(FragmentList *frags,
                            Fragment *curr, Fragment **succ_ptr,
                            bool (*is_end)(Fragment *, Fragment *),
                            Fragment *(*make_frag)(Fragment *, Fragment *)) {
  auto succ = *succ_ptr;
  if (succ && is_end(curr, succ)) {
    // Try to merge some of the exit fragments using the `entry_exit_frag`
    // pointer in fragment. This will allow us to generate slightly tighter
    // code.
    auto &back_link(succ->entry_exit_frag);
    if (back_link && curr->partition == back_link->partition) {
      *succ_ptr = back_link;
    } else {
      auto exit_frag = make_frag(curr, succ);
      frags->InsertAfter(curr, exit_frag);
      *succ_ptr = exit_frag;
      back_link = exit_frag;
    }
  }
}

// Add in all of the end fragments of a particular kind.
static void AddExitFragments(FragmentList * const frags,
                             bool (*is_end)(Fragment *, Fragment *),
                             Fragment *(*make_frag)(Fragment *, Fragment *)) {
  ResetEntryExitFrag(frags);
  for (auto frag : FragmentListIterator(frags)) {
    for (auto &succ : frag->successors) {
      if (succ) {
        AddExitFragment(frags, frag, &succ, is_end, make_frag);
      }
    }
  }
}

// Conditionally add an entry fragment, and try to be slightly smart about not
// making redundant fragments (e.g. redundant entry/exit fragments).
static void AddEntryFragment(FragmentList *frags,
                             Fragment *curr, Fragment **succ_ptr,
                             bool (*is_end)(Fragment *, Fragment *),
                             Fragment *(*make_frag)(Fragment *, Fragment *)) {
  auto succ = *succ_ptr;
  if (succ && is_end(curr, succ)) {
    // Try to merge some of the entry fragments using the `entry_exit_frag`
    // pointer in fragment. This will allow us to generate slightly tighter
    // code.
    auto &back_link(succ->entry_exit_frag);
    if (back_link && succ->partition == back_link->partition) {
      *succ_ptr = back_link;
    } else {
      auto entry_frag = make_frag(succ, succ);
      frags->InsertAfter(curr, entry_frag);
      *succ_ptr = entry_frag;
      back_link = entry_frag;
    }
  }
}

// Make a specific type of fragment with a single successor.
template <typename T>
static Fragment *MakeFragment(Fragment *inherit, Fragment *succ) {
  Fragment *frag = new T;

  frag->block_meta = inherit->block_meta;
  frag->kind = kFragmentKindInst;

  // TODO(pag): Should `cache` inheritance be special, as with compensation
  //            fragments?
  frag->cache = inherit->cache;

  frag->stack_status = inherit->stack_status;
  frag->successors[kFragSuccFallThrough] = succ;
  frag->partition.Union(inherit->partition);
  if (!TypesAreEqual<T,PartitionEntryFragment>() &&
      !TypesAreEqual<T,PartitionExitFragment>()) {
    frag->flag_zone.Union(inherit->flag_zone);
  }

  // Propagate flags usage info.
  frag->app_flags.exit_live_flags = succ->app_flags.entry_live_flags;
  frag->app_flags.entry_live_flags = frag->app_flags.exit_live_flags;

  // Propagate flags usage info.
  frag->app_flags.exit_live_flags = succ->app_flags.entry_live_flags;
  frag->app_flags.entry_live_flags = frag->app_flags.exit_live_flags;

  return frag;
}

// Add in all of the entry fragments of a particular kind.
static void AddEntryFragments(FragmentList * const frags,
                              bool (*is_end)(Fragment *, Fragment *),
                              Fragment *(*make_frag)(Fragment *, Fragment *)) {
  for (auto frag : FragmentListIterator(frags)) {
    for (auto &succ : frag->successors) {
      if (succ) {
        AddEntryFragment(frags, frag, &succ, is_end, make_frag);
      }
    }
  }
}

// Label the N fragment partitions with IDs 1 through N.
static void LabelPartitions(FragmentList *frags) {
  auto next_id = 0;
  for (auto frag : FragmentListIterator(frags)) {
    auto &partition(frag->partition.Value());
    if (!partition) {
      partition = new PartitionInfo(++next_id);
    }
    for (auto succ : frag->successors) {
      if (succ) succ->num_predecessors++;
    }
  }
}

// Propagate code cache kinds through the fragment graph.
static void PropagateCodeCacheKinds(FragmentList *frags) {
  for (const auto frag : FragmentListIterator(frags)) {
    for (auto succ : frag->successors) {
      if (succ && 1 >= succ->num_predecessors &&
          frag->cache == succ->cache) {
        succ->cache = frag->cache;
      }
    }
  }
}

}  // namespace

// Adds designated entry and exit fragments around fragment partitions and
// around groups of instrumentation code fragments. First we add entry/exits
// around instrumentation code fragments for saving/restoring flags, then we
// add entry/exits around the partitions for saving/restoring registers.
void AddEntryAndExitFragments(FragmentList *frags) {
  PropagateFragKinds(frags);
  AnalyzeFlagsUse(frags);
  CombineFlagZones(frags);
  UpdateFlagZones(frags);

  // Guarantee that there is a partition entry fragment. The one special case
  // against this entry fragment is that the first instruction of the first
  // fragment is a function call / return / something else that can't be
  // added to a partition.
  ResetEntryExitFrag(frags);
  auto first_frag = frags->First();
  if (!IsA<ControlFlowInstruction *>(first_frag->branch_instr)) {
    auto first_entry = MakeFragment<PartitionEntryFragment>(first_frag,
                                                            first_frag);
    frags->Prepend(first_entry);
    first_frag->entry_exit_frag = first_entry;
  }

  AddEntryFragments(frags, IsPartitionEntry,
                    MakeFragment<PartitionEntryFragment>);

  AddExitFragments(frags, IsPartitionExit,
                   MakeFragment<PartitionExitFragment>);

  ResetEntryExitFrag(frags);
  AddEntryFragments(frags, IsFlagEntry, MakeFragment<FlagEntryFragment>);
  ResetEntryExitFrag(frags);
  AddExitFragments(frags, IsFlagExit, MakeFragment<FlagExitFragment>);
  LabelPartitions(frags);
  PropagateCodeCacheKinds(frags);
}

}  // namespace granary
