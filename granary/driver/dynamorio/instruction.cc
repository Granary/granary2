/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/string.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

void DecodedInstruction::Clear(void) {
  memset(this, 0, sizeof *this);
}


void DecodedInstruction::Copy(const DecodedInstruction *that) {
  if (this == that) {
    return;
  }

  memcpy(this, that, sizeof *this);

  if (instruction.srcs) {
    instruction.srcs = &(operands[that->instruction.srcs -
                                  &(that->operands[0])]);
  }
  if (instruction.dsts) {
    instruction.dsts = &(operands[that->instruction.dsts -
                                  &(that->operands[0])]);
  }
  if (instruction.note == &(that->raw_bytes[0])) {
    instruction.note = &(raw_bytes[0]);
  }
  if (instruction.translation == &(that->raw_bytes[0])) {
    instruction.translation = &(raw_bytes[0]);
  }
  if (instruction.bytes == &(that->raw_bytes[0])) {
    instruction.bytes = &(raw_bytes[0]);
  }
}

}  // namespace driver
}  // namespace granary
