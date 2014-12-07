/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/instrument_memop.h"  // Needs to go first.

GRANARY_USING_NAMESPACE granary;

// Instrument all of the instructions in a basic block.
void MemOpInstrumentationTool::InstrumentBlock(DecodedBasicBlock *bb) {
  MemoryOperand mloc1, mloc2;
  for (auto instr : bb->AppInstructions()) {
    auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                   ReadOrWriteTo(mloc2));
    if (2 == num_matched) {
      InstrumentMemOp(bb, instr, mloc1);
      InstrumentMemOp(bb, instr, mloc2);
    } else if (1 == num_matched) {
      InstrumentMemOp(bb, instr, mloc1);
    }
  }
}

// Instrument a memory operation.
void MemOpInstrumentationTool::InstrumentMemOp(DecodedBasicBlock *bb,
                                               NativeInstruction *instr,
                                               MemoryOperand &mloc) {
  // Doesn't read from or write to memory.
  if (mloc.IsEffectiveAddress()) return;

  // Reads or writes from an absolute address, not through a register.
  VirtualRegister addr_reg;
  const void *addr_ptr(nullptr);

  if (mloc.MatchRegister(addr_reg)) {
    if (addr_reg.IsSegmentOffset()) {
      InstrumentSegMemOp(bb, instr, mloc, addr_reg);
    } else {
      RegisterOperand reg(addr_reg);
      InstrumentMemOp(bb, instr, mloc, reg);
    }
  } else if (mloc.MatchPointer(addr_ptr)) {
    InstrumentAddrMemOp(bb, instr, mloc, addr_ptr);

  } else if (mloc.IsCompound()) {
    InstrumentCompoundMemOp(bb, instr, mloc);
  }
}

// Instrument a memory operand that accesses some memory through an offset of
// a segment register. We assume that the first quadword stored in the segment
// points to the segment base address.
void MemOpInstrumentationTool::InstrumentSegMemOp(DecodedBasicBlock *bb,
                                                  NativeInstruction *instr,
                                                  MemoryOperand &mloc,
                                                  VirtualRegister seg_offs) {

  VirtualRegister seg_reg;
  GRANARY_IF_DEBUG( auto matched = ) mloc.MatchSegmentRegister(seg_reg);
  GRANARY_ASSERT(matched);

  RegisterOperand offset_op(seg_offs);
  RegisterOperand addr_reg_op(bb->AllocateVirtualRegister());
  RegisterOperand seg_reg_op(seg_reg);
  lir::InlineAssembly asm_(offset_op, addr_reg_op, seg_reg_op);
  asm_.InlineBefore(instr, "MOV r64 %1, m64 %2:[0];"
                           "LEA r64 %1, m64 [%1 + %0];"_x86_64);
  InstrumentMemOp(bb, instr, mloc, addr_reg_op);
}

// Instrument a memory operand that accesses some absolute memory address.
void MemOpInstrumentationTool::InstrumentAddrMemOp(DecodedBasicBlock *bb,
                                                   NativeInstruction *instr,
                                                   MemoryOperand &mloc,
                                                   const void *addr) {
  ImmediateOperand native_addr(addr);
  RegisterOperand addr_reg_op(bb->AllocateVirtualRegister());
  lir::InlineAssembly asm_(native_addr, addr_reg_op);
  asm_.InlineBefore(instr, "MOV r64 %1, i64 %0;"_x86_64);
  InstrumentMemOp(bb, instr, mloc, addr_reg_op);
}

void MemOpInstrumentationTool::InstrumentCompoundMemOp(
    DecodedBasicBlock *bb, NativeInstruction *instr,
    MemoryOperand &mloc) {

  // Track stack pointer propagation.
  VirtualRegister base;
  VirtualRegister addr_reg = bb->AllocateVirtualRegister();
  if (mloc.CountMatchedRegisters(base) &&
      (base.IsStackPointer() || base.IsVirtualStackPointer())) {
    addr_reg.ConvertToVirtualStackPointer();
  }

  RegisterOperand addr_reg_op(addr_reg);
  lir::InlineAssembly asm_(mloc, addr_reg_op);
  asm_.InlineBefore(instr, "LEA r64 %1, m64 %0;"_x86_64);
  InstrumentMemOp(bb, instr, mloc, addr_reg_op);
}
