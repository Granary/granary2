/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/driver/driver.h"

#include "granary/decoder.h"
#include "granary/environment.h"
#include "granary/mir.h"

namespace granary {

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
class UnknownBasicBlock : public InstrumentedBasicBlock {
 public:
  UnknownBasicBlock(AppProgramCounter app_start_pc_,
                    const BasicBlockMetaData *entry_meta_,
                    BasicBlockMetaData *meta_)
      : InstrumentedBasicBlock(app_start_pc_, entry_meta_, meta_) {}

  virtual ~UnknownBasicBlock(void) = default;

  GRANARY_DERIVED_CLASS_OF(BasicBlock, UnknownBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(UnknownBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1  // Read-only after allocation.
  });

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(UnknownBasicBlock);

} static UNKNOWN_BLOCK(nullptr, nullptr, nullptr);

// Initialize the instruction encoder with an environment and a control-flow
// graph. The control-flow graph is modified in place (to add successors and
// predecessors).
InstructionDecoder::InstructionDecoder(const Environment *env_,
                                       ControlFlowGraph *cfg_)
    : env(env_),
      cfg(cfg_) {}

// Decode and return a basic block.
void InstructionDecoder::DecodeBasicBlock(AppProgramCounter start_pc,
                                          BasicBlockMetaData *meta) {
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction instr;

  auto block = new InFlightBasicBlock(start_pc, meta, nullptr);
  block->first = new AnnotationInstruction(BEGIN_BASIC_BLOCK);
  block->last = new AnnotationInstruction(END_BASIC_BLOCK);

  Instruction *curr_instr(block->first);
  Instruction *next_instr(nullptr);

  for (AppProgramCounter next_pc(block->app_start_pc), decoded_pc(next_pc);
       decoder.DecodeNext(&instr, &next_pc);
       decoded_pc = next_pc, curr_instr = next_instr) {

    // Decode and annotate the instruction.
    curr_instr = DecodeInstruction(&instr);
    next_instr = env->AnnotateInstruction(curr_instr);

    // Figure out if we need to add a synthesized jump to the end of the
    // basic block.
    auto cti = DynamicCast<ControlFlowInstruction *>(curr_instr);
    if (cti) {
      if (cti->IsConditionalJump() || cti->IsFunctionCall()) {
        next_instr->InsertAfter(mir::Jump(cfg, next_pc));
      }
      break;
    }
  }
}

namespace {
static Instruction *MakeDirectCTI(driver::DecodedInstruction *decoded_instr,
                           AppProgramCounter target) {
  return new ControlFlowInstruction(
      decoded_instr, new FutureBasicBlock(target, nullptr));
}
}  // namespace

// Convert a decoded instruction into the internal Granary IR.
Instruction *InstructionDecoder::DecodeInstruction(
    const driver::DecodedInstruction *instr) {

  auto *instr_copy(new driver::DecodedInstruction);
  instr_copy->Copy(instr);

  // Conditional jump: need to synthesize a fall-through jump.
  if (instr_copy->IsConditionalJump()) {
    return MakeDirectCTI(instr_copy, instr_copy->BranchTarget());

  // Indirect call, jump, return, interrupt return, system call, system return.
  } else if (instr_copy->HasIndirectTarget()) {
    return new ControlFlowInstruction(instr_copy, &UNKNOWN_BLOCK);

  // Function call or direct jump.
  } else if (instr_copy->IsFunctionCall() || instr_copy->IsJump()) {
    return MakeDirectCTI(instr_copy, instr_copy->BranchTarget());

  // Plain old instruction.
  } else {
    return new NativeInstruction(instr_copy);
  }
}

}  // namespace granary
