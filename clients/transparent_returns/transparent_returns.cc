/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_DEFINE_bool(transparent_returns, GRANARY_IF_USER_ELSE(true, false),
    "Enable transparent return addresses? The default is `"
    GRANARY_IF_USER_ELSE("yes", "no") "`.\n"
    "\n"
    "Note: Enabling transparent returns will introduce significant\n"
    "      performance overheads due to the extra complications involved\n"
    "      specializing function return targets."
    GRANARY_IF_USER("\n"
    "\n"
    "Note: Granary needs to preserve return address transparency when\n"
    "      comprehensively instrumenting user space programs. However, if a\n"
    "      program isn't being comprehensively instrumented, then return\n"
    "      address transparency can likely be enabled."),

    "transparent_returns");

class RetAddrInCodeCache : public IndexableMetaData<RetAddrInCodeCache> {
 public:
  RetAddrInCodeCache(void)
      : returns_to_cache(!FLAG_transparent_returns) {}

  bool Equals(const RetAddrInCodeCache *that) const {
    return returns_to_cache == that->returns_to_cache;
  }

  bool returns_to_cache;
};

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

class TransparentRetsInstrumenterEarly : public InstrumentationTool {
 public:
  virtual ~TransparentRetsInstrumenterEarly(void) = default;

  virtual void Init(InitReason) {
    RegisterMetaData<RetAddrInCodeCache>();
  }

  // Should the return be specialized?
  bool ShouldSpecializeReturn(BasicBlock *block) {
    return !GetMetaData<RetAddrInCodeCache>(block)->returns_to_cache;
  }

  // Is `block` something that can still be specialized?
  bool IsFutureBlock(BasicBlock *block) {
    return IsA<DirectBasicBlock *>(block) || IsA<IndirectBasicBlock *>(block);
  }

  // Propagates the meta-data tracking of whether or not the return address is
  // located in the code cache or is transparent.
  void SetRetAddrLocation(BasicBlock *predecessor,
                          detail::BasicBlockSuccessor succ) {
    if (succ.cfi->IsFunctionCall()) {
      GetMetaData<RetAddrInCodeCache>(succ.block)->returns_to_cache = \
          !FLAG_transparent_returns;
    } else {
      *GetMetaData<RetAddrInCodeCache>(succ.block) = \
          *GetMetaData<RetAddrInCodeCache>(predecessor);
    }
  }

  // Used to instrument code entrypoints.
  virtual void InstrumentEntryPoint(BlockFactory *,
                                    CompensationBasicBlock *entry_block,
                                    EntryPointKind kind, int) {
    if (ENTRYPOINT_USER_LOAD == kind) {
      GetMetaData<RetAddrInCodeCache>(entry_block)->returns_to_cache = false;
      for (auto succ : entry_block->Successors()) {
        SetRetAddrLocation(entry_block, succ);
      }
    }
  }

  // Instrument the control-flow instructions, specifically: function call
  // instructions.
  virtual void InstrumentControlFlow(BlockFactory *,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (IsFutureBlock(succ.block)) SetRetAddrLocation(block, succ);

        // Specialize the return. Behind the scenes, this will convert the
        // return into an indirect jump.
        //
        // Note: `ReturnBasicBlock`s can have meta-data, but usually don't.
        //       Their meta-data is created lazily when first requested with
        //       `MetaData`. One can check if a `ReturnBasicBlock` has meta-data
        //       and optionally operate on it if non-NULL by invoking the
        //       `UnsafeMetaData` method instead.
        if (succ.cfi->IsFunctionReturn() && ShouldSpecializeReturn(block)) {
          GetMetaDataStrict<RetAddrInCodeCache>(
              succ.block)->returns_to_cache = false;
        }
      }
    }
  }
};

class TransparentRetsInstrumenterLate : public InstrumentationTool {
 public:
  TransparentRetsInstrumenterLate(void)
      : InstrumentationTool() {}

  virtual ~TransparentRetsInstrumenterLate(void) = default;

  // Push on a return address for either of a direct or indirect function
  // call.
  void AddTransparentRetAddr(ControlFlowInstruction *cfi) {
    // Compute return address.
    auto ret_addr_pc = cfi->DecodedPC() + cfi->DecodedLength();
    auto ret_addr_uint = reinterpret_cast<uintptr_t>(ret_addr_pc);
    auto is_u32 = static_cast<uint32_t>(ret_addr_uint) == ret_addr_uint;
    ImmediateOperand ret_addr(reinterpret_cast<uintptr_t>(ret_addr_pc),
                              is_u32 ? 32 : arch::ADDRESS_WIDTH_BYTES);

    // Push on the native return address.
    lir::InlineAssembly asm_(ret_addr);
    asm_.InlineBeforeIf(cfi, is_u32,  "PUSH i32 %0;"_x86_64);
    asm_.InlineBeforeIf(cfi, !is_u32, "MOV r64 %1, i64 %0;"
                                      "PUSH r64 %1;"_x86_64);
    lir::ConvertFunctionCallToJump(cfi);
  }

  // Add a return address to the
  void AddRetAddrToBlock(BlockFactory *factory, DecodedBasicBlock *block) {
    if (!block) return;
    for (auto succ : block->Successors()) {
      if (!succ.cfi->IsFunctionCall()) continue;
      if (!succ.cfi->IsAppInstruction()) continue;

      // Convert a function call into a `PUSH; JMP` combination.
      AddTransparentRetAddr(succ.cfi);
      DecodedBasicBlock::Truncate(succ.cfi->Next());
      factory->RequestBlock(succ.block);  // Walk into the call.
      break;  // Won't have any more successors.
    }
  }

  // Instrument the control-flow instructions, specifically: function call
  // instructions.
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      AddRetAddrToBlock(factory, DynamicCast<DecodedBasicBlock *>(block));
    }
  }
};

// Initialize the `transparent_rets` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<TransparentRetsInstrumenterEarly>(
      "transparent_returns_early");

  if (FLAG_transparent_returns) {
    RegisterInstrumentationTool<TransparentRetsInstrumenterLate>(
        "transparent_returns_late", {"transparent_returns_early"});
  }
})
