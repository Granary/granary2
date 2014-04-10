/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/7_convert_to_ssa.h"
#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/util.h"

namespace granary {
namespace {

static void InitAnalysis(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    frag->num_predecessors = 0;
    if (FRAG_KIND_PARTITION_ENTRY != frag->kind &&
        FRAG_KIND_PARTITION_EXIT != frag->kind) {
      frag->vars = new SSAVariableTable;
    } else {
      frag->vars = nullptr;
    }
    for (auto instr : ForwardInstructionIterator(frag->first)) {
      ClearMetaData(instr);
    }
  }
  for (auto frag : FragmentIterator(frags)) {
    if (frag->fall_through_target) {
      frag->fall_through_target->num_predecessors++;
    }
    if (frag->branch_target) {
      frag->branch_target->num_predecessors++;
    }
  }
}

// Create a new variable definition.
static void AddDef(SSAVariableTable *vars, const RegisterOperand &op,
                   NativeInstruction *instr) {
  auto reg = op.Register();
  if (reg.IsGeneralPurpose() && op.IsExplicit()) {
    if (op.IsRead() || op.IsConditionalWrite()) {

      // Note: A given instruction can have multiple inheriting definitions.
      //       For example, `XADD_GPRv_GPRv` on x86-64.
      auto def = vars->AddInheritingDefinition(
          reg, GetMetaData<SSAVariable *>(instr));
      SetMetaData(instr, def);
    } else {
      SetMetaData(instr, vars->AddSimpleDefinition(reg, instr));
    }
  }
}

// Declare that the virtual register `reg` is used within the SSA variable
// table `vars`.
static void DeclareUse(SSAVariableTable *vars, VirtualRegister reg) {
  if (reg.IsGeneralPurpose()) {
    vars->DeclareUse(reg);
  }
}

// Declare all uses of virtual registers. This ensures that matching missing
// definitions are present in the SSA variable table.
static void AddUses(SSAVariableTable *vars, NativeInstruction *instr) {
  instr->ForEachOperand([=] (Operand *op) {
    if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
      if (!reg_op->IsWrite() && reg_op->IsExplicit()) {
        DeclareUse(vars, reg_op->Register());
      }
    } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
      VirtualRegister r1, r2, r3;
      if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
        DeclareUse(vars, r1);
        DeclareUse(vars, r2);
        DeclareUse(vars, r3);
      }
    }
  });
}

// Create a local value numbering of the definitions and uses within the
// instructions of a fragment. This visits the instructions in reverse order
// and adds definitions and then declares uses.
static void NumberLocalValues(Fragment * const frag) {
  for (auto instr : BackwardInstructionIterator(frag->last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      RegisterOperand r1, r2;
      auto count = ninstr->CountMatchedOperands(WriteTo(r1), WriteTo(r2));
      if (1 <= count) AddDef(frag->vars, r1, ninstr);
      if (2 <= count) AddDef(frag->vars, r2, ninstr);
      AddUses(frag->vars, ninstr);
    }
  }
  frag->vars->PromoteMissingDefinitions();
}

// Perform a local value numbering for all fragments in the control-flow
// graph.
static void LocalValueNumbering(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->vars) {
      NumberLocalValues(frag);
    }
  }
}

// Perform a single-step local value number propagation for all fragments
// within the same partition.
static bool PropagateSSAVars(Fragment *pred, Fragment *succ) {
  return succ &&
         pred->partition_id == succ->partition_id &&
         pred->vars &&
         succ->vars &&
         pred->vars->PropagateMissingDefinitions(succ->vars,
                                                 succ->num_predecessors);
}

// Convert the local value numberings into partition-global value numberings.
static void PropagateLocalValueNumbers(Fragment * const frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentIterator(frags)) {
      changed = PropagateSSAVars(frag, frag->fall_through_target) || changed;
      changed = PropagateSSAVars(frag, frag->branch_target) || changed;
    }
  }
}

// Connect the PHI nodes between a predecessor and a successor.
static void ConnectPhiNodes(Fragment *pred, Fragment *succ) {
  if (succ && pred->vars && succ->vars) {
    pred->vars->AddPhiOperands(succ->vars);
  }
}

// Connect and simplify all PHI nodes.
static void ConnectPhiNodes(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    ConnectPhiNodes(frag, frag->fall_through_target);
    ConnectPhiNodes(frag, frag->branch_target);
  }
  for (auto frag : FragmentIterator(frags)) {
    if (frag->vars) {
      frag->vars->SimplifyPhiNodes();
    }
  }
}

}  // namespace

// Build a graph for the SSA definitions associated with the fragments.
//
// Note: This does not cover uses in the traditional sense. That is, we only
//       explicitly maintain SSA form for definitions, and uses that reach
//       PHI nodes. However, no information is explicitly maintained to track
//       which registers a given SSA register depends on, as that information
//       is indirectly maintained by the native instructions themselves.
void ConvertToSSA(Fragment * const frags) {
  InitAnalysis(frags);
  LocalValueNumbering(frags);
  PropagateLocalValueNumbers(frags);
  ConnectPhiNodes(frags);
}

}  // namespace granary
