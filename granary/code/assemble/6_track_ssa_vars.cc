/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/util.h"

namespace granary {

// Performs architecture-specific conversion of `SSAOperand` actions. The things
// we want to handle here are instructions like `XOR A, A`, that can be seen as
// clearing the value of `A` and not reading it for the sake of reading it.
//
// Note: This function has an architecture-specific implementation.
extern void ConvertOperandActions(const NativeInstruction *instr,
                                  SSAOperandPack &operands);

// Get the virtual register associated with an arch operand.
//
// Note: This assumes that the arch operand is indeed a register operand!
//
// Note: This function has an architecture-specific implementation.
extern VirtualRegister GetRegister(const SSAOperand &op);

namespace {

// Returns true of we find a read register operand in the `operands` pack that
// uses the same register as `op`.
//
// Note: This assumes that `op` refers to a register operand.
static bool FindReadFromReg(const SSAOperand &op,
                            const SSAOperandPack &operands) {
  const auto op_reg = GetRegister(op);
  for (auto &related_op : operands) {
    if (&op == &related_op || !related_op.is_reg) continue;
    if (SSAOperandAction::WRITE == related_op.action) continue;
    if (GetRegister(related_op) == op_reg) return true;
  }
  return false;
}

}  // namespace

// Convert writes to register operates into read/writes if there is another
// read from the same register (that isn't a memory operand) in the current
// operand pack.
//
// The things we want to handle here are instruction's like `MOV A, A`.
//
// Note: This function is also used by `7_propagate_copies`.
bool ConvertOperandActions(SSAOperandPack &operands) {
  auto changed = false;
  for (auto &op : operands) {
    if (op.is_reg && SSAOperandAction::WRITE == op.action &&
        FindReadFromReg(op, operands)) {
      op.action = SSAOperandAction::READ_WRITE;
      changed = true;
    }
  }
  return changed;
}

// Decompose an `SSAOperandPack` containing all kinds of operands into the
// canonical format required by `SSAInstruction`.
//
// Note: This function is alos used by `7_propagate_copies`.
void AddInstructionOperands(SSAInstruction *instr, SSAOperandPack &operands) {
  for (auto &op : operands) {
    if (SSAOperandAction::WRITE == op.action) instr->defs.Append(op);
  }
  for (auto &op : operands) {
    if (SSAOperandAction::CLEARED == op.action) instr->defs.Append(op);
  }
  for (auto &op : operands) {
    if (SSAOperandAction::READ_WRITE == op.action) instr->uses.Append(op);
  }
  for (auto &op : operands) {
    if (SSAOperandAction::READ == op.action) instr->uses.Append(op);
  }
}

namespace {

// Add an `SSAOperand` to an operand pack.
static void AddSSAOperand(SSAOperandPack &operands, Operand *op) {
  auto mem_op = DynamicCast<MemoryOperand *>(op);
  auto reg_op = DynamicCast<RegisterOperand *>(op);

  // Ignore immediate operands as they are unrelated to virtual registers.
  if (!mem_op && !reg_op) {
    return;

  // Ignore all non general-purpose registers, as they cannot be scheduled with
  // virtual registers.
  } else if (reg_op) {
    if (!reg_op->Register().IsGeneralPurpose()) return;

  // Only use memory operands that contain general-purpose registers.
  } else if (mem_op) {
    if (mem_op->IsPointer()) return;

    VirtualRegister r1, r2, r3;
    auto num_gprs = 0;
    if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
      if (r1.IsGeneralPurpose()) ++num_gprs;
      if (r2.IsGeneralPurpose()) ++num_gprs;
      if (r3.IsGeneralPurpose()) ++num_gprs;
    }
    if (!num_gprs) return;  // E.g. referencing memory directly on the stack.
  }

  SSAOperand ssa_op;

  GRANARY_ASSERT(op->Ref().IsValid());
  ssa_op.operand = const_cast<arch::Operand *>(op->UnsafeExtract());
  GRANARY_ASSERT(nullptr != ssa_op.operand);

  ssa_op.is_reg = nullptr != reg_op;

  // Figure out the action that should be associated with all dependencies
  // of this operand. Later we'll also do minor post-processing of all
  // operands that will potentially convert some `WRITE`s into `READ_WRITE`s
  // where the same register appears as both a read and write operand.
  // Importantly, we could have the same register is a write reg, and a read
  // mem, and in that case we wouldn't perform any such conversions.
  if (mem_op) {
    ssa_op.action = SSAOperandAction::READ;
  } else if (op->IsConditionalWrite() || op->IsReadWrite()) {
    ssa_op.action = SSAOperandAction::READ_WRITE;
  } else if (op->IsWrite()) {
    if (reg_op->Register().PreservesBytesOnWrite()) {
      ssa_op.action = SSAOperandAction::READ_WRITE;
    } else {
      ssa_op.action = SSAOperandAction::WRITE;
    }
  } else {
    ssa_op.action = SSAOperandAction::READ;
  }

  operands.Append(ssa_op);
}

// Create an `SSAInstruction` for the operands associated with some
// `NativeInstruction`. We add the operands to the instruction in a specific
// order for later convenience.
static SSAInstruction *BuildSSAInstr(SSAOperandPack &operands) {
  if (!operands.Size()) {
    return nullptr;
  }
  auto instr = new SSAInstruction;
  AddInstructionOperands(instr, operands);
  return instr;
}

// Create the `SSAOperandPack`s for every native instruction in `SSAFragment`
// fragments.
static void CreateSSAInstructions(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<SSAFragment *>(frag)) {
      for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          SSAOperandPack operands;
          ninstr->ForEachOperand([&] (Operand *op) {
            AddSSAOperand(operands, op);
          });
          ConvertOperandActions(operands);  // Generic.
          ConvertOperandActions(ninstr, operands);  // Arch-specific.
          SetMetaData(ninstr, BuildSSAInstr(operands));
        }
      }
    }
  }
}

// For every `SSAFragment` that targets a non `SSAFragment` successor, add the
// live GPRs on exit from the `SSAFragment` as initial `SSAControlPhiNode`s to
// the fragment's `ssa.entry_nodes` map.
static void InitEntryNodesFromLiveExitRegs(FragmentList *frags) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      auto is_exit = false;
      for (auto succ : frag->successors) {
        if (succ && !IsA<SSAFragment *>(succ)) {
          is_exit = true;
          break;
        }
      }
      if (is_exit) {
        for (auto reg : ssa_frag->regs.live_on_exit) {
          auto &node(ssa_frag->ssa.entry_nodes[reg]);
          if (!node) {
            node = new SSAControlPhiNode(ssa_frag, reg);
          }
        }
      }
    }
  }
}

// Perform local value numbering for definitions.
static void LVNDefs(SSAFragment *frag, NativeInstruction *instr,
                    SSAInstruction *ssa_instr) {
  // Update any existing nodes on writes to be `SSARegisterNode`s, and share
  // the register nodes with `CLEAR`ed operands.
  for (auto &op : ssa_instr->defs) {
    auto reg = GetRegister(op);
    auto &node(frag->ssa.entry_nodes[reg]);
    if (SSAOperandAction::WRITE == op.action) {

      // Some later (in this fragment) instruction reads from this register,
      // and so it created an `SSAControlPhiNode` for that use so that it could
      // signal that a concrete definition of that use was missing. We now have
      // a concrete definition, so convert the existing memory into a register
      // node.
      if (node) {
        GRANARY_ASSERT(IsA<SSAControlPhiNode *>(node));
        node = new (node) SSARegisterNode(frag, instr, reg);

      // No use (in the current fragment) depends on this register, but when
      // we later to global value numbering, we might need to forward-propagate
      // this definition to a use in a successor fragment.
      } else {
        node = new SSARegisterNode(frag, instr, reg);
      }
    } else {  // `SSAOperandAction::CLEAR`.
      GRANARY_ASSERT(IsA<SSARegisterNode *>(node));
    }

    GRANARY_ASSERT(!op.nodes.Size());
    op.nodes.Append(node);  // Single dependency.
  }

  // Clear out the written `SSARegisterNode`s, as we don't want them to be
  // inherited by other instructions.
  for (auto &op : ssa_instr->defs) {
    if (SSAOperandAction::WRITE == op.action) {
      frag->ssa.entry_nodes.Remove(GetRegister(op));
    }
  }
}

// Perform local value numbering for uses.
static void LVNUses(SSAFragment *frag, SSAInstruction *ssa_instr) {
  for (auto &op : ssa_instr->uses) {
    if (SSAOperandAction::READ_WRITE == op.action) {  // Read-only, must be reg.
      GRANARY_ASSERT(op.is_reg);
      auto reg = GetRegister(op);
      auto &node(frag->ssa.entry_nodes[reg]);

      // We're doing a read/write, so while we are making a new definition, it
      // will need to depend on some as-of-yet to be determined definition.
      auto new_node = new SSAControlPhiNode(frag, reg);

      // Some previous instruction (in the current fragment) uses this register,
      // and so created a placeholder version of the register to be filled in
      // later. Now we've got a definition, so we can replace the existing
      // control-PHI with a data-PHI.
      if (node) {
        GRANARY_ASSERT(IsA<SSAControlPhiNode *>(node));
        op.nodes.Append(new (node) SSADataPhiNode(frag, new_node));

      // No instructions (in the current fragment) that follow `instr` use the
      // register `reg`, but later when we do GVN, we might need to propagate
      // this definition to a successor.
      } else {
        op.nodes.Append(new SSADataPhiNode(frag, new_node));
      }

      GRANARY_ASSERT(1U == op.nodes.Size());
      node = new_node;

    } else {  // `SSAOperandAction::READ`, register or memory operand.
      VirtualRegister regs[3];
      if (op.is_reg) {
        regs[0] = GetRegister(op);
      } else {
        MemoryOperand mem_op(op.operand);
        mem_op.CountMatchedRegisters({&(regs[0]), &(regs[1]), &(regs[2])});
      }

      // Treat register and memory operands uniformly. For each read register,
      // add a control-dependency on the register to signal that definition of
      // the register is presently missing and thus might be inherited from
      // a predecessor fragment.
      for (auto reg : regs) {
        if (reg.IsGeneralPurpose()) {
          auto &node(frag->ssa.entry_nodes[reg]);
          if (!node) {
            node = new SSAControlPhiNode(frag, reg);
          }
          op.nodes.Append(node);
        }
      }
    }
  }
}

// Add the missing definitions as annotation instructions. This is so that all
// nodes are owned by *some* fragment, which simplifies later memory
// reclamation.
static void AddMissingDefsAsAnnotations(SSAFragment *frag) {
  for (auto node : frag->ssa.entry_nodes.Values()) {
    GRANARY_ASSERT(IsA<SSAControlPhiNode *>(node));
    frag->instrs.Prepend(new AnnotationInstruction(IA_SSA_NODE_DEF, node));
  }
}

// Perform a local-value numbering of all general-purpose register uses
// within an `SSAFragment` fragment.
static void LocalValueNumbering(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          if (auto ssa_instr = GetMetaData<SSAInstruction *>(instr)) {
            LVNDefs(ssa_frag, ninstr, ssa_instr);
            LVNUses(ssa_frag, ssa_instr);
          }
        }
      }
      AddMissingDefsAsAnnotations(ssa_frag);
    }
  }
}

// Returns the last `SSANode` defined within the fragment `frag` that
// defines the register `reg`.
static SSANode *FindDefForUse(SSAFragment *frag, VirtualRegister reg) {
  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto node = DefinedNodeForReg(instr, reg)) {
      return node;
    }
  }
  return nullptr;
}

// Back-propagate the entry nodes of `succ` into the exit nodes of `frag`, then
// update the entry nodes of `succ` if necessary.
static bool BackPropagateEntryDefs(SSAFragment *frag, SSAFragment *succ) {
  auto changed = false;
  for (auto succ_node : succ->ssa.entry_nodes.Values()) {
    auto reg = succ_node->reg;
    GRANARY_ASSERT(reg.IsGeneralPurpose());

    // Already inherited, either in a previous step, or by a different
    // successor of `frag` that we've already visited.
    auto &exit_node(frag->ssa.exit_nodes[reg]);
    if (exit_node) {
      exit_node->storage.Union(exit_node, succ_node);
      continue;
    }

    // Defined in `frag`, or used in `frag` but not defined.
    if (auto node = FindDefForUse(frag, reg)) {
      node->storage.Union(node, succ_node);
      exit_node = node;
      continue;
    }

    // `FindDefForUse` didn't find it, so it means that `reg` was neither
    // defined nor used in `frag`. We should similarly not find it in
    // `entry_nodes`, because then that would imply a bug where something that
    // should be both in the exit and entry nodes is present in the entry but
    // not the exit nodes (which would have been caught by a check above).
    auto &entry_node(frag->ssa.entry_nodes[reg]);
    GRANARY_ASSERT(nullptr == entry_node);

    // `frag` (predecessor of `succ`) doesn't define or use `reg`, so inherit
    // the node directly and pass it up through the `entry_nodes` as well.
    entry_node = succ_node;
    exit_node = succ_node;

    // Make a note that `entry_nodes` has changed, which could further change
    // other fragments.
    changed = true;
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

// Connects all control-PHI nodes between predecessors and successors.
static void ConnectControlPhiNodes(SSAFragment *pred, SSAFragment *succ) {
  for (auto succ_entry_node : succ->ssa.entry_nodes.Values()) {
    if (auto succ_phi = DynamicCast<SSAControlPhiNode *>(succ_entry_node)) {
      auto pred_exit_node = pred->ssa.exit_nodes[succ_phi->reg];
      succ_phi->AddOperand(pred_exit_node);
    }
  }
}

// Connects all control-PHI nodes between predecessors and successors.
static void ConnectControlPhiNodes(FragmentList *frags) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      for (auto succ : frag->successors) {
        if (auto ssa_succ = DynamicCast<SSAFragment *>(succ)) {
          ConnectControlPhiNodes(ssa_frag, ssa_succ);
        }
      }
    }
  }
}

// Attempt to trivialize as many `SSAControlPhiNode`s as possible into either
// `SSAAliasNode`s or into `SSARegisterNode`s.
static void SimplifyControlPhiNodes(FragmentList *frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : ReverseFragmentListIterator(frags)) {
      if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
        for (auto entry_node : ssa_frag->ssa.entry_nodes.Values()) {
          auto phi_entry_node = DynamicCast<SSAControlPhiNode *>(entry_node);
          if (phi_entry_node) {
            changed = phi_entry_node->UnsafeTryTrivialize() || changed;
          }
        }
      }
    }
  }
}

static void AddCompensationRegKills(CodeFragment *frag) {
  for (auto node : frag->ssa.entry_nodes.Values()) {
    frag->instrs.Append(new AnnotationInstruction(IA_SSA_NODE_UNDEF, node));
  }
}

// If a *virtual* register R is live on exit in `pred` but not live on entry
// in `succ` then add a compensating fragment between `pred` and `succ` that
// contains R as as live on entry, and explicitly kills those variables using
// special annotation instructions.
//
// Note: `succ` is passed by reference so that we can update the correct
//       successor entry in `pred` more easily.
static void AddCompensatingFragment(FragmentList *frags, SSAFragment *pred,
                                    Fragment *&succ) {
  auto comp = new CodeFragment;
  for (auto &exit_node : pred->ssa.exit_nodes) {
    if (exit_node.key.IsVirtual()) {
      comp->ssa.entry_nodes[exit_node.key] = exit_node.value;
    }
  }
  if (auto ssa_succ = DynamicCast<SSAFragment *>(succ)) {
    for (auto entry_reg : ssa_succ->ssa.entry_nodes.Keys()) {
      if (entry_reg.IsVirtual()) {
        comp->ssa.entry_nodes.Remove(entry_reg);
      }
    }
  }
  // No "leaky" definitions to compensate for.
  if (!comp->ssa.entry_nodes.Size()) {
    delete comp;
    return;
  }

  // Make `comp` appear to be yet another `CodeFragment` to all future
  // assembly passes.
  if (auto code_pred = DynamicCast<CodeFragment *>(pred)) {
    comp->attr.block_meta = code_pred->attr.block_meta;
    comp->stack.is_checked = true;
    comp->stack.is_valid = code_pred->stack.is_valid;
  }

  comp->attr.is_compensation_code = true;
  comp->partition.Union(reinterpret_cast<Fragment *>(comp),
                        reinterpret_cast<Fragment *>(pred));
  comp->regs.live_on_entry = pred->regs.live_on_exit;
  comp->regs.live_on_exit = pred->regs.live_on_exit;

  // Chain it into the control-flow.
  comp->successors[0] = succ;
  succ = comp;

  frags->InsertAfter(pred, comp);  // Chain it into the fragment list.

  AddCompensationRegKills(comp);
}

#ifdef GRANARY_DEBUG
// Asserts that there are nodes (of any type) on entry to frag that are
// associated with virtual registers. This can happen in the case where some
// instrumentation reads from a virtual register before writing to it. We
// handle some architecture-specific special cases like `XOR A, A` on x86
// when buildig up the `SSAInstruction`s and by using the
// `SSAOperandAction::CLEARED` action.
static void CheckForUndefinedVirtualRegs(SSAFragment *frag) {
  for (auto reg : frag->ssa.entry_nodes.Keys()) {
    GRANARY_ASSERT(!reg.IsVirtual());
  }
}
#endif

// Goes and adds "compensating" fragments. The idea here is that if we have
// an edge between a predecessor fragment P and its successor S, and some
// register R is live on exit from P, but is not live on entry to S, then
// really it is killed in the transition from P to S. We need to explicitly
// represent this "death" (for later allocation purposes) by introducing
// a dummy compensating fragment.
static void AddCompensatingFragments(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (code_frag->attr.is_compensation_code) {
        continue;
      }
    }
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      for (auto &succ : ssa_frag->successors) {
        if (succ) {
          AddCompensatingFragment(frags, ssa_frag, succ);
        }
      }

    } else if (IsA<PartitionEntryFragment *>(frag)) {
#ifdef GRANARY_DEBUG
      for (auto succ : frag->successors) {
        if (auto ssa_succ = DynamicCast<SSAFragment *>(succ)) {
          CheckForUndefinedVirtualRegs(ssa_succ);
        }
      }
#endif
    }
  }
}

}  // namespace

// Build a graph for the SSA definitions associated with the fragments.
void TrackSSAVars(FragmentList * const frags) {
  CreateSSAInstructions(frags);
  InitEntryNodesFromLiveExitRegs(frags);
  LocalValueNumbering(frags);
  BackPropagateEntryDefs(frags);
  ConnectControlPhiNodes(frags);
  SimplifyControlPhiNodes(frags);
  AddCompensatingFragments(frags);
}

}  // namespace granary

