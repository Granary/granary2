/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_DECODE_H_
#define ARCH_DECODE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;

namespace arch {

// Forward declarations.
class Instruction;

// Manages encoding and decoding of instructions.
class InstructionDecoder {
 public:
  // Initialize the instruction decoder.
  InstructionDecoder(void);

  // Decode an instruction, and update the program counter by reference
  // to point to the next logical instruction. Returns `true` if the
  // instruction was successfully decoded/encoded.
  bool DecodeNext(Instruction *, AppPC *);

  // Decode an instruction. Returns `true` if the instruction was
  // successfully decoded/encoded.
  bool Decode(Instruction *, AppPC);

  // Mangle a decoded instruction. Separated from the `Decode` step because
  // mangling might involve adding many new instructions to deal with some
  // instruction set peculiarities, and sometimes we only want to speculatively
  // decode and instruction and not add these extra instructions to a block.
  void Mangle(DecodedBasicBlock *block, Instruction *instr);

 private:
  // Internal APIs for decoding instructions. These APIs directly
  // interact with the driver.
  AppPC DecodeInternal(Instruction *, AppPC);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace arch
}  // namespace granary

#endif  // ARCH_DECODE_H_
