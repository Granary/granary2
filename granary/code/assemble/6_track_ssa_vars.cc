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
void ConvertOperandActions(const NativeInstruction *instr,
                           SSAOperandPack &operands);

// Get the virtual register associated with an arch operand.
//
// Note: This assumes that the arch operand is indeed a register operand!
//
// Note: This function has an architecture-specific implementation.
VirtualRegister GetRegister(const SSAOperand &op);

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
  } else if (reg_op && reg_op->Register().IsGeneralPurpose()) {
    return;

  // Return pointer memory operands (i.e. absolute memory addresses), as they
  // contain no general-purpose registers.
  } else if (mem_op && mem_op->IsPointer()) {
    return;
  }

  SSAOperand ssa_op;
  ssa_op.operand = const_cast<arch::Operand *>(op->UnsafeExtract());
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
    if (reg_op && reg_op->Register().PreservesBytesOnWrite()) {
      ssa_op.action = SSAOperandAction::READ_WRITE;
    } else {
      ssa_op.action = SSAOperandAction::WRITE;
    }
  } else {
    ssa_op.action = SSAOperandAction::READ;
  }

  operands.Append(ssa_op);
}

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

// Convert writes to register operates into read/writes if there is another
// read from the same register (that isn't a memory operand) in the current
// operand pack.
//
// The things we want to handle here are instruction's like `MOV A, A`.
static void ConvertOperandActions(SSAOperandPack &operands) {
  for (auto &op : operands) {
    if (op.is_reg && SSAOperandAction::WRITE == op.action &&
        FindReadFromReg(op, operands)) {
      op.action = SSAOperandAction::READ_WRITE;
    }
  }
}

// Create an `SSAInstruction` for the operands associated with some
// `NativeInstruction`. We add the operands to the instruction in a specific
// order for later convenience.
static SSAInstruction *BuildSSAInstr(SSAOperandPack &operands) {
  if (!operands.Size()) {
    return nullptr;
  }
  auto instr = new SSAInstruction;
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
static void LVNUses(SSAFragment *frag, NativeInstruction *instr,
                    SSAInstruction *ssa_instr) {


  for (auto &op : ssa_instr->uses) {
    if (SSAOperandAction::READ_WRITE == op.action) {  // Read-only, must be reg.
      GRANARY_ASSERT(op.is_reg);
      auto reg = GetRegister(op);
      auto &node(frag->ssa.entry_nodes[reg]);

      // We're doing a read/write, so while we are making a new definition, it
      // will need to depend on some as-of-yet to be determined definition.
      auto new_node = new SSAControlPhiNode(frag);

      // Some previous instruction (in the current fragment) uses this register,
      // and so created a placeholder version of the register to be filled in
      // later. Now we've got a definition, so we can replace the existing
      // control-PHI with a data-PHI.
      if (node) {
        GRANARY_ASSERT(IsA<SSAControlPhiNode *>(node));
        op.nodes.Append(new (node) SSADataPhiNode(frag, instr, new_node));

      // No instructions (in the current fragment) that follow `instr` use the
      // register `reg`, but later when we do GVN, we might need to propagate
      // this definition to a successor.
      } else {
        op.nodes.Append(new SSADataPhiNode(frag, instr, new_node));
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
            node = new SSAControlPhiNode(frag);
          }
          op.nodes.Append(node);
        }
      }
    }
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
            LVNUses(ssa_frag, ninstr, ssa_instr);
          }
        }
      }
    }
  }
}

}  // namespace

// Build a graph for the SSA definitions associated with the fragments.
void TrackSSAVars(FragmentList * const frags) {
  CreateSSAInstructions(frags);
  LocalValueNumbering(frags);
}

}  // namespace granary

