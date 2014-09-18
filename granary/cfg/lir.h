/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_LIR_H_
#define GRANARY_CFG_LIR_H_

#include "arch/context.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/cfg/factory.h"

#include "granary/code/inline_assembly.h"

namespace granary {

class BasicBlock;
class Instruction;
class AnnotationInstruction;
class Operand;

namespace lir {

// Indirect jump to an existing basic block.
std::unique_ptr<Instruction> IndirectJump(BasicBlock *target_block,
                                          const Operand &op);

// Call / jump to existing basic blocks.
std::unique_ptr<Instruction> Call(BasicBlock *target_block);
std::unique_ptr<Instruction> Jump(BasicBlock *target_block);

// Materialize a direct basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request=REQUEST_LATER);

// Materialize a direct basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request=REQUEST_LATER);

// Materialize a return from a function.
std::unique_ptr<Instruction> Return(BlockFactory *factory);

std::unique_ptr<Instruction> Jump(const LabelInstruction *target_instr);

// Call to a client function that takes in an argument to an
// `arch::MachineContext` pointer.
//
// A context call does not allow one to see intermediate virtual register
// state. Therefore, context calls do not have access to virtual registers.
// This limits there applicability to places where the instrumentation code
// wants to see the native machine context as it would be without
std::unique_ptr<Instruction> CallWithContext(
    void (*func)(arch::MachineContext *));

#if 0
template <typename R, typename... Args>
std::unique_ptr<Instruction> CallWithArgs(R (*func)(Args...),
                                          std::initializer_list<Operand *>) {

}
#endif

// TODO(pag): InlineCall, inline the code of a function directly into the
//            code.

// Represents a block of inline assembly.
class InlineAssembly {
 public:
  explicit InlineAssembly(std::initializer_list<Operand *> operands);
  ~InlineAssembly(void);

  // Inline some assembly code before `instr`, but only if `cond` is true.
  // Returns the inlined instruction, or `instr` if `cond` is false.
  template <typename... Strings>
  inline Instruction *InlineBeforeIf(Instruction *instr, bool cond,
                                     Strings... lines) {
    if (cond) {
      return InlineBefore(instr, {lines...});
    } else {
      return instr;
    }
  }

  // Inline some assembly code before `instr`. Returns the inlined instruction.
  template <typename... Strings>
  inline Instruction *InlineBefore(Instruction *instr, Strings... lines) {
    return InlineBefore(instr, {lines...});
  }

  // Inline some assembly code after `instr`, but only if `cond` is true.
  // Returns the inlined instruction, or `instr` if `cond` is false.
  template <typename... Strings>
  inline Instruction *InlineAfterIf(Instruction *instr, bool cond,
                                    Strings... lines) {
    if (cond) {
      return InlineAfter(instr, {lines...});
    } else {
      return instr;
    }
  }

  // Inline some assembly code after `instr`. Returns the inlined instruction.
  template <typename... Strings>
  Instruction *InlineAfter(Instruction *instr, Strings... lines) {
    return InlineAfter(instr, {lines...});
  }

  // Inline some assembly code before `instr`. Returns the inlined instruction.
  Instruction *InlineBefore(Instruction *instr,
                            std::initializer_list<const char *> lines);

  // Inline some assembly code after `instr`. Returns the inlined instruction.
  Instruction *InlineAfter(Instruction *instr,
                           std::initializer_list<const char *> lines);

 private:
  InlineAssembly(void) = delete;

  GRANARY_POINTER(InlineAssemblyScope) *scope;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InlineAssembly);
};

}  // namespace lir
}  // namespace granary

#endif  // GRANARY_CFG_LIR_H_
