/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DECODER_H_
#define GRANARY_DECODER_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

// Forward declarations.
class Environment;
class ControlFlowGraph;
class BasicBlock;
class BasicBlockMetaData;
class CodeCacheIndex;

// Manages decoding instructions into basic blocks and adding those basic blocks
// into the control-flow graph.
class InstructionDecoder {
 public:
  InstructionDecoder(const Environment *env_, const CodeCacheIndex *index_,
                     ControlFlowGraph *cfg_);

  // Decode and return a basic block. This might return an `InFlightBasicBlock`
  // or a `CachedBasicBlock`. This function might query the `index` to find
  // a `CachedBasicBlock`. For each decoded instruction, this will query the
  // `env` to check for environment-specific behaviors on each instruction.
  BasicBlock *DecodeBasicBlock(const BasicBlockMetaData *meta,
                               AppProgramCounter start_pc);

 private:
  const Environment * const env;
  const CodeCacheIndex * const index;
  ControlFlowGraph * const cfg;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace granary

#endif  // GRANARY_DECODER_H_
