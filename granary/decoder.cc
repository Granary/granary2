/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/decoder.h"
#include "granary/driver/driver.h"
#include "granary/environment.h"

namespace granary {

enum InstructionAnnotation : uint32_t {
  BEGIN_BASIC_BLOCK             = (1 << 0),
  END_BASIC_BLOCK               = (1 << 1),
  BEGIN_MIGHT_FAULT             = (1 << 2),
  END_MIGHT_FAULT               = (1 << 3),
  BEGIN_DELAY_INTERRUPT         = (1 << 4),
  END_DELAY_INTERRUPT           = (1 << 5),
  LABEL                         = (1 << 6),
};

// Initialize the instruction encoder with an environment and a control-flow
// graph. The control-flow graph is modified in place (to add successors and
// predecessors).
InstructionDecoder::InstructionDecoder(const Environment *env_)
    : env(env_) {}


// Decode and return a basic block.
void InstructionDecoder::DecodeBasicBlock(InFlightBasicBlock *block) {
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction instr;

  block->first = new AnnotationInstruction(BEGIN_BASIC_BLOCK);
  block->last = new AnnotationInstruction(END_BASIC_BLOCK);
  Instruction *current(block->first);

  for (AppProgramCounter next_pc(block->app_start_pc), decoded_pc(next_pc);
       decoder.DecodeNext(&instr, &next_pc);
       decoded_pc = next_pc) {

    if (decoder.CanAddInstructionToBasicBlock(&instr)) {
      current = current->InsertAfter(std::move(DecodeInstruction(&instr)));
      current = env->AnnotateInstruction(current);
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

std::unique_ptr<Instruction> InstructionDecoder::DecodeInstruction(
    const driver::DecodedInstruction *instr) {

  auto *new_instr(new driver::DecodedInstruction);
  new_instr->Copy(instr);

  if (instr->IsJump()) {
    if (instr->IsConditionalJump()) {

    } else {

    }
  } else if (instr->IsFunctionReturn()) {

  } else if (instr->IsFunctionCall()) {

  } else {

  }
  return nullptr;
}

}  // namespace granary
