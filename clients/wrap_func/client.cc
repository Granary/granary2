/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/wrap_func/client.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DECLARE_bool(transparent_returns);

// Allows us to select which wrapper to apply when instrumenting this code.
// This prevents infinite recursion in the case of using
// `PASS_INSTRUMENTED_WRAPPED_FUNCTION` to wrap a function, then calling the
// wrapped function.
struct NextWrapperId : public IndexableMetaData<NextWrapperId> {
  NextWrapperId(void)
      : next_wrapper_id(0) {}

  bool Equals(const NextWrapperId &that) const {
    return next_wrapper_id == that.next_wrapper_id;
  }

  // The Id of the next thing to wrap.
  uint8_t next_wrapper_id;
};

namespace {

typedef LinkedListIterator<FunctionWrapper> FunctionWrapperIterator;

// Linked list of wrappers.
static FunctionWrapper *wrappers = nullptr;
static ReaderWriterLock wrappers_lock;

// Returns true if two wrappers wrap the same function.
static bool WrappingSameFunction(FunctionWrapper *a, FunctionWrapper *b) {
  return b &&
         a->module_offset == b->module_offset &&
         StringsMatch(a->module_name, b->module_name);
}

// Returns the place in the wrappers linked list into which a wrapper will
// be placed.
static FunctionWrapper **FunctionWrapperInsertPoint(
    FunctionWrapper *new_wrapper) {
  auto prev = &wrappers;
  for (auto wrapper : FunctionWrapperIterator(wrappers)) {
    if (new_wrapper->module_offset <= wrapper->module_offset &&
        StringsMatch(new_wrapper->module_name, wrapper->module_name)) {

      // Common case: different functions.
      if (GRANARY_LIKELY(new_wrapper->module_offset !=
                         wrapper->module_offset)) {
        break;
      }

      // Uncommon case: two or more wrappers for the same function.
      while (WrappingSameFunction(wrapper, wrapper->next)) {
        wrapper = wrapper->next;
      }

      // Moves to the next wrapper id.
      new_wrapper->id = wrapper->id + 1;
      return &(wrapper->next);
    }
    prev = &(wrapper->next);
  }
  return prev;
}

// Find the wrapper associated with a given block.
static FunctionWrapper *FunctionWrapperFor(DirectBlock *block) {
  auto offset = os::ModuleOffsetOfPC(block->StartAppPC());
  if (!offset.module) return nullptr;

  auto id = GetMetaData<NextWrapperId>(block)->next_wrapper_id;

  ReadLockedRegion locker(&wrappers_lock);
  for (auto wrapper : FunctionWrapperIterator(wrappers)) {
    if (offset.offset == wrapper->module_offset &&
        id == wrapper->id &&
        StringsMatch(offset.module->Name(), wrapper->module_name)) {
      return wrapper;
    }
  }
  return nullptr;
}

}  // namespace

// Register a function wrapper with the wrapper tool.
void AddFunctionWrapper(FunctionWrapper *wrapper) {
  GRANARY_ASSERT(!wrapper->next);
  WriteLockedRegion locker(&wrappers_lock);
  auto insert_point = FunctionWrapperInsertPoint(wrapper);
  wrapper->next = *insert_point;
  *insert_point = wrapper;
  GRANARY_ASSERT(nullptr != wrappers);
}

// Tool that helps to wrap other functions, e.g. `malloc` and `free`.
class FunctionWrapperInstrumenter : public InstrumentationTool {
 public:
  virtual ~FunctionWrapperInstrumenter(void) = default;

  static void Init(InitReason reason) {
    if (kInitThread == reason) return;
    AddMetaData<NextWrapperId>();
  }

  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;
    WriteLockedRegion locker(&wrappers_lock);
    for (; wrappers; ) {
      auto next_wrapper = wrappers->next;
      wrappers->next = nullptr;
      wrappers = next_wrapper;
    }
  }

  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     Trace *cfg) {
    if (!wrappers) return;
    for (auto block : cfg->NewBlocks()) {
      if (auto decoded_block = DynamicCast<DecodedBlock *>(block)) {
        WrapDecodedBlock(factory, decoded_block);
      }
    }
  }

 protected:
  void WrapDecodedBlock(BlockFactory *factory, DecodedBlock *block) {
    for (auto succ : block->Successors()) {

      // Don't allow anyone to materialize blocks that represent code that
      // will be wrapped.
      auto direct_block = DynamicCast<DirectBlock *>(succ.block);
      if (!direct_block) continue;

      auto wrapper = FunctionWrapperFor(direct_block);
      if (!wrapper) continue;

      if (!succ.cfi->IsConditionalJump()) {
        WrapBlock(factory, wrapper, DynamicCast<DecodedBlock *>(block),
                  succ.cfi, direct_block);
      }
    }
  }

  // Note: We use `R10` and `R11` for passing an extra arguments to the
  //       wrappers because the x86-64 Linux ABI has them as a scratch
  //       registers.

  // Used to pass the native return address (through `R11`) to the wrapper.
  void WrapReturnAddress(ControlFlowInstruction *cfi) {
    GRANARY_ASSERT(cfi->IsAppInstruction());

    ImmediateOperand ret_addr(cfi->DecodedPC());
    lir::InlineAssembly asm_(ret_addr);
    if (cfi->IsFunctionCall()) {
      asm_.InlineBefore(cfi, "MOV r64 R11, i64 %0;"_x86_64);
    } else {
      asm_.InlineBefore(cfi, "MOV r64 R11, m64 [RSP];"_x86_64);
    }
  }

  // Pass the native address of the function being wrapped (through `R10`)
  // to the wrapper.
  void WrapNative(ControlFlowInstruction *cfi,
                  DirectBlock *target_block) {

    ImmediateOperand native_addr(target_block->StartAppPC());
    lir::InlineAssembly asm_(native_addr);
    asm_.InlineBefore(cfi, "MOV r64 R10, i64 %0;"_x86_64);
  }

  // Pass the instrumented address of the function being wrapped (through `R10`)
  // to the wrapper.
  //
  // This is careful to preserve the expected meta-data by passing the address
  // of a label that leads to a jump to the instrumented function.
  void WrapInstrumented(BlockFactory *factory, DecodedBlock *block,
                        ControlFlowInstruction *cfi,
                        DirectBlock *target_block) {
    auto label = new LabelInstruction;
    LabelOperand instrumented_addr(label);
    lir::InlineAssembly asm_(instrumented_addr);
    asm_.InlineBefore(cfi, "LEA r64 R10, l %0;"_x86_64);

    // Make sure everyone can update the meta-data, but that no-one will
    // actually be able to materialize the block.
    factory->RequestBlock(target_block, kRequestBlockInFuture);

    // Move the original CFI to the end of the block.
    block->AppendInstruction(label);
    block->AppendInstruction(DecodedBlock::Unlink(cfi));
    if (cfi->IsFunctionCall()) lir::ConvertFunctionCallToJump(cfi);

    auto meta = GetMetaData<NextWrapperId>(target_block);
    meta->next_wrapper_id += 1;
  }

  // Wrap a jump instruction. We need to be careful when using transparent
  // return addresses because we want our wrapper to return to the code cache
  // and not detach from the current thread.
  void WrapJump(BlockFactory *factory, FunctionWrapper *wrapper,
                ControlFlowInstruction *cfi) {
    if (!FLAG_transparent_returns) {
      cfi->InsertAfter(lir::Jump(factory, wrapper->wrapper_pc,
                                 kRequestBlockExecuteNatively));

    // If we're using transparent return addresses, then we inject a "shim"
    // in between a tail-call and its destination that does a real call. This
    // lets us return into the code cache directly with the "right" version
    // information (meta-data).
    //
    // Note: We add in a new block so that the `transparent_returns` tool
    //       picks up on this and specialize it accordingly.
    } else {
      auto target_block = factory->MaterializeEmptyBlock(wrapper->wrapper_pc);
      target_block->AppendInstruction(lir::FunctionCall(
          factory, wrapper->wrapper_pc, kRequestBlockExecuteNatively));
      target_block->AppendInstruction(lir::Return(factory));
      cfi->InsertAfter(lir::Jump(target_block));
    }
  }

  // Try to wrap a block.
  void WrapBlock(BlockFactory *factory, FunctionWrapper *wrapper,
                 DecodedBlock *block, ControlFlowInstruction *cfi,
                 DirectBlock *target_block) {
    WrapReturnAddress(cfi);

    if (PASS_NATIVE_WRAPPED_FUNCTION == wrapper->action) {
      WrapNative(cfi, target_block);
    }

    if (cfi->IsFunctionCall()) {
      cfi->InsertAfter(lir::FunctionCall(factory, wrapper->wrapper_pc,
                                         kRequestBlockExecuteNatively));
    } else if (!cfi->IsConditionalJump()) {
      WrapJump(factory, wrapper, cfi);
    } else {
      // TODO(pag): Handle a conditional jump that is a tail-call.
      GRANARY_ASSERT(false);
    }

    if (PASS_INSTRUMENTED_WRAPPED_FUNCTION == wrapper->action) {
      WrapInstrumented(factory, block, cfi, target_block);
    } else {
      DecodedBlock::Unlink(cfi);
    }
  }
};

// Initialize the `wrap_func` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<FunctionWrapperInstrumenter>("wrap_func");
}
