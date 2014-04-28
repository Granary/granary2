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
void VisitInstructionFlags(const arch::Instruction &instr,
                           FlagUsageInfo *flags);

namespace {

// Returns the live flags on exit from a fragment.
static uint32_t LiveFlagsOnExit(CodeFragment *frag) {
  auto exit_live_flags = 0U;
  for (auto succ : frag->successors) {
    if (IsA<ExitFragment *>(frag)) {
      exit_live_flags = ~0U;
    } else if (auto succ_code = DynamicCast<CodeFragment *>(succ)) {
      exit_live_flags |= succ_code->flags.entry_live_flags;
    }
  }
  return exit_live_flags;
}

// Analyzes and updates the flag use for a fragment. If the fragment's flag
// use was changed then this returns true.
static bool AnalyzeFlagUse(CodeFragment *frag) {
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

  const auto changed = flags.entry_live_flags != frag->flags.entry_live_flags;
  frag->flags = flags;
  return changed;
}

// Analyzes and updates the flags usage for all fragments.
static void AnalyzeFlagsUse(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentIterator(frags)) {
      if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
        changed = AnalyzeFlagUse(code_frag) || changed;
      }
    }
  }
}

// Try to convert an instrumentation fragment into a code fragment based on
// the flag use.
static bool ConvertToAppFrag(CodeFragment *frag) {
  auto live_flags_exit = LiveFlagsOnExit(frag);
  if (!(frag->flags.all_written_flags & live_flags_exit)) {
    frag->attr.is_app_code = true;
    return true;
  }
  return false;
}

// Converts instrumentation fragments into application fragments where the
// flags usage permits.
static void ConvertToAppFrags(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentIterator(frags)) {
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
  for (auto frag : FragmentIterator(frags)) {
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
  if (auto curr_code = DynamicCast<CodeFragment *>(curr)) {
    if (auto next_code = DynamicCast<CodeFragment *>(next)) {
      return !next_code->attr.is_app_code &&
             !curr_code->stack.has_stack_changing_cfi &&
             (curr->partition != next->partition ||
              curr_code->attr.is_app_code);
    }
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
  if (auto curr_code = DynamicCast<CodeFragment *>(curr)) {
    if (curr_code->stack.has_stack_changing_cfi) return false;
    if (curr_code->attr.is_app_code) return false;
    if (curr->partition != next->partition) return true;
    if (auto next_code = DynamicCast<CodeFragment *>(next)) {
      return next_code->attr.is_app_code;
    }
    GRANARY_ASSERT(!IsA<FlagEntryFragment *>(next));
    GRANARY_ASSERT(!IsA<FlagExitFragment *>(next));
  }
  return false;
}

// Returns true if the transition between `curr` and `next` represents a
// partition entry point.
static bool IsPartitionEntry(Fragment *curr, Fragment *next) {
  auto next_code = DynamicCast<CodeFragment *>(next);
  return curr->partition != next->partition &&
         !IsA<ExitFragment *>(next) &&
         (!next_code || !next_code->stack.has_stack_changing_cfi);
}

// Returns true if the transition between `curr` and `next` represents a
// partition exit point.
static bool IsPartitionExit(Fragment *curr, Fragment *next) {
  auto curr_code = DynamicCast<CodeFragment *>(curr);
  return curr->partition != next->partition &&
         !IsA<PartitionExitFragment *>(curr) &&
         !IsA<PartitionExitFragment *>(next) &&
         (!curr_code || !curr_code->stack.has_stack_changing_cfi);
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
    }
  }
}

// Add in all of the end fragments of a particular kind.
static void AddExitFragments(FragmentList * const frags,
                             bool (*is_end)(Fragment *, Fragment *),
                             Fragment *(*make_frag)(Fragment *, Fragment *)) {
  ResetTempData(frags);
  for (auto frag : FragmentIterator(frags)) {
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
  for (auto frag : FragmentIterator(frags)) {
    for (auto &succ : frag->successors) {
      if (succ) {
        AddEntryFragment(frags, frag, &succ, is_end, make_frag);
      }
    }
  }
}

// Label the N fragment partitions with IDs 1 through N.
static void LabelPartitions(FragmentList *frags) {
  auto next_id = 1;
  for (auto frag : FragmentIterator(frags)) {
    auto &partition(frag->partition.Value());
    if (!partition.id) {
      partition.id = next_id++;
    }
  }
}

}  // namespace

// Adds designated entry and exit fragments around fragment partitions and
// around groups of instrumentation code fragments. First we add entry/exits
// around instrumentation code fragments for saving/restoring flags, then we
// add entry/exits around the partitions for saving/restoring registers.
void AddEntryAndExitFragments(FragmentList *frags) {

  // Do some flags analysis to figure out if we can convert instrumentation
  // fragments to application fragments. The benefit of conversion is that we
  // will ideally have fewer flag save/restore zones, and so later will have
  // to inject fewer flag saving/restoring instructions.
  AnalyzeFlagsUse(frags);
  ConvertToAppFrags(frags);

  AddExitFragments(frags, IsFlagExit, MakeFragment<FlagExitFragment>);

  auto code_first = DynamicCast<CodeFragment *>(frags->First());
  GRANARY_ASSERT(nullptr != code_first);

  // Guarantee that there is a flag entry fragment for the first fragment.
  if (!code_first->attr.is_app_code) {
    frags->Prepend(MakeFragment<FlagEntryFragment>(code_first, code_first));
  }

  AddEntryFragments(frags, IsFlagEntry, MakeFragment<FlagEntryFragment>);

  AddEntryFragments(frags, IsPartitionEntry,
                    MakeFragment<PartitionEntryFragment>);

  // Guarantee that there is a partition entry fragment.
  auto first_frag = frags->First();
  frags->Prepend(MakeFragment<PartitionEntryFragment>(first_frag, first_frag));

  AddExitFragments(frags, IsPartitionExit,
                   MakeFragment<PartitionExitFragment>);
  LabelPartitions(frags);
}

}  // namespace granary
