/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WATCHPOINTS_WATCHPOINTS_H_
#define CLIENTS_WATCHPOINTS_WATCHPOINTS_H_

#include <granary.h>

// Forward declaration.
class Watchpoints;

struct WatchedOperand {
 protected:
  friend class Watchpoints;

  inline WatchedOperand(granary::DecodedBasicBlock *block_,
                        granary::NativeInstruction *instr_,
                        const granary::MemoryOperand &mem_op_,
                        const granary::RegisterOperand &unwatched_reg_op_,
                        const granary::RegisterOperand &watched_reg_op_)
      : block(block_),
        instr(instr_),
        mem_op(mem_op_),
        unwatched_reg_op(unwatched_reg_op_),
        watched_reg_op(watched_reg_op_) {}

 public:
  granary::DecodedBasicBlock * const block;

  // Instruction that contains the memory operand `mem_op`.
  granary::NativeInstruction * const instr;

  // Memory operand that de-references a potentially watched address.
  const granary::MemoryOperand &mem_op;

  // Register operand, where the register will contain the unwatched address.
  const granary::RegisterOperand &unwatched_reg_op;

  // Register operand, where the register will contain the watched address.
  const granary::RegisterOperand &watched_reg_op;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(WatchedOperand);
};

#endif  // CLIENTS_WATCHPOINTS_WATCHPOINTS_H_
