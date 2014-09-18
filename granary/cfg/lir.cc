/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"

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
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request) {
  auto block = factory->Materialize(target_pc);
  factory->RequestBlock(block, request);
  return Call(block);
}

InlineAssembly::InlineAssembly(std::initializer_list<Operand *> operands)
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
  return new AnnotationInstruction(IA_INLINE_ASSEMBLY, block);
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
