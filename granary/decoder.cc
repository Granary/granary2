/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/cfg/basic_block.h"
#include "granary/decoder.h"
#include "granary/environment.h"
#include "granary/driver/driver.h"

namespace granary {

// Initialize the instruction encoder with an environment and a control-flow
// graph. The control-flow graph is modified in place (to add successors and
// predecessors).
InstructionDecoder::InstructionDecoder(const Environment *env_)
    : env(env_) {}


// Decode and return a basic block.
void InstructionDecoder::DecodeBasicBlock(InFlightBasicBlock *block) {
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction instr;
  Instruction *reified_instr(nullptr);

  for (AppProgramCounter next_pc(block->app_start_pc), decoded_pc(next_pc);
       decoder.DecodeNext(&instr, &next_pc);
       decoded_pc = next_pc) {

    if (decoder.CanAddInstructionToBasicBlock(&instr)) {


      env->AnnotateInstruction(reified_instr);
    } else {
      break;
    }

    // Handle architecture and operating-system-specific special cases here.
    //
    // For example, when instrumenting the Linux kernel, we need to find all
    // instructions that might fault, and so we consult the exception table
    // data structure (via the `Environment`). We can use this to annotate the
    // instruction list with an annotated instruction.
    //
    // For example, in the Linux kernel on x86, if we see a `swapgs` instruction
    // then we want to back out of the instruction list until we see a write
    // to the stack pointer register, and then go native after that point.
    // if (env->Instruction)
  }

  // TODO(pag): Append a synthesized jump.
  // TODO(pag): Append a synthesized jump to native if next_pc is nullptr.
}

}  // namespace granary
