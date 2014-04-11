/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"
#include "granary/cfg/operand.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/util.h"

namespace granary {

// Returns true if this instruction is a copy instruction.
//
// Note: This has an architecture-specific implementation.
bool IsCopyInstruction(const NativeInstruction *instr);

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
static void FindDefsForUses(Instruction *def_instr, SSAVariableTable *defs) {
  auto frag = ContainingFragment(def_instr);
  frag->ssa_vars->CopyEntryDefinitions(defs);
  for (auto instr : ForwardInstructionIterator(frag->first)) {
    if (instr == def_instr) {
      break;
    } else if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      UpdateDefsFromInstr(defs, ninstr);
    }
  }
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

// Returns a valid virtual register if it looks like the use of the register
// can be replaced by a use of another register.
static VirtualRegister RegisterToPropagate(SSAVariableTable *vars,
                                           NativeInstruction *instr,
                                           VirtualRegister source_reg,
                                           VirtualRegister dest_reg) {
  if (source_reg.IsGeneralPurpose() &&
      source_reg.BitWidth() == dest_reg.BitWidth()) {

    SSAVariableTable source_vars;
    FindDefsForUses(instr, &source_vars);

    // Make sure that the same definition of the register being copied
    // reaches both the copy instruction, and the instruction to which we
    // want to propagate the copy.
    if (DefinitionOf(*source_vars.Find(source_reg)) ==
        DefinitionOf(*vars->Find(source_reg))) {
      return source_reg;
    }
  }
  return VirtualRegister();
}

// Perform a register-to-register copy or a trivial effective address to
// register copy propagation.
static void CopyPropagate(SSAVariableTable *vars, RegisterOperand *dest,
                          VirtualRegister reg) {
  if (auto instr = GetCopyInstruction(vars, reg)) {
    RegisterOperand source;
    MemoryOperand source_eff_addr;
    VirtualRegister source_reg;
    if (instr->MatchOperands(ReadOnlyFrom(source))) {
      source_reg = source.Register();
    } else if (instr->MatchOperands(ReadOnlyFrom(source_eff_addr)) &&
               source_eff_addr.IsEffectiveAddress()) {
      source_eff_addr.MatchRegister(source_reg);
    }
    source_reg = RegisterToPropagate(vars, instr, source_reg, reg);
    if (source_reg.IsValid()) {
      dest->Ref().ReplaceWith(source);
    }
  }
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
        MemoryOperand source(source_reg, dest->Width());
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

