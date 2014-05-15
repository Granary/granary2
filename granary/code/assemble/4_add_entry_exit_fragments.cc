/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"

#include "granary/code/assemble/4_add_entry_exit_fragments.h"

#include "granary/util.h"

namespace granary {

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

namespace {

// Counts the number of instrumented predecessors.
static void CountInstrumentedPredecessors(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto code = DynamicCast<CodeFragment *>(frag)) {
      if (!code->attr.is_app_code) {
        for (auto succ : code->successors) {
          if (auto code_succ = DynamicCast<CodeFragment *>(succ)) {
            code_succ->attr.num_inst_preds++;
          }
        }
      }
    }
  }
}

// Returns the live flags on exit from a fragment.
static uint32_t LiveFlagsOnExit(Fragment *frag) {
  if (IsA<ExitFragment *>(frag)) {
    return AllArithmeticFlags();
  }
  auto exit_live_flags = 0U;
  for (auto succ : frag->successors) {
    if (IsA<ExitFragment *>(succ)) {
      exit_live_flags = AllArithmeticFlags();
    } else if (auto succ_code = DynamicCast<CodeFragment *>(succ)) {
      exit_live_flags |= succ_code->flags.entry_live_flags;
    }
  }
  return exit_live_flags;
}

// Analyzes and updates the flag use for a fragment. If the fragment's flag
// use was changed then this returns true.
static bool AnalyzeFlagUse(Fragment *frag) {
  auto exit_live_flags = LiveFlagsOnExit(frag);
  FlagUsageInfo flags;
  flags.all_written_flags = 0U;
  flags.exit_live_flags = exit_live_flags;
  flags.entry_live_flags = exit_live_flags;

  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      VisitInstructionFlags(ninstr->instruction, &flags);
    }
  }
  if (flags.entry_live_flags != frag->flags.entry_live_flags ||
      flags.exit_live_flags != frag->flags.exit_live_flags) {
    frag->flags = flags;
    return true;
  } else {
    return false;
  }
}

// Analyzes and updates the flags usage for all fragments.
static void AnalyzeFlagsUse(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      changed = AnalyzeFlagUse(frag) || changed;
    }
  }
}

// Heuristic for telling us if we should try to convert an instrumented fragment
// into an application fragment.
static bool SuccessorMakesFragConvertible(CodeFragment *frag) {
  for (auto succ : frag->successors) {
    if (succ) {
      if (auto code_succ = DynamicCast<CodeFragment *>(succ)) {
        if (!code_succ->attr.is_app_code) return false;
        if (1 < code_succ->attr.num_inst_preds) return false;  // Heuristic.
      } else if (!IsA<ExitFragment *>(succ)) {
        return false;
      }
    }
  }
  return true;
}

// Try to convert an instrumentation fragment into a code fragment based on
// the flag use.
static bool ConvertToAppFrag(CodeFragment *frag) {
  auto live_flags_exit = LiveFlagsOnExit(frag);
  if (!(frag->flags.all_written_flags & live_flags_exit)) {
    frag->attr.is_app_code = SuccessorMakesFragConvertible(frag);
  }
  return frag->attr.is_app_code;
}

// Converts instrumentation fragments into application fragments where the
// flags usage permits.
static void ConvertToAppFrags(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
        if (!code_frag->attr.is_app_code) {
          changed = ConvertToAppFrag(code_frag) || changed;
        }
      }
    }
  }
}

// Reset the pass-specific "back link" pointer that is used to re-use entry and
// exit fragments.
static void ResetTempData(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    frag->temp.entry_exit_frag = nullptr;
  }
}

// Returns true if the transition between `curr` and `next` represents a flags
// entry point.
//
// Flag entry points can only occur between two `CodeFragment`s, where we
// transition between partitions, or where we transition from application code
// to instrumentation code.
static bool IsFlagEntry(Fragment *curr, Fragment *next) {
  auto code_next = DynamicCast<CodeFragment *>(next);
  if (IsA<PartitionEntryFragment *>(curr)) {
    return code_next && !code_next->attr.is_app_code;
  } else if (auto code_curr = DynamicCast<CodeFragment *>(curr)) {
    return code_curr->attr.is_app_code && code_next &&
           !code_next->attr.is_app_code;
  }
  return false;
}

// Returns true if the transition between `curr` and `next` represents a flags
// exit point.
//
// A flags exit point occurs between an instrumentation code fragment and a
// partition change, or between an instrumentation code fragment and an
// application code fragment (or a flag entry, which will appear as non-equal
// partitions).
static bool IsFlagExit(Fragment *curr, Fragment *next) {
  if (auto code_curr = DynamicCast<CodeFragment *>(curr)) {
    if (code_curr->attr.is_app_code) return false;
    if (IsA<PartitionEntryFragment *>(next)) return false;
    if (IsA<PartitionExitFragment *>(next)) return true;
    if (IsA<ExitFragment *>(next)) return true;
    auto code_next = DynamicCast<CodeFragment *>(next);
    GRANARY_ASSERT(nullptr != code_next);
    return code_next->attr.is_app_code;
  }
  return false;
}

// Returns true if the transition between `curr` and `next` represents a
// partition entry point.
static bool IsPartitionEntry(Fragment *curr, Fragment *next) {
  if (curr->partition == next->partition) return false;
  if (IsA<PartitionEntryFragment *>(curr)) return false;
  if (IsA<PartitionEntryFragment *>(next)) return false;
  if (IsA<ExitFragment *>(next)) return false;
  if (auto next_code = DynamicCast<CodeFragment *>(next)) {
    if (!next_code->attr.can_add_to_partition) return false;
  }
  return true;
}

// Returns true if the transition between `curr` and `next` represents a
// partition exit point.
static bool IsPartitionExit(Fragment *curr, Fragment *next) {
  if (curr->partition == next->partition) return false;
  if (IsA<PartitionExitFragment *>(curr)) return false;
  if (IsA<PartitionExitFragment *>(next)) return false;
  if (auto curr_code = DynamicCast<CodeFragment *>(curr)) {
    if (!curr_code->attr.can_add_to_partition) return false;
  }
  if (IsA<ExitFragment *>(next)) return true;
  if (IsA<PartitionEntryFragment *>(next)) {
    if (auto curr_code = DynamicCast<CodeFragment *>(curr)) {
      if (curr_code->attr.branches_to_edge_code) {
        return false;  // This is a fall-through.
      }
    }
    return true;
  }
  if (auto next_code = DynamicCast<CodeFragment *>(next)) {
    if (!next_code->attr.can_add_to_partition) return true;
  }
  return false;
}

// Conditionally add an exit fragment, and try to be slightly smart about not
// making redundant fragments (e.g. redundant entry/exit fragments).
static void AddExitFragment(FragmentList *frags,
                            Fragment *curr, Fragment **succ_ptr,
                            bool (*is_end)(Fragment *, Fragment *),
                            Fragment *(*make_frag)(Fragment *, Fragment *)) {
  auto succ = *succ_ptr;
  if (succ && is_end(curr, succ)) {
    // Try to merge some of the exit fragments using the `transient_back_link`
    // pointer in fragment. This will allow us to generate slightly tighter
    // code.
    auto &back_link(succ->temp.entry_exit_frag);
    if (back_link && curr->partition == back_link->partition) {
      *succ_ptr = back_link;
    } else {
      auto exit_frag = make_frag(curr, succ);
      frags->InsertAfter(curr, exit_frag);
      *succ_ptr = exit_frag;
      back_link = exit_frag;

      // Propagate flags usage info.
      exit_frag->flags.exit_live_flags = succ->flags.entry_live_flags;
      exit_frag->flags.entry_live_flags = exit_frag->flags.exit_live_flags;
    }
  }
}

// Add in all of the end fragments of a particular kind.
static void AddExitFragments(FragmentList * const frags,
                             bool (*is_end)(Fragment *, Fragment *),
                             Fragment *(*make_frag)(Fragment *, Fragment *)) {
  ResetTempData(frags);
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
    // Try to merge some of the entry fragments using the `transient_back_link`
    // pointer in fragment. This will allow us to generate slightly tighter
    // code.
    auto &back_link(succ->temp.entry_exit_frag);
    if (back_link && succ->partition == back_link->partition) {
      *succ_ptr = back_link;
    } else {
      auto entry_frag = make_frag(succ, succ);
      frags->InsertAfter(curr, entry_frag);
      *succ_ptr = entry_frag;
      back_link = entry_frag;

      // Propagate flags usage info.
      entry_frag->flags.exit_live_flags = succ->flags.entry_live_flags;
      entry_frag->flags.entry_live_flags = entry_frag->flags.exit_live_flags;
    }
  }
}

// Make a specific type of fragment with a single successor.
template <typename T>
static Fragment *MakeFragment(Fragment *inherit, Fragment *succ) {
  Fragment *frag = new T;
  frag->successors[0] = succ;
  frag->partition.Union(frag, inherit);
  return frag;
}

// Add in all of the entry fragments of a particular kind.
static void AddEntryFragments(FragmentList * const frags,
                              bool (*is_end)(Fragment *, Fragment *),
                              Fragment *(*make_frag)(Fragment *, Fragment *)) {
  ResetTempData(frags);
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
  }
}

}  // namespace

// Adds designated entry and exit fragments around fragment partitions and
// around groups of instrumentation code fragments. First we add entry/exits
// around instrumentation code fragments for saving/restoring flags, then we
// add entry/exits around the partitions for saving/restoring registers.
void AddEntryAndExitFragments(FragmentList *frags) {
  AnalyzeFlagsUse(frags);

  AddEntryFragments(frags, IsPartitionEntry,
                    MakeFragment<PartitionEntryFragment>);

  // Guarantee that there is a partition entry fragment.
  auto first_frag = frags->First();
  frags->Prepend(MakeFragment<PartitionEntryFragment>(first_frag, first_frag));

  AddExitFragments(frags, IsPartitionExit,
                   MakeFragment<PartitionExitFragment>);

  // Do some flags analysis to figure out if we can convert instrumentation
  // fragments to application fragments. The benefit of conversion is that we
  // will ideally have fewer flag save/restore zones, and so later will have
  // to inject fewer flag saving/restoring instructions.
  CountInstrumentedPredecessors(frags);
  ConvertToAppFrags(frags);

  AddEntryFragments(frags, IsFlagEntry, MakeFragment<FlagEntryFragment>);

  AddExitFragments(frags, IsFlagExit, MakeFragment<FlagExitFragment>);

  LabelPartitions(frags);
}

}  // namespace granary
