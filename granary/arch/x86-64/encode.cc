/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"

#include "granary/arch/encode.h"
#include "granary/arch/x86-64/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Encode t an instruction IR at `*pc` and update `pc`.
bool InstructionEncoder::EncodeNext(Instruction *instr, CachePC *pc) {
  *pc = EncodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Encode an instruction IR into an x86 instruction.
bool InstructionEncoder::Encode(Instruction *instr, CachePC pc) {
  return nullptr != EncodeInternal(instr, pc);
}

// Encode a XED instruction intermediate representation into an x86
// instruction, and return the address of the next memory location into which
// an instruction can be encoded.
CachePC InstructionEncoder::EncodeInternal(Instruction *instr, CachePC pc) {
  GRANARY_UNUSED(instr);
  GRANARY_UNUSED(pc);
  return pc;
}

}  // namespace arch
}  // namespace granary
