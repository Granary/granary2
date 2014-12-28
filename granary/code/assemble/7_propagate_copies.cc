/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/fragment.h"
#include "granary/code/ssa.h"

#include "granary/code/assemble/7_propagate_copies.h"

#include "granary/breakpoint.h"
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {

// TODO(pag):  I think a simpler way to do copy propagation will be to track
//             (somehow) that a register is defined once, and then only read
//             thereafter, but never used in an RW operation.

// TODO(pag):  If we count the number of uses of a reg, then we can potentially
//             back-propagate, and then "fake" constant-propagation.

// TODO(pag): Only propagate through direct jumps?


#if 0

// Convert writes to register operates into read/writes if there is another
// read from the same register (that isn't a memory operand) in the current
// operand pack.
//
// Note: This function is defined by `6_track_ssa_vars`.
extern void ConvertOperandActions(NativeInstruction *instr);

namespace arch {

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
//
// Note: This has an architecture-specific implementation.
extern bool GetCopiedOperand(const NativeInstruction *instr,
                             SSAInstruction *ssa_instr, SSAOperand **def,
                             SSAOperand **use0, SSAOperand **use1);

// Invalidates the stack analysis property of `instr`.
extern void InvalidateStackAnalysis(NativeInstruction *instr);

// Replace the virtual register `old_reg` with the virtual register `new_reg`
// in the operand `op`.
extern bool ReplaceRegInOperand(Operand *op, VirtualRegister old_reg,
                                VirtualRegister new_reg);

// Replace a memory operand with an effective address memory operand.
extern void ReplaceMemOpWithEffectiveAddress(Operand *mem_op,
                                             const Operand *effective_addr);

}  // namespace arch
namespace {

// Represents a potentially copy-able operand.
struct RegisterValue {
  inline RegisterValue(void)
      : defined_reg_web(nullptr),
        copied_value(nullptr),
        copied_value2(nullptr) {}

  // A pointer to an operand in an `SSAInstruction` where this register is
  // defined.
  SSARegisterWeb *defined_reg_web;

  // A pointer to an operand in an `SSAInstruction` that has the value of the
  // defined register.
  SSAOperand *copied_value;

  // A pointer to the second copied value. This happens only for compound
  // effective addresses.
  SSAOperand *copied_value2;
};

// The set of reaching definitions that
typedef TinyMap<VirtualRegister, RegisterValue,
                arch::NUM_GENERAL_PURPOSE_REGISTERS> ReachingDefinintions;

// Updates the definition set with a node. Here we handle the case where the
// node is in some unknown location, and so we need to be fairly general here.
static void UpdateAnnotationDefs(ReachingDefinintions &defs, SSANode *node) {
  node = UnaliasedNode(node);

  auto &reg_value(defs[node->reg]);
  reg_value.defined_reg_web = node;

  // Always treat these as null. The idea here is that even though in some
  // cases we can do cross-fragment propagation, we won't because then we'd
  // need to actually maintain the bookkeeping in order to say that the copied
  // value is propagated to the necessary fragments. That would be complicated,
  // so we don't maintain that bookkeeping, and disallow cross-fragment
  // propagation to avoid breaking invariants assumed by the register scheduler
  // about the entry/exit defs representing all shared regs.
  reg_value.copied_value = nullptr;
}

// Remove all non-copied definitions from a set of reaching definitions.
static void UpdateInstructionDefs(ReachingDefinintions &defs,
                                  SSAInstruction *instr) {
  for (const auto &op : instr->operands) {
    if (kSSAOperandActionWrite == op.action ||
        kSSAOperandActionReadWrite == op.action) {
      auto reg = op.node->reg;
      auto &reg_value(defs[reg]);
      reg_value.defined_reg_web = op.node;
      reg_value.copied_value = nullptr;
      reg_value.copied_value2 = nullptr;
    }
  }
}

// Updates the reaching definitions `defs` by either adding or removing
// definitions made by `instr`.
static void UpdateDefs(ReachingDefinintions &defs, Instruction *instr) {
  // Inherit this definition from a predecessor fragment.
  if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
    if (kAnnotSSARegisterWebOwner == ainstr->annotation ||
        kAnnotSSARestoreRegister == ainstr->annotation) {
      UpdateAnnotationDefs(defs, GetMetaData<SSANode *>(ainstr));
    }
  } else if (auto ninstr = DynamicCast<const NativeInstruction *>(instr)) {
    auto ssa_instr = ninstr->ssa;
    if (!ssa_instr) return;

    SSAOperand *def(nullptr);
    SSAOperand *use0(nullptr);
    SSAOperand *use1(nullptr);
    if (!arch::GetCopiedOperand(ninstr, ssa_instr, &def, &use0, &use1)) {
      UpdateInstructionDefs(defs, ssa_instr);
      return;
    }

    auto defined_node = def->node;
    auto &copied_value(defs[defined_node->reg]);

    copied_value.defined_reg_web = def->node;
    copied_value.copied_value = use0;
    copied_value.copied_value2 = use1;
  }
}

// Make sure that the register that we're copying is logically the same
// register, as defined by being parse of the same node set.
static bool NodeAtValueAndAtCopyMatch(ReachingDefinintions &defs,
                                      SSANode *copied_node) {
  auto reg = copied_node->reg;

  // This is subtle: we don't pre-populate the `defs` with anything from the
  // SSA entry nodes of the frag, therefore if a definition is missing, it
  // means we haven't seen one yet, and it must be the same as the one that
  // we're looking for (again: we're making sure we don't copy *across*
  // definitions, so the absence of a definition means we can't copy across
  // one).
  if (!defs.Exists(reg)) return true;

  auto reg_node_at_instr = defs[reg].defined_reg_web;
  GRANARY_ASSERT(nullptr != reg_node_at_instr);
  return UnaliasedNode(copied_node) == UnaliasedNode(reg_node_at_instr);
}

// Perform register to register and register to base/index register
// operand copy.
static bool CopyPropagateReg(ReachingDefinintions &defs,
                             SSAOperand &dest_operand) {
  auto aop = dest_operand.operand;
  if (!aop->IsExplicit()) return false;

  auto reg = dest_operand.node->reg;
  if (!defs.Exists(reg)) return false;

  const auto &reg_value(defs[reg]);
  GRANARY_ASSERT(reg == reg_value.defined_reg_web->reg);

  auto copied_value = reg_value.copied_value;
  if (!copied_value) return false;

  // Only other possibility should be a `MEMORY_READ` representing an
  // effective address. We'll assume that base-only effective operands
  // have been converted into `READ` operands.
  if (kSSAOperandActionRead != copied_value->action) return false;

  GRANARY_ASSERT(nullptr == reg_value.copied_value2);
  GRANARY_ASSERT(copied_value->operand->IsRegister());

  // Make sure we're not copying a logically different node or reg.
  auto copied_node = copied_value->node;
  auto copied_reg = copied_node->reg;

  // This one is subtle: We're copying the stack pointer into a register (not
  // into a memory operand). If this is in a fragment where the stack is valid,
  // then we might spill to the stack, and therefore have to modify instructions
  // using the stack pointer. To make things simple, we have an invariant that
  // we can only modify base/disp memory operands.
  if (kSSAOperandActionRead == dest_operand.action &&
      copied_reg.IsStackPointer()) {
    return false;
  }

  if (reg.BitWidth() != copied_reg.BitWidth()) return false;
  if (!NodeAtValueAndAtCopyMatch(defs, copied_node)) return false;

  // Overwrite the register and node. This *should* always succeed given
  // our prior checks. If it doesn't, then there's a serious inconsistency
  // somewhere.
  if (arch::ReplaceRegInOperand(aop, reg, copied_reg)) {
    SSANode::Overwrite(dest_operand.node, copied_value->node);
    return true;
  }

  GRANARY_ASSERT(false);
  return false;
}

// E.g. a `base + displacement` compound memory operand.
static bool CopyPropagateMemOneReg(ReachingDefinintions &defs,
                                   SSAOperand &dest_operand,
                                   SSAOperand *copied_value) {
  auto aop = dest_operand.operand;
  auto copied_aop = copied_value->operand;
  GRANARY_ASSERT(copied_aop->IsCompoundMemory());
  GRANARY_ASSERT(copied_aop->IsEffectiveAddress());

  // Make sure we're not copying a logically different node.
  auto copied_node = copied_value->node;
  if (!NodeAtValueAndAtCopyMatch(defs, copied_node)) return false;

  // Overwrite the memory operand.
  ReplaceMemOpWithEffectiveAddress(aop, copied_aop);

  SSANode::Overwrite(dest_operand.node, copied_node);
  return true;
}

// E.g. a `base + index * scale + displacement` compound memory operand.
static bool CopyPropagateMemTwoReg(ReachingDefinintions &defs,
                                   SSAInstruction *instr,
                                   SSAOperand &dest_operand,
                                   SSAOperand *copied_value0,
                                   SSAOperand *copied_value1) {
  auto aop = dest_operand.operand;
  auto copied_aop = copied_value0->operand;
  GRANARY_ASSERT(copied_aop->IsCompoundMemory());
  GRANARY_ASSERT(copied_aop->IsEffectiveAddress());
  GRANARY_ASSERT(copied_aop == copied_value1->operand);

  // Make sure we're not copying a logically different node.
  auto copied_node0 = copied_value0->node;
  auto copied_node1 = copied_value1->node;
  if (!NodeAtValueAndAtCopyMatch(defs, copied_node0)) return false;
  if (!NodeAtValueAndAtCopyMatch(defs, copied_node1)) return false;

  // Overwrite the memory operand.
  ReplaceMemOpWithEffectiveAddress(aop, copied_aop);
  SSANode::Overwrite(dest_operand.node, copied_node0);

  // Need to add in a new SSAOperand for this additional register.
  SSAOperand op;
  op.action = kSSAOperandActionMemoryRead;
  op.node = copied_node1;
  op.operand = aop;
  op.state = kSSAOperandStateNode;
  instr->operands.Append(op);

  return true;
}

// Perform an effective address to base address propagation. Here, we look
// for non-compound memory operands, and try to replace them with compound
// effective addresses.
static bool CopyPropagateMem(ReachingDefinintions &defs,
                             SSAInstruction *instr,
                             SSAOperand &dest_operand) {
  auto aop = dest_operand.operand;
  if (!aop->IsExplicit()) return false;
  if (aop->IsCompoundMemory()) return false;
  if (aop->IsEffectiveAddress()) return false;

  auto reg = dest_operand.node->reg;
  if (!defs.Exists(reg)) return false;

  const auto &reg_value(defs[reg]);
  GRANARY_ASSERT(reg == reg_value.defined_reg_web->reg);

  auto copied_value0 = reg_value.copied_value;
  if (!copied_value0) return false;
  if (kSSAOperandActionMemoryRead != copied_value0->action) return false;

  auto copied_value1 = reg_value.copied_value2;
  if (!copied_value1) {
    return CopyPropagateMemOneReg(defs, dest_operand, copied_value0);

  } else {
    GRANARY_ASSERT(kSSAOperandActionMemoryRead == copied_value1->action);
    return CopyPropagateMemTwoReg(defs, instr, dest_operand,
                                  copied_value0, copied_value1);
  }
}

// Returns `true` if copy-propagation into this instruction changed any
// `arch::Operand`.
static bool UpdateUses(ReachingDefinintions &defs, SSAInstruction *instr,
                       const NativeInstruction *ninstr) {
  auto changed = false;
  SSAOperand *mem0(nullptr);
  SSAOperand *mem1(nullptr);
  for (auto &op : instr->operands) {

    // Register -> register.
    if (kSSAOperandActionRead == op.action) {
      changed = CopyPropagateReg(defs, op) || changed;

    // Register -> memory base address, register -> memory index address
    } else if (kSSAOperandActionMemoryRead == op.action) {
      changed = CopyPropagateReg(defs, op) || changed;

      // Track for later. Memory copy propagation can change the number of
      // `SSAOperand`s in `instr`, so we don't want to risk invalidating the
      // operand iterator.
      if (!mem0) mem0 = &op;
      mem1 = &op;
    }
  }

  // Perform effective address -> memory operand copy propagation.
  //
  // Note: We have an extra check that this instruction doesn't write to the
  //       stack pointer so that we don't copy propagate the following:
  //            LEA %0, [RSP]
  //            PUSH [%0]
  //       We don't want to copy-propagate that because then we wouldn't have
  //       a valid/easy slot mangling for the `PUSH`.
  if (mem0 && !ninstr->WritesToStackPointer()) {
    changed = CopyPropagateMem(defs, instr, *mem0) || changed;
    if (mem1->operand != mem0->operand) {
      changed = CopyPropagateMem(defs, instr, *mem1) || changed;
    }
  }

  return changed;
}

// Propagate copies within a single fragment.
static bool PropagateRegisterCopies(CodeFragment *frag) {
  ReachingDefinintions defs;
  auto ret = false;

  for (auto instr : InstructionListIterator(frag->instrs)) {
    auto ninstr = DynamicCast<NativeInstruction *>(instr);
    if (!ninstr) continue;

    auto ssa_instr = ninstr->ssa;
    if (!ssa_instr) continue;

    // Don't allow copy-propagation into an exceptional CFI. This
    // simplifies the late mangling of these instructions, which requires
    // case-by-case emulation, and so knowing, e.g., that the effective
    // address of a memory operand is *always* in a register is a major
    // simplification.
    if (IsA<ExceptionalControlFlowInstruction *>(ninstr)) {
      UpdateInstructionDefs(defs, ssa_instr);
      continue;
    }

    if (UpdateUses(defs, ssa_instr, ninstr)) {
      ConvertOperandActions(ninstr);
      arch::InvalidateStackAnalysis(ninstr);
      ret = true;
    }
    UpdateDefs(defs, instr);
  }
  return ret;
}

}  // namespace
#endif

// Perform the following kinds of copy-propagation.
//    1) Register -> register.
//    2) Trivial effective address -> register.
//    3) Register -> base address of memory operand.
//    4) Effective address -> memory arch_operand.
//
// Returns true if anything was done.
bool PropagateRegisterCopies(FragmentList *frags) {
  (void) frags;
  return false;
#if 0
  auto ret = false;
  for (auto frag : FragmentListIterator(frags)) {
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      if (code_frag->attr.is_compensation_code) continue;
      ret = PropagateRegisterCopies(code_frag) || ret;
    }
  }
  return ret;
#endif
}

}  // namespace granary
