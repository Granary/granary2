/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"

#include "granary/code/inline_assembly.h"

#include "granary/translate.h"

namespace granary {
namespace lir {

// Materialize a future basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request) {
  auto block = factory->Materialize(target_pc);
  factory->RequestBlock(block, request);
  return Jump(block);
}

// Materialize a future basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> FunctionCall(BlockFactory *factory,
                                          AppPC target_pc,
                                          BlockRequestKind request) {
  auto block = factory->Materialize(target_pc);
  factory->RequestBlock(block, request);
  return FunctionCall(block);
}

// Translate an entrypoint.
CachePC TranslationContext::TranslateEntryPoint(AppPC target_pc,
                                                EntryPointKind kind,
                                                int category) {
  return granary::TranslateEntryPoint(context, target_pc, kind, category,
                                      kTargetStackUnknown);
}

// Call to a client function that takes in an argument to a granary context and
// to an `arch::MachineContext` pointer.
std::unique_ptr<Instruction> ContextFunctionCall(
    void (*func)(TranslationContext, arch::MachineContext *)) {
  return std::unique_ptr<Instruction>(new AnnotationInstruction(
      kAnnotContextFunctionCall, func));
}

namespace detail {
// Insert a "outline" call to some client code. This call can have access to
// virtual registers by means of its arguments. At least one argument is
// required.
std::unique_ptr<Instruction> InlineFunctionCall(DecodedBasicBlock *block,
                                                AppPC func_addr, Operand *ops,
                                                size_t num_args) {
  return std::unique_ptr<Instruction>(new AnnotationInstruction(
      kAnnotInlineFunctionCall,
      new granary::InlineFunctionCall(block, func_addr, ops, num_args)));
}

}  // namespace detail

InlineAssembly::InlineAssembly(std::initializer_list<const Operand *> operands)
    : scope(new InlineAssemblyScope(operands)) {}

InlineAssembly::~InlineAssembly(void) {
  if (scope->CanDestroy()) {
    delete scope;
  }
  scope = nullptr;
}

namespace {
// Make a new inline assembly instruction.
static Instruction *MakeInlineAssembly(InlineAssemblyScope *scope,
                                       const char *line) {
  auto block = new InlineAssemblyBlock(scope, line);
  return new AnnotationInstruction(kAnnotInlineAssembly, block);
}
}  // namespace

// Inline some assembly code before `instr`. Returns the inlined instruction.
Instruction *InlineAssembly::InlineBefore(
    Instruction *instr, std::initializer_list<const char *> lines) {
  for (auto line : lines) {
    if (line) {
      instr = instr->InsertBefore(MakeInlineAssembly(scope, line));
    }
  }
  return instr;
}

// Inline some assembly code after `instr`. Returns the inlined instruction.
Instruction *InlineAssembly::InlineAfter(
    Instruction *instr, std::initializer_list<const char *> lines) {
  for (auto line : lines) {
    if (line) {
      instr = instr->InsertAfter(MakeInlineAssembly(scope, line));
    }
  }
  return instr;
}

}  // namespace lir
}  // namespace granary
