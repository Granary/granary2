/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"

#include <granary.h>

#include "clients/util/closure.h"

using namespace granary;

namespace {

class FunctionWrapper {
 public:
  FunctionWrapper *next;

  // Name of the module to which the function being wrapped belongs.
  const char *module_name;

  // Offset of the function to be wrapped from within its module.
  uint64_t module_offset;

  // The wrapper function.
  AppPC wrapper_func;

  // Number of arguments that need to be passed along to the wrapped
  // function.
  uint8_t num_args;

  // Do we want to be passed a function pointer to the instrumented version
  // of the wrapped function?
  bool pass_instrumented;

  // Do we want to be passed a function pointer to the native version of
  // the wrapped function?
  bool pass_native;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(FunctionWrapper);
};

typedef LinkedListIterator<FunctionWrapper> FunctionWrapperIterator;

// Linked list of wrappers.
static FunctionWrapper *wrappers = nullptr;
static ReaderWriterLock wrappers_lock;

// Returns the place in the wrappers linked list into which a wrapper will
// be placed.
FunctionWrapper **FunctionWrapperInsertPoint(const char *module_name,
                                             uint64_t module_offset) {
  auto prev = &wrappers;
  for (auto wrapper : FunctionWrapperIterator(wrappers)) {
    if (module_offset <= wrapper->module_offset &&
        StringsMatch(module_name, wrapper->module_name)) {
      break;
    }
    prev = &(wrapper->next);
  }
  return prev;
}

enum FunctionWrapperKind {
  WRAP_CALL,
  WRAP_TAIL_CALL
};

}  // namespace

// Tool that helps instrument `malloc` and `free` functions.
class FunctionWrapperInstrumenter : public InstrumentationTool {
 public:
  virtual ~FunctionWrapperInstrumenter(void) = default;

  virtual void Exit(ExitReason) {
    WriteLockedRegion locker(&wrappers_lock);
    for (; wrappers;) {
      auto next_wrapper = wrappers->next;
      delete wrappers;
      wrappers = next_wrapper;
    }
  }

  // Step 1: Find calls/jumps to stuff that we want to wrap, and make sure
  //         that those things are not materialized.
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {

        // Don't allow anyone to materialize blocks that represent code that
        // will be wrapped.
        auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block);
        if (direct_block && FunctionWrapperFor(direct_block)) {
          factory->RequestBlock(direct_block, REQUEST_DENIED);
        }
      }
    }
  }

  // Step 2: For each wrappable target, add in the wrapping stuff.
  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    for (auto succ : block->Successors()) {
      if (auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block)) {
        if (auto wrapper = FunctionWrapperFor(direct_block)) {
          if (succ.cfi->IsJump()) {
            WrapBlock(wrapper, succ.cfi, direct_block, WRAP_TAIL_CALL);
          } else if (succ.cfi->IsFunctionCall()) {
            WrapBlock(wrapper, succ.cfi, direct_block, WRAP_CALL);
          }
        }
      }
    }
  }

 protected:

  // Find the wrapper associated with a given block.
  FunctionWrapper *FunctionWrapperFor(DirectBasicBlock *block) {
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

  // Try to wrap a block.
  void WrapBlock(FunctionWrapper *wrapper, ControlFlowInstruction *cfi,
                 DirectBasicBlock *block, FunctionWrapperKind kind) {
    GRANARY_UNUSED(wrapper);
    GRANARY_UNUSED(cfi);
    GRANARY_UNUSED(block);
    GRANARY_UNUSED(kind);
    GRANARY_UNUSED(FunctionWrapperInsertPoint);
  }
};

// Initialize the `wrap_func` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<FunctionWrapperInstrumenter>("wrap_func");
})
