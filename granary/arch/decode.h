/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_DECODE_H_
#define GRANARY_DRIVER_DECODE_H_

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
  bool DecodeNext(DecodedBasicBlock *block, Instruction *, AppPC *);

  // Decode an instruction. Returns `true` if the instruction was
  // successfully decoded/encoded.
  bool Decode(DecodedBasicBlock *block, Instruction *, AppPC);

 private:
  // Internal APIs for decoding instructions. These APIs directly
  // interact with the driver.
  AppPC DecodeInternal(DecodedBasicBlock *block, Instruction *, AppPC);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_DRIVER_DECODE_H_
