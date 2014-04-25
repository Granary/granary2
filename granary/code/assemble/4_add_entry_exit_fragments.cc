/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"

#include "granary/code/assemble/4_add_entry_exit_fragments.h"

#include "granary/util.h"

namespace granary {

#if 0
namespace {

// Make an entry/exit fragment of a specific kind and partially chain it into
// the control-flow and the fragments list.
static Fragment *MakeFragment(FragmentKind kind, Fragment *inherit,
                              Fragment *fall_through, Fragment *next) {
  auto frag = new Fragment(-1);
  auto label = new LabelInstruction;
  frag->AppendInstruction(std::unique_ptr<Instruction>(label));
  frag->kind = kind;
  frag->partition_id = inherit->partition_id;
  frag->block_meta = inherit->block_meta;
  frag->is_decoded_block_head = inherit->is_decoded_block_head;
  frag->fall_through_target = fall_through;
  frag->next = next;
  SetMetaData(label, frag);

  if (FRAG_KIND_PARTITION_ENTRY == kind || FRAG_KIND_PARTITION_EXIT == kind) {
    frag->partition_sentinel = frag;
  }
  return frag;
}

// Initialize the problem by adding the partition and flag entry fragments
// for the first basic block.
static void InitEntryFragments(Fragment **frags_ptr) {
  auto first = *frags_ptr;
  auto first_part = MakeFragment(FRAG_KIND_PARTITION_ENTRY,
                                 first, first, first);
  *frags_ptr = first_part;

  if (FRAG_KIND_INSTRUMENTATION == first->kind) {
    auto first_flag = MakeFragment(FRAG_KIND_FLAG_ENTRY, first, first, first);
    first_part->next = first_part->fall_through_target = first_flag;
  }
}

// Conditionally add an exit fragment, and try to be slightly smart about not
// making redundant fragments (e.g. redundant entry/exit fragments).
static void AddExitFragment(Fragment *curr, Fragment **next_ptr,
                            bool (*is_end)(Fragment *, Fragment *),
                            FragmentKind exit_kind) {
  auto next = *next_ptr;
  if (next && is_end(curr, next)) {
    // Try to merge some of the exit fragments using the `transient_back_link`
    // pointer in fragment. This will allow us to generate slightly tighter
    // code.
    auto back_link = next->cached_back_link;
    if (back_link && curr->partition_id == back_link->partition_id) {
      *next_ptr = back_link;
    } else {
      auto exit_frag = MakeFragment(exit_kind, curr, next, curr->next);
      curr->next = exit_frag;
      *next_ptr = exit_frag;
      next->cached_back_link = exit_frag;
    }
  }
}

// Add in all of the end fragments of a particular kind.
static void AddExitFragments(Fragment * const frags,
                             bool (*is_end)(Fragment *, Fragment *),
                             FragmentKind exit_kind) {
  for (auto frag : FragmentIterator(frags)) {
    frag->cached_back_link = nullptr;
  }
  for (auto frag : FragmentIterator(frags)) {
    if (frag->kind != exit_kind) {
      AddExitFragment(frag, &(frag->branch_target), is_end, exit_kind);
      AddExitFragment(frag, &(frag->fall_through_target), is_end, exit_kind);
    }
  }
}


// Conditionally add an entry fragment, and try to be slightly smart about not
// making redundant fragments (e.g. redundant entry/exit fragments).
static void AddEntryFragment(Fragment *curr, Fragment **next_ptr,
                             bool (*is_end)(Fragment *, Fragment *),
                             FragmentKind entry_kind) {
  auto next = *next_ptr;
  if (next && is_end(curr, next)) {
    // Try to merge some of the entry fragments using the `transient_back_link`
    // pointer in fragment. This will allow us to generate slightly tighter
    // code.
    auto back_link = next->cached_back_link;
    if (back_link && next->partition_id == back_link->partition_id) {
      *next_ptr = back_link;
    } else {
      auto entry_frag = MakeFragment(entry_kind, next, next, curr->next);
      curr->next = entry_frag;
      *next_ptr = entry_frag;
      next->cached_back_link = entry_frag;
    }
  }
}

// Add in all of the entry fragments of a particular kind.
static void AddEntryFragments(Fragment * const frags,
                              bool (*is_end)(Fragment *, Fragment *),
                              FragmentKind entry_kind) {
  for (auto frag : FragmentIterator(frags)) {
    frag->cached_back_link = nullptr;
  }
  for (auto frag : FragmentIterator(frags)) {
    AddEntryFragment(frag, &(frag->branch_target), is_end, entry_kind);
    AddEntryFragment(frag, &(frag->fall_through_target), is_end, entry_kind);
  }
}



// Associate every fragment within a partition with the same partition
// sentinel.
static void IdentifyPartitionSentinels(Fragment * const frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentIterator(frags)) {
      if (FRAG_KIND_PARTITION_EXIT != frag->kind) {
        auto part = frag->partition_sentinel;
        auto old_part = part;
        if (frag->fall_through_target) {
          part = std::max(part, frag->fall_through_target->partition_sentinel);
          changed = changed || part != old_part;
          if (frag->branch_target) {
            part = std::max(part, frag->branch_target->partition_sentinel);
            changed = changed || part != old_part;
            frag->branch_target->partition_sentinel = part;
          }
          frag->fall_through_target->partition_sentinel = part;
        }
        frag->partition_sentinel = part;
      }
    }
  }
}

}  // namespace

// Adds designated entry and exit fragments around fragment partitions and
// around groups of instrumentation code fragments. First we add entry/exits
// around instrumentation code fragments for saving/restoring flags, then we
// add entry/exits around the partitions for saving/restoring registers.
void AddEntryAndExitFragments(Fragment **frags_ptr) {
  InitEntryFragments(frags_ptr);
  auto frags = *frags_ptr;
  AddExitFragments(frags, IsFlagExit, FRAG_KIND_FLAG_EXIT);
  AddEntryFragments(frags, IsFlagEntry, FRAG_KIND_FLAG_ENTRY);
  AddEntryFragments(frags, IsPartitionEntry, FRAG_KIND_PARTITION_ENTRY);
  AddExitFragments(frags, IsPartitionExit, FRAG_KIND_PARTITION_EXIT);
  IdentifyPartitionSentinels(frags);
}
#endif

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
    } else if (auto succ_code = DynamicCast<CodeFragment *>(succ)){
      exit_live_flags |= succ_code->flags.entry_live_flags;
    }
  }
  return exit_live_flags;
}

// Analyzes and updates the flag use for a fragment. If the fragment's flag
// use was changed then this returns true.
static bool AnalyzeFlagUse(CodeFragment *frag) {
  FlagUsageInfo flags;
  flags.all_killed_flags = 0U;
  flags.entry_live_flags = LiveFlagsOnExit(frag);
  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      VisitInstructionFlags(ninstr->instruction, &flags);
    }
  }
  const auto old_live_flags = frag->flags.entry_live_flags;
  frag->flags = flags;
  return old_live_flags != flags.entry_live_flags;
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
  if (!(frag->flags.all_killed_flags & live_flags_exit)) {
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
  AnalyzeFlagsUse(frags);
  ConvertToAppFrags(frags);
  AddExitFragments(frags, IsFlagExit, MakeFragment<FlagExitFragment>);
  AddEntryFragments(frags, IsFlagEntry, MakeFragment<FlagEntryFragment>);

  AddEntryFragments(frags, IsPartitionEntry,
                    MakeFragment<PartitionEntryFragment>);
  frags->Prepend(MakeFragment<PartitionEntryFragment>(frags->First(),
                                                      frags->First()));
  AddExitFragments(frags, IsPartitionExit,
                   MakeFragment<PartitionExitFragment>);
  LabelPartitions(frags);
}

}  // namespace granary
