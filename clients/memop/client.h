/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_MEMOP_CLIENT_H_
#define CLIENTS_MEMOP_CLIENT_H_

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

// Registers a function that can hook into the memory operands instrumenter.
void AddMemOpInstrumenter(void (*func)(const InstrumentedMemoryOperand &));

#endif  // CLIENTS_MEMOP_CLIENT_H_
