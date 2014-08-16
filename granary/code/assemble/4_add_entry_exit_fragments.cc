/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/control_flow_graph.h"
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

// Returns the architectural register that is potentially killed by the
// instructions injected to save/restore flags.
//
// Note: This must return a register with width `arch::GPR_WIDTH_BYTES` if the
//       returned register is valid.
//
// Note: This has an architecture-specific implementation.
extern VirtualRegister FlagKillReg(void);

}  // namespace arch
namespace {

// Converts instrumentation fragments into application fragments where the
// flags usage permits.
static void PropagateFragKinds(FragmentList *frags) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (code_frag->attr.is_app_code) continue;
      if (code_frag->attr.is_inst_code) continue;

      // Try to copy it from any successor.
      auto has_code_succ = false;
      for (auto succ : frag->successors) {
        if (auto code_succ = DynamicCast<CodeFragment *>(succ)) {
          code_frag->attr.is_app_code = code_succ->attr.is_app_code;
          code_frag->attr.is_inst_code = code_succ->attr.is_inst_code;
          has_code_succ = true;
        }
      }

      if (!code_frag->attr.is_app_code && !code_frag->attr.is_inst_code) {
        if (has_code_succ) {
          code_frag->attr.is_inst_code = true;
        } else {
          code_frag->attr.is_app_code = true;
        }
      }
    }
  }
}

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
      return LiveFlagsOnEntry(frag->successors[FRAG_SUCC_BRANCH]);
    }
  }

  auto fall_live = LiveFlagsOnEntry(frag->successors[FRAG_SUCC_FALL_THROUGH]);
  auto branch_live = LiveFlagsOnEntry(frag->successors[FRAG_SUCC_BRANCH]);
  return {
    fall_live.app_flags | branch_live.app_flags,
    fall_live.inst_flags | branch_live.inst_flags
  };
}

#ifdef GRANARY_DEBUG
static void VerifyFlagUse(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;
    auto frag_is_app = cfrag->attr.is_app_code;
    GRANARY_ASSERT(frag_is_app != cfrag->attr.is_inst_code);

    for (auto instr : InstructionListIterator(frag->instrs)) {
      auto ninstr = DynamicCast<NativeInstruction *>(instr);
      if (!ninstr) continue;

      auto uses_flags = ninstr->ReadsConditionCodes() ||
                        ninstr->WritesConditionCodes();
      auto instr_is_app = ninstr->IsAppInstruction();
      GRANARY_ASSERT(!uses_flags || (frag_is_app == instr_is_app));
    }
  }
}
#endif  // GRANARY_DEBUG

static bool VisitFragmentFlags(CodeFragment *frag) {
  FlagUsageInfo *flags(nullptr);
  FlagUsageInfo new_flags;
  auto exit_flags = LiveFlagsOnExit(frag);

  if (frag->attr.is_app_code) {
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
      VisitInstructionFlags(ninstr->instruction, &new_flags);
    }
  }

  auto ret = 0 != memcmp(flags, &new_flags, sizeof new_flags);
  *flags = new_flags;
  return ret;
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

static void AnalyzeFlagsUse(FragmentList *frags) {
  InitFlagsUse(frags);
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
        changed = VisitFragmentFlags(code_frag) || changed;
      }
    }
  }
}

// Group fragments together into flag zones.
static void UnionFlagZones(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto code_frag = DynamicCast<CodeFragment *>(frag);
    if (!code_frag) continue;

    for (auto succ : frag->successors) {
      auto code_succ = DynamicCast<CodeFragment *>(succ);
      if (!code_succ) continue;
      if (frag->partition != succ->partition) continue;
      if (frag->flag_zone == succ->flag_zone) continue;
      if (code_frag->attr.is_app_code != code_succ->attr.is_app_code) continue;

      code_frag->flag_zone.Union(code_frag, code_succ);
    }
  }
}

// Allocate flag zones for instrumentation fragments.
static void LabelFlagZones(LocalControlFlowGraph *cfg, FragmentList *frags) {
  UnionFlagZones(frags);
  for (auto frag : FragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (code_frag->attr.is_app_code) continue;
      auto &flag_zone(frag->flag_zone.Value());
      if (!flag_zone) {
        flag_zone = new FlagZone(
                    cfg->AllocateVirtualRegister(arch::GPR_WIDTH_BYTES),
                    arch::FlagKillReg());
      }
    }
  }
}

// Update the flag zones with the flags and registers used in the various
// fragments that belong to this flag zone, as well as the flags used *after*
// the flag zone.
static void UpdateFlagZones(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto &flag_zone(frag->flag_zone.Value());
    if (flag_zone) {
      if (auto code = DynamicCast<CodeFragment *>(frag)) {
        if (!code->attr.is_app_code) {
          flag_zone->killed_flags |= code->inst_flags.all_written_flags;
          flag_zone->live_flags |= code->app_flags.exit_live_flags;
        }
      }
    }
  }
}

// Returns true if the transition between `curr` and `next` represents a flags
// entry point.
//
// Flag entry points can only occur between two `CodeFragment`s, where we
// transition between partitions, or where we transition from application code
// to instrumentation code.
static bool IsFlagEntry(Fragment *curr, Fragment *next) {
  if (IsA<ExitFragment *>(next)) return false;
  if (IsA<PartitionEntryFragment *>(next)) return false;
  if (IsA<FlagEntryFragment *>(curr)) return false;
  if (IsA<FlagEntryFragment *>(next)) return false;
  if (curr->flag_zone == next->flag_zone &&
      !IsA<PartitionEntryFragment *>(curr)) {
    GRANARY_ASSERT(curr->partition == next->partition);
    return false;
  }
  auto next_zone = next->flag_zone.Value();
  if (!next_zone) return false;
  if (!next_zone->killed_flags) return false;
  return true;
  return  0 != (next_zone->killed_flags & curr->app_flags.exit_live_flags);
}

// Returns true if the transition between `curr` and `next` represents a flags
// exit point.
//
// A flags exit point occurs between an instrumentation code fragment and a
// partition change, or between an instrumentation code fragment and an
// application code fragment (or a flag entry, which will appear as non-equal
// partitions).
static bool IsFlagExit(Fragment *curr, Fragment *next) {
  if (IsA<PartitionExitFragment *>(curr)) return false;
  if (IsA<PartitionEntryFragment *>(next)) return false;
  if (IsA<FlagEntryFragment *>(curr)) return false;
  if (IsA<FlagExitFragment *>(curr)) return false;
  if (IsA<FlagExitFragment *>(next)) return false;
  if (curr->flag_zone == next->flag_zone &&
      !IsA<PartitionExitFragment *>(next)) {
    GRANARY_ASSERT(curr->partition == next->partition);
    return false;
  }
  auto curr_zone = curr->flag_zone.Value();
  if (!curr_zone) return false;
  if (!curr_zone->killed_flags) return false;
  return true;
  return  0 != (curr_zone->killed_flags & next->app_flags.entry_live_flags);
}

// Returns true if the transition between `curr` and `next` represents a
// partition entry point.
static bool IsPartitionEntry(Fragment *curr, Fragment *next) {
  if (IsA<PartitionEntryFragment *>(curr)) return false;
  if (IsA<PartitionEntryFragment *>(next)) return false;

  auto next_code = DynamicCast<CodeFragment *>(next);

  if (curr->partition == next->partition) {
    // Interesting special case: things like self-loops back to a block head
    // are considered partition entry/exit points because we want to make sure
    // that later stack frame size analysis determines a fixed stack frame size
    // for every partition.
    if (next_code && next_code->attr.is_block_head) {
      return next_code->attr.can_add_to_partition;
    }
    return false;
  }
  if (IsA<ExitFragment *>(next)) return false;

  // Un/conditional direct call/jump targeting direct edge code, an existing
  // block, or some native code. In this case, we don't want to introduce
  // intermediate (i.e. redundant) jumps to get to edge code when the branch
  // that's already there will suffice.
  if (next_code && next_code->branch_instr &&
      next_code->attr.branches_to_code &&
      !next_code->attr.can_add_to_partition) {
    return false;
  }

  return true;
}

// Returns true if the transition between `curr` and `next` represents a
// partition exit point.
static bool IsPartitionExit(Fragment *curr, Fragment *next) {
  if (IsA<PartitionExitFragment *>(curr)) return false;
  if (IsA<PartitionExitFragment *>(next)) return false;

  const auto curr_code = DynamicCast<CodeFragment *>(curr);

  // Un/conditional direct call/jump targeting direct edge code, an existing
  // block, or some native code. In this case, we don't want to introduce
  // intermediate (i.e. redundant) jumps to get to edge code when the branch
  // that's already there will suffice.
  if (curr_code && curr_code->branch_instr &&
      curr_code->attr.branches_to_code &&
      !curr_code->attr.can_add_to_partition &&
      next == curr_code->successors[FRAG_SUCC_BRANCH]) {
    return false;
  }

  if (auto next_exit = DynamicCast<ExitFragment *>(next)) {
    return EDGE_KIND_DIRECT != next_exit->edge.kind;

  // This is a fall-through from a function call, e.g. indirect function call.
  // In the case of an indirect function call, we don't want to place a
  // partition exit on its fall-through edge because the partition exiting
  // happens at the target of the call.
  } else if (curr_code && curr_code->attr.branches_to_code &&
             curr_code->attr.branch_is_function_call &&
             curr->successors[FRAG_SUCC_FALL_THROUGH] == next) {
    return false;

  // Catch this case where the current partition and the next partition are
  // the same, but where we've got a self-loop (e.g. back to the block head),
  // and so we're jumping to a partition entrypoint for the same partition.
  } else if (IsA<PartitionEntryFragment *>(next)) {
    return true;
  }
  if (curr->partition == next->partition) return false;

  return true;
}

// Reset the pass-specific "back link" pointer that is used to re-use entry and
// exit fragments.
static void ResetTempData(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    frag->temp.entry_exit_frag = nullptr;
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
    curr->RelinkBranchInstr(back_link);
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
    }
    curr->RelinkBranchInstr(back_link);
  }
}

// Make a specific type of fragment with a single successor.
template <typename T>
static Fragment *MakeFragment(Fragment *inherit, Fragment *succ) {
  Fragment *frag = new T;
  auto label = new LabelInstruction();
  SetMetaData(label, frag);
  frag->instrs.Append(label);
  frag->successors[0] = succ;
  frag->partition.Union(frag, inherit);
  frag->flag_zone.Union(frag, inherit);

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
static void LabelPartitionsAndTrackFlagRegs(FragmentList *frags) {
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
void AddEntryAndExitFragments(LocalControlFlowGraph *cfg, FragmentList *frags) {
  PropagateFragKinds(frags);
  GRANARY_IF_DEBUG( VerifyFlagUse(frags); )
  AnalyzeFlagsUse(frags);
  LabelFlagZones(cfg, frags);
  UpdateFlagZones(frags);

  // Guarantee that there is a partition entry fragment. The one special case
  // against this entry fragment is that the first instruction of the first
  // fragment is a function call / return / something else that can't be
  // added to a partition.
  ResetTempData(frags);
  auto first_frag = frags->First();
  auto first_cfrag = DynamicCast<CodeFragment *>(first_frag);
  if (!first_cfrag ||
      (first_cfrag && first_cfrag->attr.can_add_to_partition)) {
    auto first_entry = MakeFragment<PartitionEntryFragment>(first_frag,
                                                            first_frag);
    frags->Prepend(first_entry);
    first_frag->temp.entry_exit_frag = first_entry;
  }

  AddEntryFragments(frags, IsPartitionEntry,
                    MakeFragment<PartitionEntryFragment>);

  AddExitFragments(frags, IsPartitionExit,
                   MakeFragment<PartitionExitFragment>);

  ResetTempData(frags);
  AddEntryFragments(frags, IsFlagEntry, MakeFragment<FlagEntryFragment>);

  AddExitFragments(frags, IsFlagExit, MakeFragment<FlagExitFragment>);

  LabelPartitionsAndTrackFlagRegs(frags);
}

}  // namespace granary
