/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/wrap_func/wrap_func.h"

using namespace granary;

namespace {

typedef LinkedListIterator<FunctionWrapper> FunctionWrapperIterator;

// Linked list of wrappers.
static FunctionWrapper *wrappers = nullptr;
static ReaderWriterLock wrappers_lock;

// Returns the place in the wrappers linked list into which a wrapper will
// be placed.
static FunctionWrapper **FunctionWrapperInsertPoint(const char *module_name,
                                                    uint64_t module_offset) {
  auto prev = &wrappers;
  for (auto wrapper : FunctionWrapperIterator(wrappers)) {
    if (module_offset <= wrapper->module_offset &&
        StringsMatch(module_name, wrapper->module_name)) {
      GRANARY_ASSERT(module_offset != wrapper->module_offset);
      break;
    }
    prev = &(wrapper->next);
  }
  return prev;
}

// Find the wrapper associated with a given block.
static FunctionWrapper *FunctionWrapperFor(DirectBasicBlock *block) {
  auto offset = os::ModuleOffsetOfPC(block->StartAppPC());
  if(!offset.module) return nullptr;

  ReadLockedRegion locker(&wrappers_lock);
  for (auto wrapper : FunctionWrapperIterator(wrappers)) {
    if (offset.offset == wrapper->module_offset &&
        StringsMatch(offset.module->Name(), wrapper->module_name)) {
      return wrapper;
    }
  }

  return nullptr;
}

}  // namespace

// Register a function wrapper with the wrapper tool.
void RegisterFunctionWrapper(FunctionWrapper *wrapper) {
  GRANARY_ASSERT(!wrapper->next);
  WriteLockedRegion locker(&wrappers_lock);
  auto insert_point = FunctionWrapperInsertPoint(wrapper->module_name,
                                                 wrapper->module_offset);
  wrapper->next = *insert_point;
  *insert_point = wrapper;
}

// Tool that helps instrument `malloc` and `free` functions.
class FunctionWrapperInstrumenter : public InstrumentationTool {
 public:
  virtual ~FunctionWrapperInstrumenter(void) = default;

  virtual void Exit(ExitReason) {
    WriteLockedRegion locker(&wrappers_lock);
    for (; wrappers; ) {
      auto next_wrapper = wrappers->next;
      wrappers->next = nullptr;
      wrappers = next_wrapper;
    }
  }

  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {

        // Don't allow anyone to materialize blocks that represent code that
        // will be wrapped.
        auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block);
        if (!direct_block) continue;

        auto wrapper = FunctionWrapperFor(direct_block);
        if (!wrapper) continue;

        if (!succ.cfi->IsConditionalJump()) {
          WrapBlock(factory, wrapper, DynamicCast<DecodedBasicBlock *>(block),
                    succ.cfi, direct_block);
        }
      }
    }
  }

 protected:

  // Note: We use `R10` for passing an extra argument to the wrapper because
  //       the x86-64 Linux ABI has that as a scratch register.

  void WrapNative(ControlFlowInstruction *cfi,
                  DirectBasicBlock *target_block) {
    ImmediateOperand native_addr(target_block->StartAppPC());
    lir::InlineAssembly asm_(native_addr);
    asm_.InlineBefore(cfi, "MOV r64 R10, i64 %0"_x86_64);
  }

  void WrapInstrumented(BlockFactory *factory, DecodedBasicBlock *block,
                        ControlFlowInstruction *cfi,
                        DirectBasicBlock *target_block) {
    auto label = new LabelInstruction;
    LabelOperand instrumented_addr(label);
    lir::InlineAssembly asm_(instrumented_addr);
    asm_.InlineBefore(cfi, "LEA r64 R10, l %0"_x86_64);

    // Make sure everyone can update the meta-data, but that no-one will
    // actually be able to materialize the block.
    factory->RequestBlock(target_block, REQUEST_DENIED);

    // Move the original CFI to the end of the block.
    block->AppendInstruction(label);
    block->AppendInstruction(DecodedBasicBlock::Unlink(cfi));
    if (cfi->IsFunctionCall()) lir::ConvertFunctionCallToJump(cfi);
  }

  // Try to wrap a block.
  void WrapBlock(BlockFactory *factory, FunctionWrapper *wrapper,
                 DecodedBasicBlock *block, ControlFlowInstruction *cfi,
                 DirectBasicBlock *target_block) {

    if (PASS_NATIVE_WRAPPED_FUNCTION == wrapper->action) {
      WrapNative(cfi, target_block);
    }

    if (cfi->IsFunctionCall()) {
      cfi->InsertAfter(lir::FunctionCall(factory, wrapper->wrapper_pc,
                                         REQUEST_NATIVE));
    } else if (!cfi->IsConditionalJump()) {
      cfi->InsertAfter(lir::Jump(factory, wrapper->wrapper_pc,
                                  REQUEST_NATIVE));
    } else {
      // TODO(pag): Handle a conditional jump that is a tail-call.
      GRANARY_ASSERT(false);
    }

    if (PASS_INSTRUMENTED_WRAPPED_FUNCTION == wrapper->action) {
      WrapInstrumented(factory, block, cfi, target_block);
    } else {
      DecodedBasicBlock::Unlink(cfi);
    }
  }
};

// Initialize the `wrap_func` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<FunctionWrapperInstrumenter>("wrap_func");
})
