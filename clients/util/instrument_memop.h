/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_UTIL_INSTRUMENT_MEMOP_H_
#define CLIENTS_UTIL_INSTRUMENT_MEMOP_H_

#include <granary.h>

// Represents an "instrumented" memory operand in a general way.
class InstrumentedMemoryOperand {
 public:
  // Block that contains `instr`.
  granary::DecodedBasicBlock * const block;

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
  virtual ~MemOpInstrumentationTool(void) = default;

  virtual void InstrumentBlock(granary::DecodedBasicBlock *bb) override;

 protected:
  virtual void InstrumentMemOp(InstrumentedMemoryOperand &op) = 0;

 private:

  // Instrument a memory operation.
  void InstrumentMemOp(granary::DecodedBasicBlock *bb,
                       granary::NativeInstruction *instr,
                       granary::MemoryOperand &mloc,
                       size_t op_num);

  // Instrument a memory operand that accesses some memory through a register.
  void InstrumentRegMemOp(granary::DecodedBasicBlock *bb,
                          granary::NativeInstruction *instr,
                          granary::MemoryOperand &mloc,
                          granary::VirtualRegister reg,
                          size_t op_num);

  // Instrument a memory operand that accesses some memory through an offset of
  // a segment register. We assume that the first quadword stored in the segment
  // points to the segment base address.
  void InstrumentSegMemOp(granary::DecodedBasicBlock *bb,
                          granary::NativeInstruction *instr,
                          granary::MemoryOperand &mloc,
                          granary::VirtualRegister seg_offs,
                          size_t op_num);

  // Instrument a memory operand that accesses some absolute memory address.
  void InstrumentAddrMemOp(granary::DecodedBasicBlock *bb,
                           granary::NativeInstruction *instr,
                           granary::MemoryOperand &mloc,
                           const void *addr,
                           size_t op_num);

  // Instrument a compound memory operation.
  void InstrumentCompoundMemOp(granary::DecodedBasicBlock *bb,
                               granary::NativeInstruction *instr,
                               granary::MemoryOperand &mloc,
                               size_t op_num);
};

#endif  // CLIENTS_UTIL_INSTRUMENT_MEMOP_H_
