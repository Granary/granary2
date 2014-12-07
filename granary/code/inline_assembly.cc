/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/driver.h"

#include "granary/base/cstring.h"

#include "granary/cfg/basic_block.h"

#include "granary/code/inline_assembly.h"

#include "granary/breakpoint.h"

namespace granary {

// Initialize this inline assembly scope.
InlineAssemblyScope::InlineAssemblyScope(
    std::initializer_list<const Operand *> inputs)
    : UnownedCountedObject(),
      vars() {
  memset(&vars, 0, sizeof vars);
  memset(&(var_is_initialized[0]), 0, sizeof var_is_initialized);

  for (auto i = 0U; i < kMaxNumInlineVars && i < inputs.size(); ++i) {
    if (auto op = inputs.begin()[i]) {
      vars[i].Construct(*op->Extract());
      var_is_initialized[i] = true;
    }
  }
}

InlineAssemblyScope::~InlineAssemblyScope(void) {}

// Initialize this block of inline assembly.
//
// Note: This will acquire a reference count on the scope referenced by this
//       block.
InlineAssemblyBlock::InlineAssemblyBlock(InlineAssemblyScope *scope_,
                                         const char *assembly_)
    : scope(scope_),
      assembly(assembly_) {
  scope->Acquire();
}

InlineAssemblyBlock::~InlineAssemblyBlock(void) {
  scope->Release();
  if (scope->CanDestroy()) {
    delete scope;
  }
}

// Initialize the inline function call.
InlineFunctionCall::InlineFunctionCall(DecodedBasicBlock *block, AppPC target,
                                       Operand ops[kMaxNumFuncOperands],
                                       size_t num_args_)
    : target_app_pc(target),
      num_args(num_args_) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdynamic-class-memaccess"
  memcpy(args, ops, sizeof(args));
#pragma clang diagnostic pop
  for (auto &arg_reg : arg_regs) {
    arg_reg = block->AllocateVirtualRegister();
  }
}

}  // namespace granary
