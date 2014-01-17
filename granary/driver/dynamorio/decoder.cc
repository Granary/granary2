/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/driver/dynamorio/decoder.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

// Initialize the instruction decoder.
InstructionDecoder::InstructionDecoder(void)
    : in_flight_instruction(nullptr) {

  this->dynamorio::dcontext_t::x86_mode = false;
}

// Decode an instruction, and update the program counter by reference to point
// to the next logical instruction. Returns `true` iff the instruction was
// successfully decoded.
bool InstructionDecoder::DecodeNext(DecodedInstruction *instr,
                                    AppProgramCounter &pc) {
  pc = DecodeInternal(instr, pc);
  return nullptr != pc;
}


bool InstructionDecoder::EncodeNext(DecodedInstruction *instr,
                                    CacheProgramCounter &pc) {
  pc = EncodeInternal(instr, pc);
  return nullptr != pc;
}


bool InstructionDecoder::Decode(DecodedInstruction *instr,
                                AppProgramCounter pc) {
  return nullptr != DecodeInternal(instr, pc);
}


bool InstructionDecoder::Encode(DecodedInstruction *instr,
                                CacheProgramCounter pc) {
  return nullptr != EncodeInternal(instr, pc);
}


AppProgramCounter InstructionDecoder::DecodeInternal(DecodedInstruction *instr,
                                                     AppProgramCounter pc) {
  in_flight_instruction = instr;
  instr->Clear();
  dynamorio::instr_t *raw_instr(dynamorio::instr_create(this));
  pc = dynamorio::decode_raw(this, pc, raw_instr);
  if (pc) {
    dynamorio::decode(this, pc, raw_instr);
  }
  in_flight_instruction = nullptr;
  return pc;
}


CacheProgramCounter InstructionDecoder::EncodeInternal(
    DecodedInstruction *instr, CacheProgramCounter pc) {
  in_flight_instruction = instr;
  GRANARY_UNUSED(instr);
  GRANARY_UNUSED(pc);
  in_flight_instruction = nullptr;
  return nullptr;
}

}  // namespace driver
}  // namespace granary
