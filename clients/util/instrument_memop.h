/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_UTIL_INSTRUMENT_MEMOP_H_
#define CLIENTS_UTIL_INSTRUMENT_MEMOP_H_

#include <granary.h>

// Abstract tool for instrumenting memory operands.
class MemOpInstrumentationTool : public granary::InstrumentationTool {
 public:
  virtual ~MemOpInstrumentationTool(void) = default;

  virtual void InstrumentBlock(granary::DecodedBasicBlock *bb) override;

 protected:
  virtual void InstrumentMemOp(granary::DecodedBasicBlock *bb,
                               granary::NativeInstruction *instr,
                               granary::MemoryOperand &mloc,
                               const granary::RegisterOperand &addr_reg) = 0;
 private:

  // Instrument a memory operation.
  void InstrumentMemOp(granary::DecodedBasicBlock *bb,
                       granary::NativeInstruction *instr,
                       granary::MemoryOperand &mloc);

  // Instrument a memory operand that accesses some memory through an offset of
  // a segment register. We assume that the first quadword stored in the segment
  // points to the segment base address.
  void InstrumentSegMemOp(granary::DecodedBasicBlock *bb,
                          granary::NativeInstruction *instr,
                          granary::MemoryOperand &mloc,
                          granary::VirtualRegister seg_offs);

  // Instrument a memory operand that accesses some absolute memory address.
  void InstrumentAddrMemOp(granary::DecodedBasicBlock *bb,
                           granary::NativeInstruction *instr,
                           granary::MemoryOperand &mloc,
                           const void *addr);

  // Instrument a compound memory operation.
  void InstrumentCompoundMemOp(granary::DecodedBasicBlock *bb,
                               granary::NativeInstruction *instr,
                               granary::MemoryOperand &mloc);
};

#endif  // CLIENTS_UTIL_INSTRUMENT_MEMOP_H_
