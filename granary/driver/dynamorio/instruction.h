/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DRIVER_DYNAMORIO_INSTRUCTION_H_
#define GRANARY_DRIVER_DYNAMORIO_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/driver/dynamorio/types.h"

namespace granary {
namespace driver {

class InstructionDecoder;

// Contains all data required to represent a decoded instruction.
class DecodedInstruction {
 public:
  inline DecodedInstruction(void) {
    Clear();
  }

  void Clear(void);
  void Copy(const DecodedInstruction *);

 private:
  friend class InstructionDecoder;

  // Used internally by DynamoRIO. The raw bytes can contain an in-flight,
  // encoded version of this instruction. These bytes can also contain a
  // decoded/copied version of the instruction.
  unsigned char raw_bytes[32];

  // The operands referenced by the DynamoRIO `instr_t` structure. There can be
  // up to 8 operands, but most instructions used <= 3.
  dynamorio::opnd_t operands[8];

  // The actual DynamoRIO `instr_t` data structure.
  dynamorio::instr_t instruction;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(DecodedInstruction);
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_DYNAMORIO_INSTRUCTION_H_
