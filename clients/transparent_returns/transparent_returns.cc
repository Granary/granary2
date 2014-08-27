/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_DEFINE_bool(transparent_returns, GRANARY_IF_USER_ELSE(true, false),
    "Enable transparent return addresses? The default is `"
    GRANARY_IF_USER_ELSE("yes", "no") "`.",

    "transparent_returns");

// Implements transparent return addresses. This means that the return
// addresses from instrumented function calls will point to native code and
// not into Granary's code cache.
//
// Transparent returns impose a performance overhead because they expand every
// function call/return into many instructions, instead of just a single
// instruction (in practice).
//
// The benefits of transparent return addresses are:
//    1)  Improved debugging experience, as program backtraces will appear
//        natural.
//    2)  Improves the correctness of instrumentation. Some programs won't work
//        without transparent return addresses because they will inspect and
//        decisions based on return addresses. For example, without transparent
//        return addresses, `_dl_debug_initialize` of `dl` will sometimes
//        segfault when `_dl_open` is called by `do_dlopen`. This appears to
//        be because it uses the return address to resolve a namespace, but
//        fails to do so, and then dies. This simple issue rules out most
//        standard UNIX utilities.
//    3)  Opens up the door to return target specialization. This can be useful
//        for things like tracking lock nesting depth using block meta-data.
//    4)  Improves the attach/detach story, because it makes it so that a detach
//        is really a full detach, and doesn't require that the instrumented
//        code be given time to quiesce to some kind of native state.
//
// TODO(pag): Its not clear if the best implementation of this is as a tool, as
//            an internal feature, or as some combination thereof. For example,
//            in some cases, we might want transparent return addresses on all
//            but a few calls that actually do go native, but for which we want
//            execution to return to instrumented code.
//
//            It's not clear how to nicely handle this case, except to just
//            have a purpose-built tool that re-implements selective transparent
//            return addresses, and requires that use user manually specifies
//            `--transparent_returns=no` at the command-line.
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
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
      if (!decoded_block) continue;

      for (auto succ : block->Successors()) {
        // Convert a function call into a `PUSH; JMP` combination.
        if (succ.cfi->IsFunctionCall()) {
          AddTransparentRetAddr(succ.cfi);
          RemoveTailInstructions(decoded_block, succ.cfi);
          factory->RequestBlock(succ.block);  // Walk into the call.
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
        "transparent_returns");
  }
})
