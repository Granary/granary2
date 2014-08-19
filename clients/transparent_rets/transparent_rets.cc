/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_IF_KERNEL( GRANARY_DEFINE_bool(
    transparent_returns, false,
    "Enable transparent return addresses? The default is `no`."); )

// Tool that implements several user-space special cases for instrumenting
// common binaries.
class TransparentRetsInstrumenter : public InstrumentationTool {
 public:
  virtual ~TransparentRetsInstrumenter(void) = default;

  // Push on a return address of a block is targeting libdl.
  void AddTransparentRetAddr(ControlFlowInstruction *cfi) {
    ImmediateOperand ret_addr(
        reinterpret_cast<uintptr_t>(cfi->DecodedPC() + cfi->DecodedLength()),
        arch::ADDRESS_WIDTH_BYTES);
    BeginInlineAssembly({&ret_addr});
    InlineBefore(cfi, "MOV r64 %1, i64 %0;"
                      "PUSH r64 %1;"_x86_64);
    EndInlineAssembly();
    if (cfi->HasIndirectTarget()) {
      RegisterOperand target_reg;
      GRANARY_IF_DEBUG( auto matched = ) cfi->MatchOperands(
          ReadFrom(target_reg));
      GRANARY_ASSERT(matched);
      cfi->InsertBefore(lir::IndirectJump(cfi->TargetBlock(), target_reg));
    } else {
      cfi->InsertBefore(lir::Jump(cfi->TargetBlock()));
    }
  }

  // Remove all instructions starting from (and including) `search_instr`.
  void RemoveTailAt(DecodedBasicBlock *block,
                    const Instruction *search_instr) {
    auto last_instr = block->LastInstruction();
    Instruction *instr(nullptr);
    do {
      instr = last_instr->Previous();
      Instruction::Unlink(instr);
    } while (instr != search_instr);
  }

  virtual void InstrumentControlFlow(BlockFactory *,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
      if (!decoded_block) continue;

      for (auto succ : block->Successors()) {
        if (succ.cfi->IsFunctionCall()) {
          AddTransparentRetAddr(succ.cfi);
          RemoveTailAt(decoded_block, succ.cfi);
          break;  // Won't have any more successors.
        } else if (succ.cfi->IsFunctionReturn()) {
          DynamicCast<ReturnBasicBlock *>(succ.block)->MetaData();
        }
      }
    }
  }
};

// Initialize the `transparent_rets` tool.
GRANARY_CLIENT_INIT({
  GRANARY_IF_KERNEL( if (FLAG_transparent_returns) )
    RegisterInstrumentationTool<TransparentRetsInstrumenter>(
        "transparent_rets");
})
