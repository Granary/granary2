/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/metadata.h"

#include "granary/context.h"
#include "granary/breakpoint.h"

// Append a non-native, created instruction to the block.
#define APP(...) \
  do { \
    __VA_ARGS__; \
    ni.AnalyzeStackUsage(); \
    block->UnsafeAppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

namespace granary {
namespace arch {
namespace {

// Make a fall-through block where the interrupt status is defined by
// `enable_interrupts`.
DirectBasicBlock *MakeFallThrough(ContextInterface *context,
                                  LocalControlFlowGraph *cfg, AppPC next_pc,
                                  bool enable_interrupts) {
  auto meta = context->AllocateBlockMetaData(next_pc);
  auto interrupt_meta = MetaDataCast<InterruptMetaData *>(meta);
  interrupt_meta->interrupts_enabled = enable_interrupts;
  return new DirectBasicBlock(cfg, meta);
}

}  // namespace
enum : unsigned {
  MASK_INTERRUPT_FLAG = 1U << 9U
};

// Add conditional jumps to `pc` to handle a possible change in the interrupt
// status by `instr`.
void AddConditionalInterruptFallThroughs(ContextInterface *context,
                                         LocalControlFlowGraph *cfg,
                                         DecodedBasicBlock *block, AppPC pc,
                                         Instruction *instr) {
  GRANARY_ASSERT(XED_ICLASS_POPF == instr->iclass);

  block->UnsafeAppendInstruction(new AnnotationInstruction(IA_VALID_STACK));
  Instruction ni;
  APP(TEST_MEMv_IMMz_F7r0(&ni,
                          BaseDispMemOp(0, XED_REG_RSP, arch::GPR_WIDTH_BITS),
                          static_cast<unsigned>(MASK_INTERRUPT_FLAG)));

  auto enable_interrupts = new LabelInstruction;

  // Conditional branch, interrupts will be disabled.
  JNZ_RELBRd(&ni, pc);
  block->UnsafeAppendInstruction(new BranchInstruction(&ni, enable_interrupts));
  APP(memcpy(&ni, instr, sizeof ni));  // Append `instr`.
  JMP_RELBRd(&ni, pc);
  block->UnsafeAppendInstruction(new ControlFlowInstruction(
      &ni, MakeFallThrough(context, cfg, pc, false)));

  // Fall-through: interrupts are enabled.
  block->UnsafeAppendInstruction(enable_interrupts);
  APP(memcpy(&ni, instr, sizeof ni));  // Append `instr`.
  JMP_RELBRd(&ni, pc);
  block->UnsafeAppendInstruction(new ControlFlowInstruction(
      &ni, MakeFallThrough(context, cfg, pc, true)));
}

}  // namespace arch
}  // namespace granary
