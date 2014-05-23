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
struct RegisterValue {
  inline RegisterValue(void)
      : reg_node(nullptr),
        reg_value(nullptr) {}

  RegisterValue &operator=(const RegisterValue &) = default;

  // The `SSANode` associated with the value of this operand.
  SSANode *reg_node;

  // A pointer to an operand in an `SSAInstruction` where `node` is used.
  SSAOperand *reg_value;
};

// The set of reaching definitions that
typedef TinyMap<VirtualRegister, RegisterValue,
                arch::NUM_GENERAL_PURPOSE_REGISTERS> ReachingDefinintions;

// Updates the definition set with a node. Here we handle the case where the
// node is in some unknown location, and so we need to be fairly general here.
static void UpdateAnnotationDefs(ReachingDefinintions &defs, SSANode *node) {
  node = UnaliasedNode(node);

  auto &reg_value(defs[node->reg]);
  reg_value.reg_node = node;

  // Always treat these as null. The idea here is that even though in some
  // cases we can do cross-fragment propagation, we won't because then we'd
  // need to actually maintain the bookkeeping in order to say that the copied
  // value is propagated to the necessary fragments. That would be complicated,
  // so we don't maintain that bookkeeping, and disallow cross-fragment
  // propagation to avoid breaking invariants assumed by the register scheduler
  // about the entry/exit defs representing all shared regs.
  reg_value.reg_value = nullptr;
}

// Remove all non-copied definitions from a set of reaching definitions.
static void UpdateInstructionDefs(ReachingDefinintions &defs,
                                  SSAInstruction *instr) {
  for (const auto &op : instr->defs) {
    if (SSAOperandAction::WRITE == op.action) {
      auto &reg_value(defs[GetRegister(op)]);
      reg_value.reg_node = op.nodes[0];
      reg_value.reg_value = nullptr;
    } else {
      break;
    }
  }
  for (const auto &op : instr->uses) {
    if (SSAOperandAction::READ_WRITE == op.action) {
      auto &reg_value(defs[GetRegister(op)]);
      reg_value.reg_node = op.nodes[0];
      reg_value.reg_value = nullptr;
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
    if (auto ssa_instr = GetMetaData<SSAInstruction *>(ninstr)) {
      if (auto copied_value = GetCopiedOperand(ninstr)) {
        auto &reg_operand(ssa_instr->defs[0]);
        auto defined_reg = GetRegister(reg_operand);
        auto &reg_value(defs[defined_reg]);
        reg_value.reg_node = reg_operand.nodes[0];

        GRANARY_ASSERT(SSAOperandAction::INVALID != copied_value->action);
        reg_value.reg_value = copied_value;
      } else {
        UpdateInstructionDefs(defs, ssa_instr);
      }
    }
  }
}

// Perform register to register and effective address to register copy
// propagation.
static bool CopyPropagateReg(ReachingDefinintions &defs,
                             SSAOperand &dest_operand) {
  RegisterOperand dest_reg_op(dest_operand.operand);
  if (!dest_reg_op.IsExplicit()) {
    return false;
  }

  auto reg_to_replace = GetRegister(dest_operand);
  auto replacement_operand = defs[reg_to_replace];
  auto source_operand = replacement_operand.reg_value;
  if (!source_operand) {
    return false;  // Replacement operand wasn't created by a copy instruction.
  }
  VirtualRegister replacement_reg;
  if (source_operand->is_reg) {  // Register to register.
    replacement_reg = GetRegister(*source_operand);

  } else {  // Effective address to register.
    MemoryOperand effective_address(source_operand->operand);
    if (!effective_address.IsEffectiveAddress()) return false;
    if (!effective_address.MatchRegister(replacement_reg)) return false;
  }
  if (!CanPropagate(replacement_reg, reg_to_replace)) {
    return false;
  }
  auto replacement_reg_node_at_copy = source_operand->nodes[0];
  auto replacement_reg_node_at_instr = defs[replacement_reg].reg_node;
  if (UnaliasedNode(replacement_reg_node_at_copy) !=
      UnaliasedNode(replacement_reg_node_at_instr)) {
    return false;
  }

  RegisterOperand copied_reg_op(replacement_reg);
  dest_reg_op.Ref().ReplaceWith(copied_reg_op);

  // Update the nodes in the `SSAInstruction` in-place.
  dest_operand.nodes = source_operand->nodes;
  return true;
}

// Perform register-to-base address and effective address to memory operand
// copy propagation.
static bool CopyPropagateMem(ReachingDefinintions &defs,
                             SSAOperand &dest_operand) {
  MemoryOperand dest_mem_op(dest_operand.operand);
  if (!dest_mem_op.IsExplicit()) {
    return false;
  }

  // Make sure this is a memory operand that dereferences a single register,
  // and not some compound value that uses a register.
  VirtualRegister reg_to_replace;
  if (!dest_mem_op.MatchRegister(reg_to_replace) ||
      !reg_to_replace.IsGeneralPurpose() ||
      arch::ADDRESS_WIDTH_BITS != reg_to_replace.BitWidth()) {
    return false;
  }

  // Replacement operand wasn't created by a copy instruction.
  auto replacement_operand = defs[reg_to_replace].reg_value;
  if (!replacement_operand) {
    return false;
  }
  VirtualRegister mem_regs[2];

  // Register to base address of a memory operand.
  if (replacement_operand->is_reg) {
    mem_regs[0] = GetRegister(*replacement_operand);
    if (arch::ADDRESS_WIDTH_BITS != mem_regs[0].BitWidth()) {
      return false;
    }
  } else {  // Effective address to memory operand.
    MemoryOperand effective_address(replacement_operand->operand);
    if (!effective_address.IsEffectiveAddress()) return false;
    if (!effective_address.CountMatchedRegisters({&(mem_regs[0]),
                                                  &(mem_regs[1])})) {
      return false;
    }
  }
  auto i = 0;
  for (auto mem_reg : mem_regs) {
    if (!mem_reg.IsGeneralPurpose()) {
      continue;
    }
    auto mem_reg_node_at_copy = replacement_operand->nodes[i++];
    auto mem_reg_node_at_instr = defs[mem_reg].reg_node;
    GRANARY_ASSERT(nullptr != mem_reg_node_at_copy);
    GRANARY_ASSERT(mem_reg == mem_reg_node_at_copy->reg);
    GRANARY_ASSERT(mem_reg == mem_reg_node_at_instr->reg);
    if (UnaliasedNode(mem_reg_node_at_copy) !=
        UnaliasedNode(mem_reg_node_at_instr)) {
      return false;
    }
  }
  if (replacement_operand->is_reg) {
    MemoryOperand replacement_mem_op(mem_regs[0], arch::GPR_WIDTH_BYTES);
    dest_mem_op.Ref().ReplaceWith(replacement_mem_op);
  } else {
    MemoryOperand replacement_mem_op(replacement_operand->operand);
    dest_mem_op.Ref().ReplaceWith(replacement_mem_op);
  }

  // Update the nodes in the `SSAInstruction` in-place.
  dest_operand.nodes = replacement_operand->nodes;
  return true;
}

// Returns `true` if copy-propagation into this instruction changed any
// `arch::Operand`.
static bool UpdateUses(ReachingDefinintions &defs, SSAInstruction *instr) {
  auto changed = false;
  for (auto &used_op : instr->uses) {
    if (SSAOperandAction::READ != used_op.action) {
      continue;
    }
    // Register effective address -> register, and register -> register.
    if (used_op.is_reg) {
      changed = CopyPropagateReg(defs, used_op) || changed;

    // Register -> memory base address, and effective address -> memory operand.
    } else {
      changed = CopyPropagateMem(defs, used_op) || changed;
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
//    1) Register -> register.
//    2) Trivial effective address -> register.
//    3) Register -> base address of memory operand.
//    4) Effective address -> memory arch_operand.
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
