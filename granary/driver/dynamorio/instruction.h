/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DRIVER_DYNAMORIO_INSTRUCTION_H_
#define GRANARY_DRIVER_DYNAMORIO_INSTRUCTION_H_

#include "granary/arch/base.h"
#include "granary/base/base.h"
#include "granary/base/new.h"

#include "generated/dynamorio/types.h"

namespace granary {
namespace driver {

class InstructionDecoder;
class DynamoRIOHeap;

// Contains all data required to represent a decoded instruction.
class DecodedInstruction {
 public:
  inline DecodedInstruction(void) {
    Clear();
  }

  void Clear(void);
  DecodedInstruction *Copy(void) const;

  ProgramCounter BranchTarget(void) const;

  bool IsFunctionCall(void) const;
  bool IsFunctionReturn(void) const;
  bool IsInterruptCall(void) const;
  bool IsInterruptReturn(void) const;
  bool IsSystemCall(void) const;
  bool IsSystemReturn(void) const;
  bool IsJump(void) const;
  bool IsConditionalJump(void) const;
  bool HasIndirectTarget(void) const;

  int Length(void) const;

  GRANARY_DEFINE_NEW_ALLOCATOR(DecodedInstruction, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

  // The actual DynamoRIO `instr_t` data structure.
  dynamorio::instr_t instruction;

 private:
  friend class DynamoRIOHeap;

  enum {
    MAX_NUM_RAW_BYTES = 32,
    MAX_NUM_OPERANDS = 8
  };

  // Used internally by DynamoRIO. The raw bytes can contain an in-flight,
  // encoded version of this instruction. These bytes can also contain a
  // decoded/copied version of the instruction.
  unsigned char raw_bytes[MAX_NUM_RAW_BYTES];

  // The operands referenced by the DynamoRIO `instr_t` structure. There can be
  // up to 8 operands, but most instructions used <= 3.
  dynamorio::opnd_t operands[MAX_NUM_OPERANDS];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(DecodedInstruction);
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_DYNAMORIO_INSTRUCTION_H_
