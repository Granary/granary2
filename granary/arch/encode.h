/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_ENCODE_H_
#define GRANARY_DRIVER_ENCODE_H_

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
class InstructionEncoder {
 public:
  // Initialize the instruction decoder.
  InstructionEncoder(void);

  // Encode an instruction, and update the program counter by reference
  // to point to the next logical instruction. Returns `true` if the
  // instruction was successfully decoded/encoded.
  bool EncodeNext(Instruction *, CachePC *);

  // Encode an instruction. Returns `true` if the instruction was
  // successfully decoded/encoded.
  bool Encode(Instruction *, CachePC);

 private:
  // Internal APIs for encoding instructions. These APIs directly
  // interact with the driver.
  CachePC EncodeInternal(Instruction *, CachePC);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionEncoder);
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_DRIVER_ENCODE_H_
