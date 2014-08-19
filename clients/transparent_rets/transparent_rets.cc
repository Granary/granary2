/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_DEFINE_bool(transparent_returns, GRANARY_IF_USER_ELSE(true, false),
    "Enable transparent return addresses? The default is `"
    GRANARY_IF_USER_ELSE("yes", "no") "`.");

// Implements transparent return addresses. This means that the return
// addresses from instrumented function calls will point to native code and
// not into Granary's code cache.
//
// Transparent returns impose a performance overhead because it expands every
// function call/return into many instructions, instead of just a single
// instruction (in practice).
//
// The benefit of transparent return addresses is that it improves:
//    1)  The debugging experience, as program backtraces will appear natural.
//    2)  Likely improves the correctness of instrumentation, lest any programs
//        (e.g. `ld` and `dl`) make decisions based on their return addresses.
//    2)  Opens up the door to return target specialization.
class TransparentRetsInstrumenter : public InstrumentationTool {
 public:
  virtual ~TransparentRetsInstrumenter(void) = default;

  // Push on a return address for either of a direct or indirect function
  // call.
  void AddTransparentRetAddr(ControlFlowInstruction *cfi) {
    GRANARY_ASSERT(cfi->IsAppInstruction());

    // Compute return address.
    auto ret_addr_pc = cfi->DecodedPC() + cfi->DecodedLength();
    ImmediateOperand ret_addr(reinterpret_cast<uintptr_t>(ret_addr_pc),
                              arch::ADDRESS_WIDTH_BYTES);

    // Push on the native return address.
    BeginInlineAssembly({&ret_addr});
    InlineBefore(cfi, "MOV r64 %1, i64 %0;"
                      "PUSH r64 %1;"_x86_64);
    EndInlineAssembly();

    // Convert the (in)direct call into a jump.
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
  void RemoveTailInstructions(DecodedBasicBlock *block,
                              const Instruction *search_instr) {
    auto last_instr = block->LastInstruction();
    Instruction *instr(nullptr);
    do {
      instr = last_instr->Previous();
      Instruction::Unlink(instr);
    } while (instr != search_instr);
  }

  // Instrument the control-flow instructions, specifically: function call
  // instructions.
  virtual void InstrumentControlFlow(BlockFactory *,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
      if (!decoded_block) continue;

      for (auto succ : block->Successors()) {
        // Convert a function call into a `PUSH; JMP` combination.
        if (succ.cfi->IsFunctionCall()) {
          AddTransparentRetAddr(succ.cfi);
          RemoveTailInstructions(decoded_block, succ.cfi);
          break;  // Won't have any more successors.

        // Specialize the return. Behind the scenes, this will convert the
        // return into an indirect jump.
        //
        // Note: `ReturnBasicBlock`s can have meta-data, but usually don't.
        //       Their meta-data is created lazily when first requested with
        //       `MetaData`. One can check if a `ReturnBasicBlock` has meta-data
        //       and optionally operate on it if non-NULL by invoking the
        //       `UnsafeMetaData` method instead.
        } else if (succ.cfi->IsFunctionReturn()) {
          DynamicCast<ReturnBasicBlock *>(succ.block)->MetaData();
        }
      }
    }
  }
};

// Initialize the `transparent_rets` tool.
GRANARY_CLIENT_INIT({
  if (FLAG_transparent_returns) {
    RegisterInstrumentationTool<TransparentRetsInstrumenter>(
        "transparent_rets");
  }
})
