/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_XED2_INTEL64_DECODER_H_
#define GRANARY_DRIVER_XED2_INTEL64_DECODER_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

#include "granary/driver/xed2-intel64/xed.h"

namespace granary {
namespace driver {

// Forward declarations.
struct Instruction;

// Manages encoding and decoding of instructions.
class InstructionDecoder {
 public:
  inline InstructionDecoder(void) {}

  // Decode/Encode an instruction, and update the program counter by reference
  // to point to the next logical instruction. Returns `true` if the
  // instruction was successfully decoded/encoded.
  bool DecodeNext(Instruction *, AppPC *);
  bool EncodeNext(Instruction *, CachePC *);

  // Decode/Encode an instruction. Returns `true` if the instruction was
  // successfully decoded/encoded.
  bool Decode(Instruction *, AppPC);
  bool Encode(Instruction *, CachePC);

 private:
  // Internal APIs for encoding and decoding instructions. These APIs directly
  // interact with the DynamoRIO driver.
  AppPC DecodeInternal(Instruction *, AppPC);
  CachePC EncodeInternal(Instruction *, CachePC);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_XED2_INTEL64_DECODER_H_
