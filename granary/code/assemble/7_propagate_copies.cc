/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/breakpoint.h"
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {

// Get the virtual register associated with an arch operand.
//
// Note: This assumes that the arch operand is indeed a register operand!
//
// Note: This function has an architecture-specific implementation.
extern VirtualRegister GetRegister(const SSAOperand &op);

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
//
// Note: This has an architecture-specific implementation.
extern SSAOperand *GetCopiedOperand(const NativeInstruction *instr);

// Returns true if we can propagate the register `source` into the place of the
// register `dest`.
//
// Note: This has an architecture-specific implementation.
extern bool CanPropagate(VirtualRegister source, VirtualRegister dest);

// Convert writes to register operates into read/writes if there is another
// read from the same register (that isn't a memory operand) in the current
// operand pack.
//
// The things we want to handle here are instruction's like `MOV A, A`.
//
// Note: This function is defined in `6_track_ssa_vars`.
extern bool ConvertOperandActions(SSAOperandPack &operands);

// Performs architecture-specific conversion of `SSAOperand` actions. The things
// we want to handle here are instructions like `XOR A, A`, that can be seen as
// clearing the value of `A` and not reading it for the sake of reading it.
//
// Note: This function has an architecture-specific implementation.
extern void ConvertOperandActions(const NativeInstruction *instr,
                                  SSAOperandPack &operands);

// Decompose an `SSAOperandPack` containing all kinds of operands into the
// canonical format required by `SSAInstruction`.
//
// Note: This function is defined in `6_track_ssa_vars`.
void AddInstructionOperands(SSAInstruction *instr, SSAOperandPack &operands);

namespace {

// Represents a potentially copy-able operand.
struct CopyOperand {
  inline CopyOperand(void)
      : node(nullptr),
        operand(nullptr) {}

  CopyOperand &operator=(const CopyOperand &) = default;

  SSANode *node;
  SSAOperand *operand;
};

// The set of reaching definitions that
typedef TinyMap<VirtualRegister, CopyOperand,
                arch::NUM_GENERAL_PURPOSE_REGISTERS> ReachingDefinintions;

// Updates the definition set with a node. Here we handle the case where the
// node is in some unknown location, and so we need to be fairly general here.
static void UpdateAnnotationDefs(ReachingDefinintions &defs, SSANode *node) {
  node = UnaliasedNode(node);

  auto &copy_op(defs[node->reg]);
  copy_op.node = node;
  copy_op.operand = nullptr;

  if (auto reg_node = DynamicCast<SSARegisterNode *>(node)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(reg_node->instr)) {
      if (auto copied_op = GetCopiedOperand(ninstr)) {
        copy_op.operand = copied_op;
      }
    }
  }
}

// Remove all non-copied definitions from a set of reaching definitions.
static void UpdateInstructionDefs(ReachingDefinintions &defs,
                                  SSAInstruction *instr) {
  for (const auto &op : instr->defs) {
    if (SSAOperandAction::WRITE == op.action) {
      auto &copy_op(defs[GetRegister(op)]);
      copy_op.node = op.nodes[0];
      copy_op.operand = nullptr;
    } else {
      break;
    }
  }
  for (const auto &op : instr->defs) {
    if (SSAOperandAction::READ_WRITE == op.action) {
      auto &copy_op(defs[GetRegister(op)]);
      copy_op.node = op.nodes[0];
      copy_op.operand = nullptr;
    } else {
      break;
    }
  }
}

// Updates the reaching definitions `defs` by either adding or removing
// definitions made by `instr`.
static void UpdateDefs(ReachingDefinintions &defs, Instruction *instr) {
  // Inherit this definition from a predecessor fragment.
  if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
    if (IA_SSA_NODE_DEF == ainstr->annotation) {
      UpdateAnnotationDefs(defs, ainstr->GetData<SSANode *>());
    }
  } else if (auto ninstr = DynamicCast<const NativeInstruction *>(instr)) {
    auto ssa_instr = GetMetaData<SSAInstruction *>(ninstr);
    if (!ssa_instr) {
      return;
    }
    if (auto copied_op = GetCopiedOperand(ninstr)) {
      /*
      VirtualRegister copied_reg;
      if (copied_op->is_reg) {  // Register.
        copied_reg = GetRegister(*copied_op);
      } else {  // Effective address, encoded as a memory operand.
        MemoryOperand effective_address(copied_op->operand);
        effective_address.MatchRegister(copied_reg);
      }
      if (copied_reg.IsValid()) {

        return;
      }*/

      auto &copy_op(defs[ssa_instr->defs[0].nodes[0]->reg]);
      copy_op.node = copied_op->nodes[0];
      copy_op.operand = copied_op;

    } else {
      UpdateInstructionDefs(defs, ssa_instr);
    }
  }
}

// Perform a register-to-register copy or a trivial effective address to
// register copy propagation.
static bool CopyPropagate(ReachingDefinintions &defs, RegisterOperand &dest) {
  auto dest_reg = dest.Register();
  auto &dest_op(defs[dest_reg]);
  if (dest_op.operand) {
    VirtualRegister source_reg;
    SSANode *source_node(nullptr);
    if (dest_op.operand->is_reg) {
      RegisterOperand source(dest_op.operand->operand);
      source_reg = source.Register();
      source_node = dest_op.node;
    } else {
      MemoryOperand source(dest_op.operand->operand);
      if (source.IsEffectiveAddress()) {
        if (source.MatchRegister(source_reg)) {
          auto reaching_source_reg_node = UnaliasedNode(defs[source_reg].node);
          auto orig_source_reg_node = UnaliasedNode(dest_op.operand->nodes[0]);
          if (reaching_source_reg_node == orig_source_reg_node) {
            source_node = reaching_source_reg_node;
          }
        }
      }
    }
    if (source_node && source_reg.IsValid() &&
        CanPropagate(source_reg, dest_reg)) {

      RegisterOperand copied_reg_op(source_reg);
      dest.Ref().ReplaceWith(copied_reg_op);

      dest_op.operand->is_reg = true;
      dest_op.operand->nodes.Clear();
      dest_op.operand->nodes.Append(source_node);

      return true;
    }
  }
  return false;
}

// Returns `true` if copy-propagation into this instruction changed any arch_operand.
static bool UpdateUses(ReachingDefinintions &defs, SSAInstruction *instr) {
  auto changed = false;
  for (auto &used_op : instr->uses) {
    if (SSAOperandAction::READ != used_op.action) {
      continue;
    }
    if (used_op.is_reg) {
      RegisterOperand reg_op(used_op.operand);
      changed = (reg_op.IsExplicit() && CopyPropagate(defs, reg_op)) || changed;
    } else {
      //MemoryOperand mem_op(used_op.operand);
    }
  }
  return changed;
}

// When we do a copy propagation, we might break an invariant in the
// `SSAInstruction` data structure, so we need to go and fix it.
static void FixInstruction(NativeInstruction *ninstr,
                           SSAInstruction *ssa_instr) {
  SSAOperandPack operands(ssa_instr->defs);
  for (auto use : ssa_instr->uses) {
    operands.Append(use);
  }
  if (ConvertOperandActions(operands)) {  // Generic.
    ConvertOperandActions(ninstr, operands);  // Arch-specific.
    ssa_instr->defs.Clear();
    ssa_instr->uses.Clear();
    AddInstructionOperands(ssa_instr, operands);
  }
}

}  // namespace

// Perform the following kinds of copy-propagation.
//    1) register-to-register
//    2) register-to-(memory operand)
//    3) (effective address)-to-(memory arch_operand)
void PropagateRegisterCopies(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (code_frag->attr.is_compensation_code) {
        continue;
      }
      ReachingDefinintions defs;
      for (auto instr : InstructionListIterator(code_frag->instrs)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          if (auto ssa_instr = GetMetaData<SSAInstruction *>(ninstr)) {
            if (UpdateUses(defs, ssa_instr)) {
              FixInstruction(ninstr, ssa_instr);
            }
          }
        }
        UpdateDefs(defs, instr);
      }
    }
  }
}

}  // namespace granary

#if 0


#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"
#include "granary/cfg/operand.h"



#include "granary/util.h"

namespace granary {



namespace {

// Get the fragment containing a particular instruction.
static Fragment *ContainingFragment(Instruction *def_instr) {
  for (auto instr : BackwardInstructionIterator(def_instr)) {
    if (IsA<LabelInstruction *>(instr)) {
      if (auto frag = GetMetaData<Fragment *>(instr)) {
        return frag;
      }
    }
  }
  return nullptr;
}

// Update the definitions in `defs` with any variables defined in a native
// instruction.
static void UpdateDefsFromInstr(SSAVariableTable *defs,
                                NativeInstruction *instr) {
  if (auto def_var = GetMetaData<SSAVariable *>(instr)) {
    while (auto def_forward = DynamicCast<SSAForward *>(def_var)) {
      *(defs->Find(def_forward->reg)) = def_forward;
      def_var = def_forward->next_instr_def;
    }
    if (def_var) {
      *(defs->Find(RegisterOf(def_var))) = DefinitionOf(def_var);
    }
  }
}

// Find the definitions of the registers used by a particular instruction.
static SSAVariable *FindDefForUse(Instruction *def_instr, VirtualRegister reg) {
  // Search for a local definition within the list of instructions
  for (auto instr : BackwardInstructionIterator(def_instr)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (auto def = DefinitionOf(ninstr, reg)) {
        return def;
      }
    }
  }
  // Search for an incoming definition from `frag`s precedeccor(s).
  auto frag = ContainingFragment(def_instr);
  return frag->ssa_vars->EntryDefinitionOf(reg);
}

// Returns a pointer to the "copy" instruction that defines `reg`. If `reg` is
// defined by a non-`SSARegister` (e.g. a PHI node), or not defined by a copy
// instruction, then return `nullptr`.
static NativeInstruction *GetCopyInstruction(SSAVariableTable *vars,
                                             VirtualRegister reg) {
  auto reg_var = DynamicCast<SSARegister *>(*vars->Find(reg));
  if (reg_var) {
    if (auto def_instr = reg_var->instr) {
      return IsCopyInstruction(def_instr) ? def_instr : nullptr;
    }
  }
  return nullptr;
}




// Perform an effective address to memory operand copy propagation.
//
// When checking an effective address, we need to verify that all general-
// purpose registers participating in the computation of the effective address
// are still defined, and have the same definitions, at the point at which we
// want to propagate them to.
//
// Note: We ignore non-general-purpose registers, e.g. x86 segment registers.
static void CopyPropagate(SSAVariableTable *vars, NativeInstruction *instr,
                          const MemoryOperand &source,
                          MemoryOperand *dest) {
  VirtualRegister r1, r2, r3;
  source.CountMatchedRegisters({&r1, &r2, &r3});
  bool can_replace = true;
  if (r1.IsGeneralPurpose()) {
    can_replace = RegisterToPropagate(vars, instr, r1, r1).IsValid();
  }
  if (can_replace && r2.IsGeneralPurpose()) {
    can_replace = RegisterToPropagate(vars, instr, r2, r2).IsValid();
  }
  if (can_replace && r3.IsGeneralPurpose()) {
    can_replace = RegisterToPropagate(vars, instr, r3, r3).IsValid();
  }
  if (can_replace) {
    dest->Ref().ReplaceWith(source);
  }
}

// Perform a address register-to-memory op or effective address-to-memory op
// copy propagation.
static void CopyPropagate(SSAVariableTable *vars, MemoryOperand *dest,
                          VirtualRegister addr) {
  if (auto instr = GetCopyInstruction(vars, addr)) {
    RegisterOperand source_addr;
    MemoryOperand source_eff_addr;

    // Address register -> dereference propagation.
    if (instr->MatchOperands(ReadOnlyFrom(source_addr))) {
      auto source_reg = RegisterToPropagate(
          vars, instr, source_addr.Register(), addr);
      if (source_reg.IsValid()) {
        MemoryOperand source(source_reg, dest->ByteWidth());
        dest->Ref().ReplaceWith(source);
      }

    // Effective address -> memory operation.
    } else if (instr->MatchOperands(ReadOnlyFrom(source_eff_addr)) &&
        source_eff_addr.IsEffectiveAddress()) {
      CopyPropagate(vars, instr, source_eff_addr, dest);
    }
  }
}

// Try to perform a copy propagation for one of the registers being used in a
// particular instruction.
static void CopyPropagate(SSAVariableTable *vars, Operand *op) {
  if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
    auto reg = reg_op->Register();
    if (reg.IsGeneralPurpose() && !reg_op->IsWrite()) {
      CopyPropagate(vars, reg_op, reg);
    }
  } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
    VirtualRegister addr;
    if (mem_op->MatchRegister(addr)) {
      CopyPropagate(vars, mem_op, addr);
    }
  }
}

// Perform copy propagation for all operands in all instructions in a given
// fragment.
static void CopyPropagate(SSAVariableTable *vars, Fragment * const frag) {
  frag->ssa_vars->CopyEntryDefinitions(vars);
  for (auto instr : ForwardInstructionIterator(frag->first)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      ninstr->ForEachOperand([=] (Operand *op) {
        if (op->IsExplicit()) {
          CopyPropagate(vars, op);
        }
      });
      UpdateDefsFromInstr(vars, ninstr);
    }
  }
}

}  // namespace

// Schedule virtual registers to either physical registers or to stack/TLS
// slots.
void PropagateRegisterCopies(Fragment * const frags) {
  SSAVariableTable vars;
  // Single-step copy propagation.
  for (auto frag : FragmentIterator(frags)) {
    if (frag->ssa_vars) {
      CopyPropagate(&vars, frag);
    }
  }
}

}  // namespace granary
#endif
