/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/instrument_memop.h"  // Needs to go first.

GRANARY_USING_NAMESPACE granary;

MemOpInstrumentationTool::MemOpInstrumentationTool(void)
    : bb(nullptr),
      instr(nullptr),
      virt_addr_reg{},
      op_num(0) {}

void MemOpInstrumentationTool::InstrumentBlocks(granary::Trace *trace) {
  virt_addr_reg[0] = trace->AllocateVirtualRegister();
  virt_addr_reg[1] = trace->AllocateVirtualRegister();
}

// Instrument all of the instructions in a basic block.
void MemOpInstrumentationTool::InstrumentBlock(DecodedBlock *bb_) {
  MemoryOperand mloc1, mloc2;
  bb = bb_;
  for (auto instr_ : bb->AppInstructions()) {
    auto num_matched = instr_->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                    ReadOrWriteTo(mloc2));
    instr = instr_;
    op_num = 0;
    if (2 == num_matched) {
      InstrumentMemOp(mloc1);
      op_num = 1;
      InstrumentMemOp(mloc2);
    } else if (1 == num_matched) {
      InstrumentMemOp(mloc1);
    }
  }
}

// Instrument a memory operation.
void MemOpInstrumentationTool::InstrumentMemOp(MemoryOperand &mloc) {
  if (mloc.IsEffectiveAddress()) return;  // Doesn't access memory.

  // Reads or writes from an absolute address, not through a register.
  VirtualRegister addr_reg, seg_reg;
  const void *addr_ptr(nullptr);

  if (mloc.MatchRegister(addr_reg)) {
    if (mloc.MatchSegmentRegister(seg_reg)) {
      InstrumentSegMemOp(mloc, addr_reg, seg_reg);
    } else {
      InstrumentRegMemOp(mloc, addr_reg);
    }
  } else if (mloc.MatchPointer(addr_ptr)) {
    InstrumentAddrMemOp(mloc, addr_ptr);

  } else if (mloc.IsCompound()) {
    InstrumentCompoundMemOp(mloc);
  }
}

// Instrument a memory operand that accesses some memory through a register.
void MemOpInstrumentationTool::InstrumentRegMemOp(
    granary::MemoryOperand &mloc, granary::VirtualRegister addr_reg) {
  RegisterOperand addr_reg_op(addr_reg);
  InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
  InstrumentMemOp(op);
}

// Instrument a memory operand that accesses some memory through an offset of
// a segment register. We assume that the first quadword stored in the segment
// points to the segment base address.
void MemOpInstrumentationTool::InstrumentSegMemOp(MemoryOperand &mloc,
                                                  VirtualRegister seg_offs,
                                                  VirtualRegister seg_reg) {
  RegisterOperand offset_op(seg_offs);
  RegisterOperand addr_reg_op(virt_addr_reg[op_num]);
  RegisterOperand seg_reg_op(seg_reg);
  lir::InlineAssembly asm_(offset_op, addr_reg_op, seg_reg_op);
  asm_.InlineBefore(instr, "MOV r64 %1, m64 %2:[0];"
                           "LEA r64 %1, m64 [%1 + %0];"_x86_64);
  InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
  InstrumentMemOp(op);
}

// Instrument a memory operand that accesses some absolute memory address.
void MemOpInstrumentationTool::InstrumentAddrMemOp(MemoryOperand &mloc,
                                                   const void *addr) {
  ImmediateOperand native_addr(addr);
  RegisterOperand addr_reg_op(virt_addr_reg[op_num]);
  lir::InlineAssembly asm_(native_addr, addr_reg_op);
  asm_.InlineBefore(instr, "MOV r64 %1, i64 %0;"_x86_64);
  InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
  InstrumentMemOp(op);
}

void MemOpInstrumentationTool::InstrumentCompoundMemOp(MemoryOperand &mloc) {

  // Track stack pointer propagation.
  VirtualRegister base;
  auto addr_reg = virt_addr_reg[op_num];
  if (mloc.CountMatchedRegisters(base) && base.IsStackPointerAlias()) {
    addr_reg.MarkAsStackPointerAlias();
  }

  RegisterOperand addr_reg_op(addr_reg);
  lir::InlineAssembly asm_(mloc, addr_reg_op);
  asm_.InlineBefore(instr, "LEA r64 %1, m64 %0;"_x86_64);
  InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
  InstrumentMemOp(op);
}
