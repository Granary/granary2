/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DECODER_H_
#define GRANARY_DECODER_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

// Forward declarations.
class Environment;
class InFlightBasicBlock;
class Instruction;

namespace driver {
class DecodedInstruction;
}  // namespace driver

// Manages decoding instructions into basic blocks.
class InstructionDecoder {
 public:
  explicit InstructionDecoder(const Environment *env_, ControlFlowGraph *cfg_);

  // Decode a basic block and update the control-flow graph.
  void DecodeBasicBlock(AppProgramCounter start_pc, BasicBlockMetaData *meta);

 private:
  Instruction *DecodeInstruction(const driver::DecodedInstruction *instr);

  const Environment * const env;
  ControlFlowGraph *cfg;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace granary

#endif  // GRANARY_DECODER_H_
