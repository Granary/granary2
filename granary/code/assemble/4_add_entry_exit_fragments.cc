/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/assemble/4_add_entry_exit_fragments.h"
#include "granary/code/assemble/fragment.h"

#include "granary/util.h"

namespace granary {
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
    auto back_link = next->transient_back_link;
    if (back_link && curr->partition_id == back_link->partition_id) {
      *next_ptr = back_link;
    } else {
      auto exit_frag = MakeFragment(exit_kind, curr, next, curr->next);
      curr->next = exit_frag;
      *next_ptr = exit_frag;
      next->transient_back_link = exit_frag;
    }
  }
}

// Add in all of the end fragments of a particular kind.
static void AddExitFragments(Fragment * const frags,
                             bool (*is_end)(Fragment *, Fragment *),
                             FragmentKind exit_kind) {
  for (auto frag : FragmentIterator(frags)) {
    frag->transient_back_link = nullptr;
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
    auto back_link = next->transient_back_link;
    if (back_link && next->partition_id == back_link->partition_id) {
      *next_ptr = back_link;
    } else {
      auto entry_frag = MakeFragment(entry_kind, next, next, curr->next);
      curr->next = entry_frag;
      *next_ptr = entry_frag;
      next->transient_back_link = entry_frag;
    }
  }
}

// Add in all of the entry fragments of a particular kind.
static void AddEntryFragments(Fragment * const frags,
                              bool (*is_end)(Fragment *, Fragment *),
                              FragmentKind entry_kind) {
  for (auto frag : FragmentIterator(frags)) {
    frag->transient_back_link = nullptr;
  }
  for (auto frag : FragmentIterator(frags)) {
    AddEntryFragment(frag, &(frag->branch_target), is_end, entry_kind);
    AddEntryFragment(frag, &(frag->fall_through_target), is_end, entry_kind);
  }
}

// Returns true if the transition between `curr` and `next` represents a flags
// entry point.
static bool IsFlagEntry(Fragment *curr, Fragment *next) {
  return FRAG_KIND_INSTRUMENTATION == next->kind &&
         FRAG_KIND_FLAG_ENTRY != curr->kind &&
         (curr->partition_id != next->partition_id ||
          curr->kind != next->kind);
}

// Returns true if the transition between `curr` and `next` represents a flags
// exit point.
static bool IsFlagExit(Fragment *curr, Fragment *next) {
  return FRAG_KIND_INSTRUMENTATION == curr->kind &&
         (curr->partition_id != next->partition_id ||
          curr->kind != next->kind);
}

// Returns true if the transition between `curr` and `next` represents a
// partition entry point.
static bool IsPartitionEntry(Fragment *curr, Fragment *next) {
  return curr->partition_id != next->partition_id &&
         !(next->is_exit || next->is_future_block_head);
}

// Returns true if the transition between `curr` and `next` represents a
// partition exit point.
static bool IsPartitionExit(Fragment *curr, Fragment *next) {
  return curr->partition_id != next->partition_id;
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
}

}  // namespace granary
