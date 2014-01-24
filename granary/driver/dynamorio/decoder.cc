/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"
#include "granary/breakpoint.h"

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

// Initialize the instruction decoder.
InstructionDecoder::InstructionDecoder(DecodedInstruction *instr)
    : in_flight_instruction(instr),
      allocated_instruction(false),
      allocated_raw_bytes(false),
      num_allocated_operands(0) {
  this->dynamorio::dcontext_t::x86_mode = false;
}

// Decode an instruction, and update the program counter by reference to point
// to the next logical instruction. Returns `true` iff the instruction was
// successfully decoded.
bool InstructionDecoder::DecodeNext(DecodedInstruction *instr,
                                    AppProgramCounter *pc) {
  *pc = DecodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Encode t an instruction IR at `*pc` and update `pc`.
bool InstructionDecoder::EncodeNext(DecodedInstruction *instr,
                                    CacheProgramCounter *pc) {
  *pc = EncodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Decode an x86 instruction into an instruction IR.
bool InstructionDecoder::Decode(DecodedInstruction *instr,
                                AppProgramCounter pc) {
  return nullptr != DecodeInternal(instr, pc);
}

// Encode an instruction IR into an x86 instruction.
bool InstructionDecoder::Encode(DecodedInstruction *instr,
                                CacheProgramCounter pc) {
  return nullptr != EncodeInternal(instr, pc);
}

// Decode an x86 instruction into a DynamoRIO instruction intermediate
// representation.
AppProgramCounter InstructionDecoder::DecodeInternal(DecodedInstruction *instr,
                                                     AppProgramCounter pc) {
  if (!pc) {
    return pc;
  }

  const AppProgramCounter decoded_pc(pc);
  in_flight_instruction = instr;
  instr->Clear();

  dynamorio::instr_t *raw_instr(dynamorio::instr_create(this));
  pc = dynamorio::decode_raw(this, pc, raw_instr);
  if (pc) {
    dynamorio::decode(this, pc, raw_instr);
  }

  // Special cases: all of these examples should end a basic block and lead to
  // detaching. Detaching is handled in Granary by synthesizing a direct-to-
  // native jump.
  switch (raw_instr->opcode) {
    case dynamorio::OP_INVALID:
    case dynamorio::OP_UNDECODED:
      granary_break_on_decode(decoded_pc);
      // fall-through.
    case dynamorio::OP_ud2a:
    case dynamorio::OP_ud2b:
    case dynamorio::OP_int3:
      return nullptr;

    default: break;
  }

  raw_instr->bytes = decoded_pc;
  raw_instr->translation = decoded_pc;

  in_flight_instruction = nullptr;
  allocated_instruction = false;
  allocated_raw_bytes = false;
  num_allocated_operands = 0;

  return pc;
}

// Encode a DynamoRIO instruction intermediate representation into an x86
// instruction.
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
