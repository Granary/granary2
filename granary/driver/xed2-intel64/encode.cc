/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"

#include "granary/driver/xed2-intel64/decode.h"
#include "granary/driver/xed2-intel64/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace driver {

// Encode t an instruction IR at `*pc` and update `pc`.
bool InstructionDecoder::EncodeNext(Instruction *instr, CachePC *pc) {
  *pc = EncodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Encode an instruction IR into an x86 instruction.
bool InstructionDecoder::Encode(Instruction *instr, CachePC pc) {
  return nullptr != EncodeInternal(instr, pc);
}

// Encode a XED instruction intermediate representation into an x86
// instruction, and return the address of the next memory location into which
// an instruction can be encoded.
CachePC InstructionDecoder::EncodeInternal(Instruction *instr, CachePC pc) {
  /*if (instr->needs_encoding || instr->has_pc_rel_op) {
    EncodeInstruction(instr, pc);
    instr->needs_encoding = false;
  }
  CopyEncodedBytes(instr, pc);
  return pc + instr->length;*/
  GRANARY_UNUSED(instr);
  return pc;
}

}  // namespace driver
}  // namespace granary
