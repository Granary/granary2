/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/register.h"

#include "granary/code/assemble/6_track_virtual_regs.h"

#include "granary/util.h"

namespace granary {
namespace arch {

// Returns `true` if `op` in `instr`, which looks like a read/write operand,
// actually behaves like a write. This happens for things like:
//      SUB R, R
//      XOR R, R
//      AND R, 0
//
// Note: This function has an architecture-specific implementation.
extern bool OperandIsWrite(const NativeInstruction *instr,
                           const granary::Operand *op);

}  // namespace arch
namespace {

// Adds a virtual register as either a use or a def to `instr`.
static void AddOperand(CodeFragment *frag, NativeInstruction *instr,
                       const Operand *op, VirtualRegister reg) {
  if (!reg.IsVirtual()) return;
  auto vr_id = static_cast<uint16_t>(reg.Number());

  // Figure out the action that should be associated with all dependencies
  // of this operand. Later we'll also do minor post-processing of all
  // operands that will potentially convert some `WRITE`s into `READ_WRITE`s
  // where the same register appears as both a read and write operand.
  // Importantly, we could have the same register is a write reg, and a read
  // memory, and in that case we wouldn't perform any such conversions.
  if (op->IsMemory()) {
    instr->used_vrs[instr->num_used_vrs++] = vr_id;

  } else if (op->IsConditionalWrite() || op->IsReadWrite()) {
    frag->def_regs[vr_id]++;

    // Used to handle things like `SUB A, A` and `XOR A, A`.
    if (arch::OperandIsWrite(instr, op)) {
      GRANARY_ASSERT(!instr->defined_vr);
      instr->defined_vr = vr_id;
    } else {
      instr->used_vrs[instr->num_used_vrs++] = vr_id;
    }

  } else if (op->IsWrite()) {
    frag->def_regs[vr_id]++;

    if (!op->IsSemanticDefinition() && reg.PreservesBytesOnWrite()) {
      instr->used_vrs[instr->num_used_vrs++] = vr_id;
    } else {
      GRANARY_ASSERT(!instr->defined_vr);
      instr->defined_vr = vr_id;
    }
  } else {
    instr->used_vrs[instr->num_used_vrs++] = vr_id;
  }

  GRANARY_ASSERT(instr->num_used_vrs <=
                 (sizeof instr->used_vrs / sizeof instr->used_vrs[0]));
}

// Find the VRs defined/used in `op`, and add then to `ninstr`.
static void AddOpVRs(CodeFragment *frag, NativeInstruction *instr,
                     Operand *op) {
  if (!op->IsExplicit()) return;

  // Ignore all non general-purpose registers, as they cannot be scheduled with
  // virtual registers.
  if (op->IsRegister()) {
    auto reg_op = UnsafeCast<const RegisterOperand *>(op);
    AddOperand(frag, instr, op, reg_op->Register());

  // Only use memory operands that contain general-purpose registers.
  } else if (op->IsMemory()) {
    auto mem_op = UnsafeCast<MemoryOperand *>(op);
    if (mem_op->IsPointer()) return;

    VirtualRegister r1, r2;
    if (mem_op->CountMatchedRegisters(r1, r2)) {
      AddOperand(frag, instr, op, r1);
      AddOperand(frag, instr, op, r2);
    }
  }
}

static void FindInstructionVRs(CodeFragment *frag, NativeInstruction *instr) {
  instr->ForEachOperand([=] (Operand *op) { AddOpVRs(frag, instr, op); });

  for (auto vr_id : instr->used_vrs) {
    if (vr_id) {
      frag->entry_regs.Add(vr_id);
      frag->exit_regs.Add(vr_id);
    }
  }
  if (instr->defined_vr) {
    frag->entry_regs.Remove(instr->defined_vr);
    frag->exit_regs.Add(instr->defined_vr);
  }
}

static void FindInstructionVRs(CodeFragment *frag) {
  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      FindInstructionVRs(frag, ninstr);
    }
  }
}

static void FindInstructionVRs(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      FindInstructionVRs(cfrag);
    }
  }
}


// Back-propagate the entry nodes of `succ` into the exit nodes of `frag`, then
// update the entry nodes of `succ` if necessary.
static bool BackPropagateEntryDefs(CodeFragment *pred, CodeFragment *succ) {
  auto changed = false;
  for (auto vr_id : succ->entry_regs) {
    // Either `vr_id` was locally defined in `pred`, or a previous iteration
    // performed the propagation already.
    if (pred->exit_regs.Contains(vr_id)) continue;

    // Inherit it.
    GRANARY_ASSERT(!pred->entry_regs.Contains(vr_id));
    pred->exit_regs.Add(vr_id);
    if (!pred->attr.follows_partition_entrypoint) {
      pred->entry_regs.Add(vr_id);
    }
    changed = true;
  }
  return changed;
}

// Back-propagate VRs through the fragment list.
static void BackPropagateEntryDefs(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
        for (auto succ : cfrag->successors) {
          if (auto succ_cfrag = DynamicCast<CodeFragment *>(succ)) {
            changed = BackPropagateEntryDefs(cfrag, succ_cfrag) || changed;
          }
        }
      }
    }
  }
}

// Get the set of registers defined by `frag`.
static void GetDefSet(CodeFragment *frag, VRIdSet *def_set) {
  for (auto vr_id : frag->exit_regs) {
    if (!frag->entry_regs.Contains(vr_id)) def_set->Add(vr_id);
  }
}

// Extend live ranges of VRs. This is sensitive to the code cache tier of a
// fragment. The idea is that we want to minimize the number of spills/fills,
// but only within a given code cache tier. Specifically, we don't want to
// extend the live range of a variable defined in cold code to be in hot code.
//
// TODO(pag): Should we try to propagate hot registers to all fragments to
//            reduce the number of compensation fragments?
static void ExtendLiveRanges(FragmentList *frags) {

  // Step 1: Find the definitions in the successors that are at least as hot
  // as us, and back-propagate those definitions into our frag's `exit_regs.`
  for (auto frag : ReverseFragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;
    VRIdSet def_set;
    for (auto succ_frag : frag->successors) {
      if (!succ_frag) continue;
      if (frag->cache < succ_frag->cache) continue;
      if (frag->partition != succ_frag->partition) continue;
      if (auto succ_cfrag = DynamicCast<CodeFragment *>(succ_frag)) {
        GetDefSet(succ_cfrag, &def_set);
      }
    }
    cfrag->exit_regs.Union(def_set);
  }

  // Step 2: For those fragments where we found definitions, make the
  //         definitions live on entry.
  //
  // Note: This is a separate step just in case a given fragment has many
  //       predecessors.
  for (auto frag : ReverseFragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;
    for (auto succ_frag : frag->successors) {
      if (!succ_frag) continue;
      if (frag->cache < succ_frag->cache) continue;
      if (frag->partition != succ_frag->partition) continue;
      auto succ_cfrag = DynamicCast<CodeFragment *>(succ_frag);
      if (!succ_cfrag) continue;

      VRIdSet def_set;
      GetDefSet(succ_cfrag, &def_set);
      succ_cfrag->entry_regs.Union(def_set);
    }
  }
}

// Make a compensation fragment.
static void MakeCompensatingFrag(FragmentList *frags, CodeFragment *pred,
                                 Fragment **succ_ptr, const VRIdSet &entry_regs,
                                 const VRIdSet &exit_regs) {
  auto succ = *succ_ptr;
  auto comp = new CodeFragment;
  comp->attr.is_compensation_frag = true;
  comp->entry_regs.Union(entry_regs);
  comp->exit_regs.Union(exit_regs);
  comp->block_meta = pred->block_meta;
  comp->stack_status = pred->stack_status;

  // Might be at the end of a partition, so need `pred`s partition info.
  comp->partition.Union(pred->partition);

  // `comp` doesn't affect flags, and might be placed *after* a flag exit
  // frag, so it definitely isn't part of the same flag zone!
  comp->flag_zone.Union(succ->flag_zone);

  // Tricky! Compensation code goes between `pred` and `succ`, so if the jump
  // goes from hot (pred) to cold (succ) code, then we want the compensation
  // code to be cold so that it's not occupying space in the hot region.
  if (pred->cache < succ->cache) {  // hot-to-cold
    comp->cache = succ->cache;
  } else {
    comp->cache = pred->cache;  // cold-to-cold
  }

  // Chain it into the control-flow.
  comp->successors[kFragSuccFallThrough] = succ;
  *succ_ptr = comp;

  frags->InsertAfter(pred, comp);  // Chain it into the fragment list.
}

static void AddCompensatingFragment(FragmentList *frags, CodeFragment *pred,
                                    CodeFragment *succ, Fragment **succ_ptr) {
  GRANARY_ASSERT(nullptr != succ);

  if (succ->attr.is_compensation_frag) return;

  VRIdSet needed_kills(pred->exit_regs);
  for (auto vr_id : pred->exit_regs) {
    if (!succ->entry_regs.Contains(vr_id)) goto add_comp_frag;
  }
  return;

 add_comp_frag:
  MakeCompensatingFrag(frags, pred, succ_ptr, pred->exit_regs,
                       succ->entry_regs);
}

// If a virtual register R is live on exit in `pred` but not live on entry
// in `succ` then add a compensating fragment between `pred` and `succ` that
// contains R as as live on entry, and explicitly kills those variables using
// special annotation instructions.
//
// TODO(pag): This isn't the an optimal way of doing things. We could do a
//            "pre-filtering" where we add compensation kills that never
//            reach any of the successors. This avoids adding an extra
//            fragment (or two), and potentially avoids adding two
//            instructions.
//
// Note: `succ` is passed by reference so that we can update the correct
//       successor entry in `pred` more easily.
static void AddCompensatingFragment(FragmentList *frags, CodeFragment *pred,
                                    Fragment **succ_ptr) {
  if (!pred->exit_regs.Size()) return;

  if (auto succ_cfrag = DynamicCast<CodeFragment *>(*succ_ptr)) {
    AddCompensatingFragment(frags, pred, succ_cfrag, succ_ptr);
    return;
  }

  const VRIdSet empty_set;
  MakeCompensatingFrag(frags, pred, succ_ptr, pred->exit_regs, empty_set);
}

// Goes and adds "compensating" fragments. The idea here is that if we have
// an edge between a predecessor fragment P and its successor S, and some
// register R is live on exit from P, but is not live on entry to S, then
// really it is killed in the transition from P to S. We need to explicitly
// represent this "death" (for later allocation purposes) by introducing
// a dummy compensating fragment.
static void AddCompensatingFragments(FragmentList *frags) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (cfrag->attr.is_compensation_frag) continue;
      for (auto &succ : cfrag->successors) {
        if (succ) AddCompensatingFragment(frags, cfrag, &succ);
      }
    }
  }
}

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
// Asserts that there are no live VRs on entry to any frag that begins
// a partition.
static void CheckForUndefinedVirtualRegs(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<PartitionEntryFragment *>(frag)) {
      for (auto succ : frag->successors) {
        if (auto succ_cfrag = DynamicCast<CodeFragment *>(succ)) {
          GRANARY_ASSERT(!succ_cfrag->entry_regs.Size());
        }
      }
    }
  }
}
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test

}  // namespace

// Track virtual registers through the fragment graph.
void TrackVirtualRegs(FragmentList * const frags) {
  FindInstructionVRs(frags);
  BackPropagateEntryDefs(frags);
  ExtendLiveRanges(frags);
  AddCompensatingFragments(frags);
  GRANARY_IF_DEBUG( CheckForUndefinedVirtualRegs(frags); )
}

}  // namespace granary
