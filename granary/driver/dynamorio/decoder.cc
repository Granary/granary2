/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/driver/dynamorio/decoder.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

// Initialize the instruction decoder.
InstructionDecoder::InstructionDecoder(void)
    : in_flight_instruction(nullptr),
      allocated_instruction(false),
      allocated_raw_bytes(false),
      num_allocated_operands(0) {
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
  const AppProgramCounter decoded_pc(pc);
  in_flight_instruction = instr;
  instr->Clear();

  dynamorio::instr_t *raw_instr(dynamorio::instr_create(this));
  pc = dynamorio::decode_raw(this, pc, raw_instr);
  if (pc) {
    dynamorio::decode(this, pc, raw_instr);
  }

  raw_instr->bytes = decoded_pc;
  raw_instr->translation = decoded_pc;

  in_flight_instruction = nullptr;
  allocated_instruction = false;
  allocated_raw_bytes = false;
  num_allocated_operands = 0;

  return pc;
}


CacheProgramCounter InstructionDecoder::EncodeInternal(
    DecodedInstruction *instr, CacheProgramCounter pc) {

  const CacheProgramCounter encoded_pc(pc);
  dynamorio::instr_t *raw_instr(&(instr->instruction));

  in_flight_instruction = instr;

  // Address calculation for relative jumps uses the note field.
  raw_instr->note = pc;
  pc = dynamorio::instr_encode(this, &(instr->instruction), pc);

  raw_instr->bytes = encoded_pc;
  raw_instr->translation = encoded_pc;
  raw_instr->length = static_cast<unsigned>(pc - encoded_pc);

  in_flight_instruction = nullptr;
  allocated_instruction = false;
  allocated_raw_bytes = false;
  num_allocated_operands = 0;

  return pc;
}

}  // namespace driver
}  // namespace granary
