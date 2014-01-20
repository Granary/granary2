/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DECODER_H_
#define GRANARY_DECODER_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

// Forward declarations.
class Environment;
class BasicBlock;

// Manages decoding instructions into basic blocks.
class InstructionDecoder {
 public:
  explicit InstructionDecoder(const Environment *env_);

  // Decode and return a basic block. This might return an `InFlightBasicBlock`
  // or a `CachedBasicBlock`. For each decoded instruction, this will query the
  // `env` to check for environment-specific behaviors on each instruction.
  void DecodeBasicBlock(InFlightBasicBlock *block);

 private:
  void DecodeInstructionList();

  const Environment * const env;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionDecoder);
};

}  // namespace granary

#endif  // GRANARY_DECODER_H_
