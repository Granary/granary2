/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/decode.h"
#include "granary/arch/x86-64/instruction.h"

namespace granary {
namespace arch {

// Table of all implicit operands.
extern const Operand * const IMPLICIT_OPERANDS[];

// Number of implicit operands for each iclass.
extern const int NUM_IMPLICIT_OPERANDS[];

Instruction::Instruction(void) {
  memset(this, 0, sizeof *this);
  iclass = XED_ICLASS_INVALID;
  category = XED_CATEGORY_INVALID;
}

Instruction::Instruction(const Instruction &that) {
  memcpy(this, &that, sizeof that);
}

bool Instruction::HasIndirectTarget(void) const {
  if (IsFunctionCall() || IsUnconditionalJump()) {
    return XED_ENCODER_OPERAND_TYPE_REG == ops[0].type ||
           XED_ENCODER_OPERAND_TYPE_MEM == ops[0].type ||
           XED_ENCODER_OPERAND_TYPE_PTR == ops[0].type;
  } else {
    return IsFunctionReturn() || IsInterruptCall() || IsInterruptReturn() ||
           IsSystemCall() || IsSystemReturn();
  }
}

// Returns true if an instruction reads from the stack pointer.
bool Instruction::ReadsFromStackPointer(void) const {
  if (GRANARY_UNLIKELY(!analyzed_stack_usage)) {
    AnalyzeStackUsage();
  }
  return reads_from_stack_pointer;
}

// Returns true if an instruction writes to the stack pointer.
bool Instruction::WritesToStackPointer(void) const {
  if (GRANARY_UNLIKELY(!analyzed_stack_usage)) {
    AnalyzeStackUsage();
  }
  return writes_to_stack_pointer;
}

// Returns true if an instruction reads the flags.
//
// Note: the RFLAGS register is always the last implicit operand.
bool Instruction::ReadsFlags(void) const {
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[iclass];
  const auto &op(IMPLICIT_OPERANDS[iclass][num_implicit_ops - 1]);
  return XED_ENCODER_OPERAND_TYPE_REG == op.type &&
         op.reg.IsFlags() && op.IsRead();
}

// Returns true if an instruction writes to the flags.
//
// Note: the RFLAGS register is always the last operand.
bool Instruction::WritesFlags(void) const {
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[iclass];
  const auto &op(IMPLICIT_OPERANDS[iclass][num_implicit_ops - 1]);
  return XED_ENCODER_OPERAND_TYPE_REG == op.type &&
         op.reg.IsFlags() && op.IsWrite();
}

namespace {
// Analyze the stack usage of a single instruction.
static void AnalyzeOperandStackUsage(Instruction *instr, const Operand &op) {
  if (XED_ENCODER_OPERAND_TYPE_REG == op.type) {
    if (op.reg.IsStackPointer()) {
      if (op.IsRead()) {
        instr->reads_from_stack_pointer = true;
      }
      if (op.IsWrite()) {
        instr->writes_to_stack_pointer = true;
      }
    }
  } else if (XED_ENCODER_OPERAND_TYPE_MEM == op.type && op.is_compound &&
             XED_REG_RSP == op.mem.reg_base) {
    instr->reads_from_stack_pointer = true;
  }
}
}  // namespace

// Analyze this instruction's use of the stack pointer.
void Instruction::AnalyzeStackUsage(void) const {
  analyzed_stack_usage = true;
  auto self = const_cast<Instruction *>(this);
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    AnalyzeOperandStackUsage(self, op);
  }
  for (auto i = 0; i < NUM_IMPLICIT_OPERANDS[iclass]; ++i) {
    AnalyzeOperandStackUsage(self, IMPLICIT_OPERANDS[iclass][i]);
  }
}

// Get the opcode name.
const char *Instruction::OpCodeName(void) const {
  return xed_iclass_enum_t2str(iclass);
}

namespace {

// Invoke a function on a `arch::Operand` that has been converted into a
// `granary::Operand`.
static void CallWithOperand(
    Operand *op, const std::function<void(granary::Operand *)> &func) {
  switch (op->type) {
    case XED_ENCODER_OPERAND_TYPE_REG:
    case XED_ENCODER_OPERAND_TYPE_SEG0:
    case XED_ENCODER_OPERAND_TYPE_SEG1: {
      RegisterOperand reg(op);
      func(reinterpret_cast<granary::Operand *>(&reg));
      break;
    }
    case XED_ENCODER_OPERAND_TYPE_BRDISP:
    case XED_ENCODER_OPERAND_TYPE_IMM0:
    case XED_ENCODER_OPERAND_TYPE_SIMM0:
    case XED_ENCODER_OPERAND_TYPE_IMM1: {
      ImmediateOperand imm(op);
      func(reinterpret_cast<granary::Operand *>(&imm));
      break;
    }
    case XED_ENCODER_OPERAND_TYPE_MEM:
    case XED_ENCODER_OPERAND_TYPE_PTR: {
      MemoryOperand mem(op);
      func(reinterpret_cast<granary::Operand *>(&mem));
      break;
    }
    default: break;  // TODO(pag): Implement others.
  }
}
}  // namespace

// Invoke a function on the branch target, where the branch target is treated
// as a `granary::Operand`.
void Instruction::WithBranchTargetOperand(
    const std::function<void(granary::Operand *)> &func) {
  CallWithOperand(&(ops[0]), func);
}

// Invoke a function on every operand.
void Instruction::ForEachOperand(
    const std::function<void(granary::Operand *)> &func) {
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    CallWithOperand(&op, func);
  }
  auto implicit_ops = IMPLICIT_OPERANDS[iclass];
  for (auto i = 0; i < NUM_IMPLICIT_OPERANDS[iclass]; ++i) {
    auto implicit_op = const_cast<Operand *>(&(implicit_ops[i]));
    CallWithOperand(implicit_op, func);
  }
}

namespace {

// Returns true if the action of the operand in the instruction matches
// the expected action in the operand matcher.
static bool OperandMatchesAction(const OperandMatcher &matcher,
                                 const Operand &op) {
  auto is_read = op.IsRead();
  auto is_write = op.IsWrite();
  if (is_read && is_write) {
    if (OperandAction::READ_ONLY == matcher.action ||
        OperandAction::WRITE_ONLY == matcher.action) {
      return false;
    }
  } else if (is_read) {
    if (OperandAction::ANY != matcher.action &&
        OperandAction::READ != matcher.action &&
        OperandAction::READ_ONLY != matcher.action) {
      return false;
    }
  } else if (is_write) {
    if (OperandAction::ANY != matcher.action &&
        OperandAction::WRITE != matcher.action &&
        OperandAction::WRITE_ONLY != matcher.action) {
      return false;
    }
  }
  return true;
}

// Returns true of the operand is matched and bound to the operand in the
// matcher.
static bool BindOperand(OperandMatcher matcher, Operand *op) {
  if ((op->IsRegister() && IsA<RegisterOperand *>(matcher.op)) ||
      (op->IsMemory() && IsA<MemoryOperand *>(matcher.op)) ||
      (op->IsImmediate() && IsA<ImmediateOperand *>(matcher.op))) {
    matcher.op->UnsafeReplace(op);
    return true;
  } else {
    return false;
  }
}

// Returns true of the operand is matched.
//
// TODO(pag): Extend matching beyond register operands.
static bool MatchOperand(OperandMatcher matcher, const Operand &op) {
  auto reg_op = DynamicCast<RegisterOperand *>(matcher.op);
  return (op.IsRegister() && reg_op && op.reg == reg_op->Register());
}

struct MatchState {
  size_t num_matched;
  bool was_matched[Instruction::MAX_NUM_OPS];
};

// Try to match an operand, and update the `MatchState` accordingly.
bool TryMatchOperand(MatchState *state, OperandMatcher m, Operand *op, int i) {
  if (state->was_matched[i] || !OperandMatchesAction(m, *op)) {
    return false;
  }
  if (GRANARY_LIKELY(OperandConstraint::BIND == m.constraint)) {
    if (!BindOperand(m, op)) {
      return false;
    }
  } else if (!MatchOperand(m, *op)) {
    return false;
  }

  state->was_matched[i] = true;
  ++state->num_matched;
  return true;
}

}  // namespace

// Operand matcher for multiple arguments. Returns the number of matched
// arguments, starting from the first argument.
size_t Instruction::CountMatchedOperands(
    std::initializer_list<OperandMatcher> &&matchers) {
  MatchState state = {0, {false}};
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[iclass];
  const auto implicit_ops = IMPLICIT_OPERANDS[iclass];
  for (auto m : matchers) {
    int op_num = 0;
    auto matched = false;
    for (auto &op : ops) {
      if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
        break;
      }
      if ((matched = TryMatchOperand(&state, m, &op, op_num++))) {
        break;
      }
    }
    if (!matched) {  // Try to match against implicit operands.
      for (auto i = 0; i < num_implicit_ops; ++i) {
        auto op = const_cast<Operand *>(&(implicit_ops[i]));
        if ((matched = TryMatchOperand(&state, m, op, op_num++))) {
          break;
        }
      }
      if (!matched) {  // Didn't match against anything; give up.
        return state.num_matched;
      }
    }
  }
  return state.num_matched;
}

}  // namespace arch
}  // namespace granary