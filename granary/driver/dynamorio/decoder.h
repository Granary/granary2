/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DRIVER_DYNAMORIO_DECODER_H_
#define GRANARY_DRIVER_DYNAMORIO_DECODER_H_

#include "granary/base/base.h"
#include "granary/base/types.h"
#include "granary/driver/dynamorio/types.h"

namespace granary {

class Instruction;

namespace driver {

class DecodedInstruction;
class DynamoRIOHeap;
class InstructionBuilder;

// Manages encoding and decoding of instructions.
class InstructionDecoder : public dynamorio::dcontext_t {
 public:
  // Initialize the instruction decoder.
  InstructionDecoder(void);
  explicit InstructionDecoder(DecodedInstruction *);

  // Decode/Encode an instruction, and update the program counter by reference
  // to point to the next logical instruction. Returns `true` if the
  // instruction was successfully decoded/encoded.
  bool DecodeNext(DecodedInstruction *, AppProgramCounter *);
  bool EncodeNext(DecodedInstruction *, CacheProgramCounter *);

  // Decode/Encode an instruction. Returns `true` if the instruction was
  // successfully decoded/encoded.
  bool Decode(DecodedInstruction *, AppProgramCounter);
  bool Encode(DecodedInstruction *, CacheProgramCounter);

 private:
  friend class DynamoRIOHeap;
  friend class InstructionBuilder;

  // Internal APIs for encoding and decoding instructions. These APIs directly
  // interact with the DynamoRIO driver.
  AppProgramCounter DecodeInternal(DecodedInstruction *, AppProgramCounter);
  CacheProgramCounter EncodeInternal(DecodedInstruction *, CacheProgramCounter);

  // The current instruction being encoded or decoded.
  DecodedInstruction *in_flight_instruction;
  bool allocated_instruction;
  bool allocated_raw_bytes;
  size_t num_allocated_operands;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_DYNAMORIO_DECODER_H_
