/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_LIR_H_
#define GRANARY_CFG_LIR_H_

#include "arch/context.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"
#include "granary/base/type_trait.h"

#include "granary/cfg/factory.h"

#include "granary/code/inline_assembly.h"

namespace granary {

class BasicBlock;
class DecodedBasicBlock;
class Instruction;
class AnnotationInstruction;
class ControlFlowInstruction;
class Operand;

namespace lir {

// Indirect jump to an existing basic block.
std::unique_ptr<Instruction> IndirectJump(BasicBlock *target_block,
                                          const Operand &op);

// Call / jump to existing basic blocks.
std::unique_ptr<Instruction> FunctionCall(BasicBlock *target_block);
std::unique_ptr<Instruction> Jump(BasicBlock *target_block);

// Materialize a direct basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request=REQUEST_LATER);

// Materialize a direct basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> FunctionCall(
    BlockFactory *factory, AppPC target_pc,
    BlockRequestKind request=REQUEST_LATER);

// Materialize a return from a function.
std::unique_ptr<Instruction> Return(BlockFactory *factory);

std::unique_ptr<Instruction> Jump(const LabelInstruction *target_instr);

// Conversion functions.
void ConvertFunctionCallToJump(ControlFlowInstruction *cfi);
void ConvertJumpToFunctionCall(ControlFlowInstruction *cfi);

struct TranslationContext {
  GRANARY_POINTER(ContextInterface) *granary_context;
};

// Call to a client function that takes in an argument to a granary context and
// to an `arch::MachineContext` pointer.
//
// A context call does not allow one to see intermediate virtual register
// state. Therefore, context calls do not have access to virtual registers.
// This limits there applicability to places where the instrumentation code
// wants to see the native machine context as it would be without
std::unique_ptr<Instruction> ContextFunctionCall(
    void (*func)(TranslationContext, arch::MachineContext *));

namespace detail {

inline static void InitInlineOp(Operand *op, const RegisterOperand &arg_op) {
  new (op) RegisterOperand(arg_op);
}

inline static void InitInlineOp(Operand *op, const RegisterOperand &&arg_op) {
  new (op) RegisterOperand(arg_op);
}

inline static void InitInlineOp(Operand *op, const ImmediateOperand &arg_op) {
  new (op) ImmediateOperand(arg_op);
}

inline static void InitInlineOp(Operand *op, const ImmediateOperand &&arg_op) {
  new (op) ImmediateOperand(arg_op);
}

inline static void InitInlineOp(Operand *op, const MemoryOperand &arg_op) {
  new (op) MemoryOperand(arg_op);
}

inline static void InitInlineOp(Operand *op, const MemoryOperand &&arg_op) {
  new (op) MemoryOperand(arg_op);
}

inline static void InitInlineOp(Operand *op, VirtualRegister reg) {
  new (op) RegisterOperand(reg);
}

template <typename T, typename EnableIf<IsPointer<T>::RESULT>::Type=0>
inline static void InitInlineOp(Operand *op, T ptr) {
  new (op) ImmediateOperand(ptr);
}

template <typename T, typename EnableIf<IsInteger<T>::RESULT>::Type=0>
inline static void InitInlineOp(Operand *op, T ptr) {
  new (op) ImmediateOperand(ptr);
}

inline static void InitInlineOps(Operand *, size_t, size_t) {}

template <typename T, typename... Args>
inline static void InitInlineOps(Operand *ops, size_t i, size_t num,
                                 T arg, Args... args) {
  if (i >= num || i >= MAX_NUM_FUNC_OPERANDS) return;
  InitInlineOp(&(ops[i]), arg);
  InitInlineOps(ops, i + 1, num, args...);
}

// Insert a "outline" call to some client code. This call can have access to
// virtual registers by means of its arguments. At least one argument is
// required.
std::unique_ptr<Instruction> InlineFunctionCall(DecodedBasicBlock *block,
                                                AppPC func_addr, Operand *ops,
                                                size_t num_args);

}  // namespace detail

// Insert a "outline" call to some client code. This call can have access to
// virtual registers by means of its arguments. At least one argument is
// required.
template <typename FuncT, typename... Args>
inline static std::unique_ptr<Instruction> InlineFunctionCall(
    DecodedBasicBlock *block, FuncT func, Args... args) {
  Operand ops[MAX_NUM_FUNC_OPERANDS];
  auto num_args = sizeof...(args);
  detail::InitInlineOps(ops, 0UL, num_args, args...);
  return detail::InlineFunctionCall(block, UnsafeCast<AppPC>(func), ops,
                                    num_args);
}

// TODO(pag): InlineCall, inline the code of a function directly into the
//            code.

// Represents a block of inline assembly.
class InlineAssembly {
 public:
  inline InlineAssembly(void)
      : InlineAssembly({}) {}

  template <typename... OperandTs>
  inline explicit InlineAssembly(OperandTs&... ops)
      : InlineAssembly({&ops...}) {}

  explicit InlineAssembly(std::initializer_list<Operand *> operands);

  ~InlineAssembly(void);

  // Inline some assembly code before `instr`, but only if `cond` is true.
  // Returns the inlined instruction, or `instr` if `cond` is false.
  template <typename... Strings>
  inline Instruction *InlineBeforeIf(Instruction *instr, bool cond,
                                     const char *line, Strings... lines) {
    if (cond) {
      return InlineBefore(instr, {line, lines...});
    } else {
      return instr;
    }
  }

  // Inline some assembly code before `instr`. Returns the inlined instruction.
  template <typename... Strings>
  inline Instruction *InlineBefore(Instruction *instr, const char *line,
                                   Strings... lines) {
    return InlineBefore(instr, {line, lines...});
  }

  // Inline some assembly code after `instr`, but only if `cond` is true.
  // Returns the inlined instruction, or `instr` if `cond` is false.
  template <typename... Strings>
  inline Instruction *InlineAfterIf(Instruction *instr, bool cond,
                                    const char *line, Strings... lines) {
    if (cond) {
      return InlineAfter(instr, {line, lines...});
    } else {
      return instr;
    }
  }

  // Inline some assembly code after `instr`. Returns the inlined instruction.
  template <typename... Strings>
  Instruction *InlineAfter(Instruction *instr, const char *line,
                           Strings... lines) {
    return InlineAfter(instr, {line, lines...});
  }

  // Inline some assembly code before `instr`. Returns the inlined instruction.
  Instruction *InlineBefore(Instruction *instr,
                            std::initializer_list<const char *> lines);

  // Inline some assembly code after `instr`. Returns the inlined instruction.
  Instruction *InlineAfter(Instruction *instr,
                           std::initializer_list<const char *> lines);

  // Gives access to one of the registers defined within the inline assembly.
  RegisterOperand &Register(DecodedBasicBlock *block, int reg_num) const;

 private:
  GRANARY_POINTER(InlineAssemblyScope) *scope;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InlineAssembly);
};

}  // namespace lir
}  // namespace granary

#endif  // GRANARY_CFG_LIR_H_
