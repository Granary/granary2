/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/6_track_ssa_vars.h"
#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/util.h"

namespace granary {
namespace {

// Initialize the fragment list for tracking SSA variables. This does a few
// important things:
//    1.  Creates SSA variable trackers for non partition entry/exit fragments.
//    2.  Clears the meta-data associated with native instructions. Importantly,
//        meta-data associated with other instructions is left untouched. For
//        example, the meta-data of a `LabelInstruction` will point to the
//        `Fragment` associated with that label. This is used later on.
//    3.  Nulls out the SSA variable trackers for entry/exit fragments. The
//        effect of this is that SSA variable tracking is partition-local.
static void InitAnalysis(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (FRAG_KIND_PARTITION_ENTRY != frag->kind &&
        FRAG_KIND_PARTITION_EXIT != frag->kind) {
      frag->ssa_vars = new SSAVariableTracker;
    } else {
      frag->ssa_vars = nullptr;
    }
    for (auto instr : ForwardInstructionIterator(frag->first)) {
      if (IsA<NativeInstruction *>(instr)) {
        ClearMetaData(instr);
      }
    }
  }
}

// Returns true if an instruction reads from a particular register.
static bool InstructionReadsReg(NativeInstruction *instr,
                                const VirtualRegister reg) {
  auto ret = false;
  instr->ForEachOperand([&] (Operand *op) {
    if (!ret && op->IsRead()) {
      if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
        ret = reg_op->Register() == reg;
      } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
        VirtualRegister r1, r2, r3;
        if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
          ret = reg == r1 || reg == r2 || reg == r3;
        }
      }
    }
  });
  return ret;
}

// Create a new variable definition.
static void AddDef(SSAVariableTracker *vars, const RegisterOperand &op,
                   NativeInstruction *instr) {
  auto reg = op.Register();
  if (reg.IsGeneralPurpose()) {
    if (op.IsRead() || op.IsConditionalWrite() || !op.IsExplicit() ||
        reg.PreservesBytesOnWrite() || InstructionReadsReg(instr, reg)) {

      // Note: A given instruction can have multiple inheriting definitions.
      //       For example, `XADD_GPRv_GPRv` on x86-64.
      //
      // Note: We throw implicit operands into the mix as there can be many
      //       implicit operand written by a single instruction, and reasoning
      //       about their values is not useful (e.g. `POPAD` on x86). In the
      //       case of implicit output operands, we assume that explicit
      //       operands come before implicit operands.
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
static void DeclareUse(SSAVariableTracker *vars, VirtualRegister reg) {
  if (reg.IsGeneralPurpose()) {
    vars->DeclareUse(reg);
  }
}

// Declare all uses of virtual registers. This ensures that matching missing
// definitions are present in the SSA variable table.
static void AddUses(SSAVariableTracker *vars, NativeInstruction *instr) {
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
  auto ssa_vars = frag->ssa_vars;
  for (auto instr : BackwardInstructionIterator(frag->last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      RegisterOperand r1, r2;
      auto count = ninstr->CountMatchedOperands(WriteTo(r1), WriteTo(r2));
      if (1 <= count) AddDef(ssa_vars, r1, ninstr);
      if (2 <= count) AddDef(ssa_vars, r2, ninstr);
      AddUses(ssa_vars, ninstr);
    }
  }
  //ssa_vars->PromoteMissingDefinitions();
}

// Perform a local value numbering for all fragments in the control-flow
// graph.
static void LocalValueNumbering(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->ssa_vars) {
      NumberLocalValues(frag);
    }
  }
}

// Perform a single-step local value number propagation for all fragments
// within the same partition.
//
// The idea here is that if a variable is used before it's defined in `succ`,
// and if it it's not defined in `pred` (if it's used in `pred` it will be
// seen as being defined), then we need to back-propagate the use of the
// variable from `succ` and into `pred`. The goal of this back-propagation is
// that the set of live virtual regs on exit of every fragment will match the
// potential control-flow, and that we can then connect outgoing live regs with
// incoming live regs (via PHIs), then simplify the PHIs.
static bool BackPropagateSSAUses(Fragment *pred, Fragment *succ) {
  return succ &&
         pred->partition_id == succ->partition_id &&
         pred->ssa_vars &&
         succ->ssa_vars &&
         pred->ssa_vars->BackPropagateMissingDefsForUses(pred, succ->ssa_vars);
}

// Convert the local value numberings into partition-global value numberings.
static void PropagateLocalValueNumbers(Fragment * const frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentIterator(frags)) {
      changed = BackPropagateSSAUses(frag, frag->fall_through_target) || changed;
      changed = BackPropagateSSAUses(frag, frag->branch_target) || changed;
    }
  }
}

// Connect the PHI nodes between a predecessor and a successor.
static void ConnectPhiNodes(Fragment *pred, Fragment *succ) {
  if (succ && pred->ssa_vars && succ->ssa_vars) {
    pred->ssa_vars->AddPhiOperands(succ->ssa_vars);
  }
}

// Connect and simplify all PHI nodes.
static void ConnectPhiNodes(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    ConnectPhiNodes(frag, frag->fall_through_target);
    ConnectPhiNodes(frag, frag->branch_target);
  }
  for (auto frag : FragmentIterator(frags)) {
    if (frag->ssa_vars) {
      frag->ssa_vars->SimplifyPhiNodes();
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
void TrackSSAVars(Fragment * const frags) {
  InitAnalysis(frags);
  LocalValueNumbering(frags);
  PropagateLocalValueNumbers(frags);
  ConnectPhiNodes(frags);
}

}  // namespace granary
