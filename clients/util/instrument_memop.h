/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_UTIL_INSTRUMENT_MEMOP_H_
#define CLIENTS_UTIL_INSTRUMENT_MEMOP_H_

#include <granary.h>

// Represents an "instrumented" memory operand in a general way.
class InstrumentedMemoryOperand {
 public:
  // Block that contains `instr`.
  granary::DecodedBlock * const block;

  // Instruction that contains the memory operand `native_mem_op`.
  granary::NativeInstruction * const instr;

  // Memory operand that is accessing native memory.
  granary::MemoryOperand &native_mem_op;

  // Register operand containing the native address accessed by
  // `address_reg_op`.
  const granary::RegisterOperand &native_addr_op;

  // Which memory operand (of the instruction) is being shadowed? This is
  // going to be `0` or `1`.
  const size_t operand_number;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentedMemoryOperand);
};

// Abstract tool for instrumenting memory operands.
class MemOpInstrumentationTool : public granary::InstrumentationTool {
 public:
  MemOpInstrumentationTool(void);

  virtual ~MemOpInstrumentationTool(void) = default;

  virtual void InstrumentBlocks(granary::Trace *trace) override;
  virtual void InstrumentBlock(granary::DecodedBlock *bb) override;

 protected:
  virtual void InstrumentMemOp(InstrumentedMemoryOperand &op) = 0;

 private:

  // Instrument a memory operation.
  void InstrumentMemOp(granary::MemoryOperand &mloc);

  // Instrument a memory operand that accesses some memory through a register.
  void InstrumentRegMemOp(granary::MemoryOperand &mloc,
                          granary::VirtualRegister reg);

  // Instrument a memory operand that accesses some memory through an offset of
  // a segment register. We assume that the first quadword stored in the segment
  // points to the segment base address.
  void InstrumentSegMemOp(granary::MemoryOperand &mloc,
                          granary::VirtualRegister seg_offs,
                          granary::VirtualRegister seg_reg);

  // Instrument a memory operand that accesses some absolute memory address.
  void InstrumentAddrMemOp(granary::MemoryOperand &mloc, const void *addr);

  // Instrument a compound memory operation.
  void InstrumentCompoundMemOp(granary::MemoryOperand &mloc);

 private:
  granary::DecodedBlock *bb;
  granary::NativeInstruction *instr;
  granary::VirtualRegister virt_addr_reg[2];
  size_t op_num;
};

#endif  // CLIENTS_UTIL_INSTRUMENT_MEMOP_H_
