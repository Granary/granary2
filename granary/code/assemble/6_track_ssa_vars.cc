/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/register.h"
#include "granary/code/ssa.h"

#include "granary/code/assemble/6_track_ssa_vars.h"

#include "granary/util.h"

namespace granary {
namespace arch {

// Performs architecture-specific conversion of `SSAOperand` actions. The things
// we want to handle here are instructions like `XOR A, A`, that can be seen as
// clearing the value of `A` and not reading it for the sake of reading it.
//
// Note: This function has an architecture-specific implementation.
extern void ConvertOperandActions(NativeInstruction *instr);
}  // namespace arch
namespace {

// Adds `reg` as an `SSAOperand` to the `operands` operand pack.
static void AddSSAOperand(SSAInstruction *instr, Operand *op,
                          VirtualRegister reg) {
  if (!reg.IsGeneralPurpose() && !reg.IsStackPointer()) return;
  GRANARY_ASSERT(op->IsValid());

  auto &ssa_op(instr->ops[instr->num_ops++]);
  ssa_op.operand = op->UnsafeExtract();

  GRANARY_ASSERT(ssa_op.reg_web.Find() == &(ssa_op.reg_web));
  *(ssa_op.reg_web) = reg;

  // Figure out the action that should be associated with all dependencies
  // of this operand. Later we'll also do minor post-processing of all
  // operands that will potentially convert some `WRITE`s into `READ_WRITE`s
  // where the same register appears as both a read and write operand.
  // Importantly, we could have the same register is a write reg, and a read
  // mem, and in that case we wouldn't perform any such conversions.
  if (ssa_op.operand->IsMemory()) {
    ssa_op.action = kSSAOperandActionMemoryRead;

  } else if (op->IsConditionalWrite() || op->IsReadWrite()) {
    ssa_op.action = kSSAOperandActionReadWrite;

  } else if (op->IsWrite()) {
    if (!op->IsSemanticDefinition() && reg.PreservesBytesOnWrite()) {
      ssa_op.action = kSSAOperandActionReadWrite;
    } else {
      ssa_op.action = kSSAOperandActionWrite;
    }
  } else {
    ssa_op.action = kSSAOperandActionRead;
  }
}

// Add an `SSAOperand` to an operand pack.
static void AddSSAOperand(SSAInstruction *instr, Operand *op) {

  // Ignore all non general-purpose registers, as they cannot be scheduled with
  // virtual registers.
  if (op->IsRegister()) {
    auto reg_op = UnsafeCast<const RegisterOperand *>(op);
    AddSSAOperand(instr, op, reg_op->Register());

  // Only use memory operands that contain general-purpose registers.
  } else if (op->IsMemory()) {
    auto mem_op = UnsafeCast<MemoryOperand *>(op);
    if (mem_op->IsPointer()) return;

    VirtualRegister r1, r2;
    if (mem_op->CountMatchedRegisters(r1, r2)) {
      AddSSAOperand(instr, op, r1);
      AddSSAOperand(instr, op, r2);
    }
  }
}

// Returns true of we find a read register operand in the `operands` pack that
// uses the same register as `op`.
//
// Note: This assumes that `op` refers to a register operand whose action is
//       a `WRITE`.
static bool FindReadFromReg(const SSAInstruction *instr, VirtualRegister reg) {
  for (const auto &op : instr->ops) {
    if (kSSAOperandActionRead == op.action ||
        kSSAOperandActionMemoryRead == op.action ||
        kSSAOperandActionReadWrite == op.action) {
      if (reg == *(op.reg_web)) return true;
    }
  }
  return false;
}

}  // namespace

// Convert writes to register operates into read/writes if there is another
// read from the same register (that isn't a memory operand) in the current
// operand pack.
//
// Note: This function is also used by `7_propagate_copies`.
void ConvertOperandActions(NativeInstruction *instr) {
  auto ssa_instr = instr->ssa;
  auto changed = false;
  for (auto &op : ssa_instr->ops) {
    if (kSSAOperandActionWrite == op.action) {
      auto reg = *(op.reg_web);
      if (FindReadFromReg(ssa_instr, reg)) {
        op.action = kSSAOperandActionReadWrite;
        changed = true;
      }
    }
  }
  arch::ConvertOperandActions(instr);
}

namespace {

// Create an `SSAInstruction` for a `NativeInstruction`.
static void CreateSSAInstruction(NativeInstruction *instr) {
  auto ssa_instr = new SSAInstruction;
  instr->ssa = ssa_instr;
  instr->ForEachOperand([=] (Operand *op) {
    AddSSAOperand(ssa_instr, op);
  });
  ConvertOperandActions(instr);
}

// Create the `SSAOperandPack`s for every native instruction in `SSAFragment`
// fragments.
static void CreateSSAInstructions(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<SSAFragment *>(frag)) {
      for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          CreateSSAInstruction(ninstr);
        }
      }
    }
  }
}

// Update or create a register web.
static void UpdateOrCreateWeb(SSAFragment *frag, SSARegisterWeb *web) {
  auto reg = web->Value();
  auto &entry_web(frag->ssa.entry_reg_webs[reg]);
  if (entry_web) {
    GRANARY_ASSERT(reg == entry_web->Value());
    entry_web->Union(web);
  }
  entry_web = UnsafeCast<SSARegisterWeb *>(web->Find());
}

// Perform local value numbering for definitions.
static void LVNDefs(SSAFragment *frag, SSAInstruction *ssa_instr) {
  for (auto &op : ssa_instr->ops) {
    if (kSSAOperandActionInvalid == op.action) break;
    if (kSSAOperandActionWrite == op.action ||
        kSSAOperandActionCleared == op.action) {
      UpdateOrCreateWeb(frag, &(op.reg_web));
    }
  }

  // Kill this web for all preceding instructions.
  for (auto &op : ssa_instr->ops) {
    if (kSSAOperandActionInvalid == op.action) break;
    if (kSSAOperandActionWrite == op.action) {
      auto web = &(op.reg_web);
      frag->ssa.internal_reg_webs.Append(web);
      frag->ssa.entry_reg_webs.Remove(web->Value());
    }
  }
}

// Perform local value numbering for uses.
static void LVNUses(SSAFragment *frag, SSAInstruction *ssa_instr) {
  for (auto &op : ssa_instr->ops) {
    if (kSSAOperandActionInvalid == op.action) break;
    if (kSSAOperandActionWrite != op.action &&
        kSSAOperandActionCleared != op.action) {
      UpdateOrCreateWeb(frag, &(op.reg_web));
    }
  }
}

// Create an `SSARegisterWeb` for a save.
//
// Note: Save and restore registers must be native so that we don't end up with
//       the case where virtual registers get back-propagated to the head of a
//       partition and end up missing definitions.
static void LVNSave(SSAFragment *frag, AnnotationInstruction *instr) {
  auto reg = instr->Data<VirtualRegister>();
  GRANARY_ASSERT(reg.IsGeneralPurpose() && reg.IsNative());
  auto web = new SSARegisterWeb(reg);
  UpdateOrCreateWeb(frag, web);
  SetMetaData(instr, web);
}

// Create an `SSARegisterWeb` for a restore.
static void LVNRestore(SSAFragment *frag, AnnotationInstruction *instr) {
  auto reg = instr->Data<VirtualRegister>();
  GRANARY_ASSERT(reg.IsGeneralPurpose() && reg.IsNative());
  auto web = new SSARegisterWeb(reg);
  UpdateOrCreateWeb(frag, web);
  SetMetaData(instr, web);

  frag->ssa.internal_reg_webs.Append(web);

  // Update `entry_nodes` in place to contain a new, dummy node.
  web = new SSARegisterWeb(reg);
  auto ainstr = new AnnotationInstruction(kAnnotSSARegisterWebOwner);
  SetMetaData(ainstr, web);
  frag->ssa.entry_reg_webs[reg] = web;
  frag->instrs.InsertBefore(instr, ainstr);
}

// Perform a local-value numbering of all general-purpose register uses
// within an `SSAFragment` fragment.
static void LocalValueNumbering(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          if (auto ssa_instr = ninstr->ssa) {
            LVNDefs(ssa_frag, ssa_instr);
            LVNUses(ssa_frag, ssa_instr);
          }
        } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
          if (kAnnotSSASaveRegister == ainstr->annotation) {
            LVNSave(ssa_frag, ainstr);
          } else if (kAnnotSSARestoreRegister == ainstr->annotation ||
                     kAnnotSSASwapRestoreRegister == ainstr->annotation) {
            LVNRestore(ssa_frag, ainstr);
          }
        }
      }
    }
  }
}

// Back-propagate the entry nodes of `succ` into the exit nodes of `frag`, then
// update the entry nodes of `succ` if necessary.
static bool BackPropagateEntryDefs(SSAFragment *frag, SSAFragment *succ) {
  auto changed = false;
  for (auto entry : succ->ssa.entry_reg_webs) {
    const auto reg = entry.key;
    auto succ_web = UnsafeCast<SSARegisterWeb *>(entry.value->Find());
    GRANARY_ASSERT(reg == succ_web->Value());
    GRANARY_ASSERT(reg.IsGeneralPurpose() || reg.IsStackPointer());

    auto &exit_web(frag->ssa.exit_reg_webs[reg]);

    // Already inherited, either in a previous step, or by a different
    // successor of `frag` that we've already visited.
    if (exit_web) {
      if (exit_web->Find() != succ_web) {
        exit_web->Union(succ_web);
        changed = true;
      }
      continue;
    }

    // Local def of a register used in the successor.
    for (auto internal_web : frag->ssa.internal_reg_webs) {
      if (reg == internal_web->Value()) {
        internal_web->Union(succ_web);
        exit_web = internal_web;
        changed = true;
        break;
      }
    }

    if (changed) continue;

    // `frag` (predecessor of `succ`) doesn't define or use `reg`, so inherit
    // the node directly and pass it up through the `entry_nodes` as well.
    auto &entry_web(frag->ssa.entry_reg_webs[reg]);
    if (!entry_web) {
      entry_web = succ_web;
      exit_web = succ_web;
      changed = true;

    // Used in `frag` but not defined.
    } else {
      exit_web = entry_web;
      exit_web->Union(succ_web);
    }
  }
  return changed;
}

// Back propagates entry definitions of a successor fragment into the exit and
// entry definitions of a predecessor fragment.
static void BackPropagateEntryDefs(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
        for (auto succ : frag->successors) {
          if (auto ssa_succ = DynamicCast<SSAFragment *>(succ)) {
            changed = BackPropagateEntryDefs(ssa_frag, ssa_succ) || changed;
          }
        }
      }
    }
  }
}

static void AddCompensationRegKills(CodeFragment *frag) {
  for (auto reg_web : frag->ssa.entry_reg_webs.Values()) {
    auto instr = new AnnotationInstruction(kAnnotSSARegisterKill,
                                           reg_web->Value());
    SetMetaData(instr, reg_web);
    frag->instrs.Append(instr);
  }
}

// If a virtual register R is live on exit in `pred` but not live on entry
// in `succ` then add a compensating fragment between `pred` and `succ` that
// contains R as as live on entry, and explicitly kills those variables using
// special annotation instructions.
//
// Note: `succ` is passed by reference so that we can update the correct
//       successor entry in `pred` more easily.
static void AddCompensatingFragment(FragmentList *frags, SSAFragment *pred,
                                    Fragment *&succ) {
  GRANARY_ASSERT(nullptr != succ);

  auto comp = new CodeFragment;

  // Fill the compensation fragment with default regs that should be
  // inherited.
  for (const auto &exit_node : pred->ssa.exit_reg_webs) {
    if (exit_node.key.IsVirtual()) {
      GRANARY_ASSERT(nullptr != exit_node.value);
      comp->ssa.entry_reg_webs[exit_node.key] = exit_node.value;
    }
  }

  // Look for regs that are not in the exit regs, and kill those.
  if (auto ssa_succ = DynamicCast<SSAFragment *>(succ)) {
    for (auto entry_reg : ssa_succ->ssa.entry_reg_webs.Keys()) {
      if (entry_reg.IsVirtual()) {
        comp->ssa.entry_reg_webs.Remove(entry_reg);
      }
    }
  }

  // No "leaky" definitions to compensate for.
  if (!comp->ssa.entry_reg_webs.Size()) {
    delete comp;
    return;
  }

  // Make `comp` appear to be yet another `CodeFragment` to all future
  // assembly passes.
  if (auto code_pred = DynamicCast<CodeFragment *>(pred)) {

    if (code_pred->attr.branch_is_function_call &&
        succ == pred->successors[kFragSuccFallThrough]) {
      GRANARY_ASSERT(false);  // TODO(pag): I don't understand this anymore.
      delete comp;  // E.g. fall-through after an indirect CALL.
      return;
    }

    comp->attr.block_meta = code_pred->attr.block_meta;
    comp->stack.status = code_pred->stack.status;
  }

  comp->attr.is_compensation_code = true;
  comp->partition.Union(pred->partition);
  comp->flag_zone.Union(pred->flag_zone);

  // Chain it into the control-flow.
  comp->successors[kFragSuccFallThrough] = succ;
  succ = comp;

  frags->InsertAfter(pred, comp);  // Chain it into the fragment list.

  AddCompensationRegKills(comp);
}

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
// Asserts that there are nodes (of any type) on entry to frag that are
// associated with virtual registers. This can happen in the case where some
// instrumentation reads from a virtual register before writing to it. We
// handle some architecture-specific special cases like `XOR A, A` on x86
// when building up the `SSAInstruction`s and by using the
// `CLEARED` action.
static void CheckForUndefinedVirtualRegs(SSAFragment *frag) {
  for (auto reg : frag->ssa.entry_reg_webs.Keys()) {
    GRANARY_ASSERT(!reg.IsVirtual());
  }
}
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test

// Goes and adds "compensating" fragments. The idea here is that if we have
// an edge between a predecessor fragment P and its successor S, and some
// register R is live on exit from P, but is not live on entry to S, then
// really it is killed in the transition from P to S. We need to explicitly
// represent this "death" (for later allocation purposes) by introducing
// a dummy compensating fragment.
static void AddCompensatingFragments(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (code_frag->attr.is_compensation_code) continue;
    }
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      for (auto &succ : ssa_frag->successors) {
        if (succ) {
          AddCompensatingFragment(frags, ssa_frag, succ);
        }
      }

    } else if (IsA<PartitionEntryFragment *>(frag)) {
#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
      for (auto succ : frag->successors) {
        if (auto ssa_succ = DynamicCast<SSAFragment *>(succ)) {
          CheckForUndefinedVirtualRegs(ssa_succ);
        }
      }
#endif  // GRANARY_TARGET_debug
    }
  }
}

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
// Verify that the sets of virtual registers used in each partition are
// disjoint.
static void VerifyVRUsage(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (!IsA<PartitionEntryFragment *>(frag)) continue;
    auto frag_partition = frag->partition.Value();
    for (auto other_frag : FragmentListIterator(frags)) {
      if (frag == other_frag) continue;
      if (!IsA<PartitionEntryFragment *>(other_frag)) continue;
      auto other_frag_partition = other_frag->partition.Value();

      // Verify that there is only one partition entry fragment per partition.
      GRANARY_ASSERT(frag_partition != other_frag_partition);
    }
  }
}
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test
}  // namespace

// Build a graph for the SSA definitions associated with the fragments.
void TrackSSAVars(FragmentList * const frags) {
  CreateSSAInstructions(frags);
  LocalValueNumbering(frags);
  BackPropagateEntryDefs(frags);
  AddCompensatingFragments(frags);
  GRANARY_IF_DEBUG( VerifyVRUsage(frags); )
}

}  // namespace granary
