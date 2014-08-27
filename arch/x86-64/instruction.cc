/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/decode.h"
#include "arch/x86-64/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Table of all implicit operands.
extern const Operand * const IMPLICIT_OPERANDS[];

// Number of implicit operands for each iclass.
extern const int NUM_IMPLICIT_OPERANDS[];

// Table mapping each iclass to the set of read and written flags by *any*
// selection of that iclass.
GRANARY_IF_DEBUG( extern const FlagsSet IFORM_FLAGS[]; )

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

// Returns true if the instruction modifies the stack pointer by a constant
// value, otherwise returns false.
bool Instruction::ShiftsStackPointer(void) const {
  switch (iclass) {
    case XED_ICLASS_POP:
      return !(ops[0].IsRegister() && ops[0].reg.IsStackPointer());

    case XED_ICLASS_PUSHA:
    case XED_ICLASS_POPA:
    case XED_ICLASS_PUSHAD:
    case XED_ICLASS_POPAD:
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_POPF:
    case XED_ICLASS_PUSHFD:
    case XED_ICLASS_POPFD:
    case XED_ICLASS_PUSHFQ:
    case XED_ICLASS_POPFQ:
    case XED_ICLASS_PUSH:
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_RET_NEAR:
    case XED_ICLASS_ENTER:
      return true;

    case XED_ICLASS_ADD:
    case XED_ICLASS_SUB:
      return ops[0].IsRegister() && ops[0].reg.IsStackPointer() &&
             ops[1].IsImmediate();

    case XED_ICLASS_INC:
    case XED_ICLASS_DEC:
      return ops[0].IsRegister() && ops[0].reg.IsStackPointer();

    case XED_ICLASS_LEA:
      return ops[0].IsRegister() && ops[0].reg.IsStackPointer() &&
             ops[1].IsMemory() && ops[1].is_compound &&
             XED_REG_RSP == ops[1].mem.reg_base &&
             XED_REG_INVALID == ops[1].mem.reg_index &&
             0 != ops[1].mem.disp;

    // Things that appear, but aren't, constant stack pointer shifts that fall
    // into this category are: `IRET`, `CALL_FAR`, `RET_FAR`, and `LEAVE`.
    default:
      return false;
  }
}

// Returns the statically know amount by which an instruction shifts the
// stack pointer.
//
// Note: This should only be used after early mangling, as it assumes an
//       absence of `ENTER` and `LEAVE`.
int Instruction::StackPointerShiftAmount(void) const {
  if (is_stack_blind) return 0;
  int mult = -1;
  switch (iclass) {
    case XED_ICLASS_PUSHA:
    case XED_ICLASS_POPA:
    case XED_ICLASS_PUSHAD:
    case XED_ICLASS_POPAD:
    case XED_ICLASS_PUSHFD:
    case XED_ICLASS_POPFD:
      GRANARY_ASSERT(false);  // Not allowed in 64-bit mode.
      return 0;
    case XED_ICLASS_PUSHF:
      return -2;
    case XED_ICLASS_POPF:
      return 2;
    case XED_ICLASS_PUSHFQ:
      return -8;
    case XED_ICLASS_POPFQ:
      return 8;
    case XED_ICLASS_POP:
      if (!(ops[0].IsRegister() && ops[0].reg.IsStackPointer())) {
        if (-1 != effective_operand_width) {
          return effective_operand_width / 8;
        } else {
          return 8;
        }
      }
      break;
    case XED_ICLASS_PUSH:
      if (-1 != effective_operand_width) {
        return -effective_operand_width / 8;
      } else {
        return -8;
      }
    case XED_ICLASS_CALL_NEAR:
      return -8;
    case XED_ICLASS_RET_NEAR:
      if (ops[0].IsImmediate()) {
        return 8 + static_cast<int>(ops[0].imm.as_uint);
      } else {
        return 8;
      }

    // Assume that this is caught by early mangling, and that no `ENTER`
    // instructions make it into the instruction stream. `LEAVE` does not shift
    // the stack by a constant amount; however, this is a good spot to verify
    // its absence (it should also be early mangled).
    case XED_ICLASS_ENTER:
    case XED_ICLASS_LEAVE:
      GRANARY_ASSERT(false);
      break;

    case XED_ICLASS_ADD:
      mult = 1;

    [[clang::fallthrough]];
    case XED_ICLASS_SUB:
      if (ops[0].IsRegister() && ops[0].reg.IsStackPointer() &&
          ops[1].IsImmediate()) {
        return static_cast<int>(ops[1].imm.as_int) * mult;
      }
      break;

    case XED_ICLASS_INC:
      mult = 1;

    [[clang::fallthrough]];
    case XED_ICLASS_DEC:
      if (ops[0].IsRegister() && ops[0].reg.IsStackPointer()) {
        return mult;
      }
      break;

    case XED_ICLASS_LEA:
      if (ops[0].IsRegister() && ops[0].reg.IsStackPointer() &&
          ops[1].IsMemory() && ops[1].is_compound &&
          XED_REG_RSP == ops[1].mem.reg_base &&
          XED_REG_INVALID == ops[1].mem.reg_index &&
          0 != ops[1].mem.disp) {
        return static_cast<int>(ops[1].mem.disp);
      }
      break;

    default:
      break;
  }
  return 0;
}

// If this instruction computes an address that is below (or possibly below)
// the current stack pointer, then this function returns an estimate on that
// amount. The value returned is either negative or zero.
//
// Note: This should only be used after early mangling.
//
// Note: If a dynamic offset is computed (e.g. stack pointer + register), then
//       an ABI-specific value is returned. For example, for OSes running on
//       x86-64/amd64 architectures, the user space red zone amount (-128) is
//       returned, regardless of if Granary+ is instrumenting user space or
//       kernel code.
int Instruction::ComputedOffsetBelowStackPointer(void) const {
  if (is_stack_blind) return 0;
  if (XED_ICLASS_LEA != iclass) return 0;
  if (!ops[1].is_compound) return 0;
  if (XED_REG_RSP != ops[1].mem.reg_base) return 0;
  if (XED_REG_INVALID == ops[1].mem.reg_index) {
    if (0 <= ops[1].mem.disp) return 0;
    return ops[1].mem.disp;
  } else {
    // TODO(pag): Assume that this is always going up on the stack rather than
    //            down.
    return 0;
    //if (0 <= ops[1].mem.disp) return -128;
    //return ops[1].mem.disp - 128;
  }
}

// Returns true if an instruction reads the flags.
//
// Note: the RFLAGS register is always the last implicit operand.
bool Instruction::ReadsFlags(void) const {
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[isel];
  if (num_implicit_ops) {
    const auto &op(IMPLICIT_OPERANDS[isel][num_implicit_ops - 1]);
    return XED_ENCODER_OPERAND_TYPE_REG == op.type &&
           op.reg.IsFlags() && (op.IsRead() || op.IsConditionalWrite());
  } else {
    GRANARY_ASSERT(!iform || 0 == IFORM_FLAGS[iform].read.flat);
    GRANARY_ASSERT(!has_prefix_rep && !has_prefix_repne);
    return false;
  }
}

// Returns true if an instruction writes to the flags.
//
// Note: the RFLAGS register is always the last operand.
bool Instruction::WritesFlags(void) const {
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[isel];
  if (num_implicit_ops) {
    const auto &op(IMPLICIT_OPERANDS[isel][num_implicit_ops - 1]);
    return XED_ENCODER_OPERAND_TYPE_REG == op.type &&
           op.reg.IsFlags() && op.IsWrite();
  } else {
    return false;
  }
}

namespace {
// Analyze the stack usage of a single instruction.
static void AnalyzeOperandStackUsage(Instruction *instr, const Operand &op) {
  if (XED_ENCODER_OPERAND_TYPE_REG == op.type) {
    if (op.reg.IsStackPointer()) {
      if (op.IsRead() || op.IsConditionalWrite()) {
        instr->reads_from_stack_pointer = true;
      }
      if (op.IsWrite()) instr->writes_to_stack_pointer = true;
    }
  } else if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
    if (op.is_compound) {
      if (XED_REG_RSP == op.mem.reg_base)  {
        instr->reads_from_stack_pointer = true;
      }
    } else {
      if (op.reg.IsStackPointer()) instr->reads_from_stack_pointer = true;
    }
  }
}
}  // namespace

// Analyze this instruction's use of the stack pointer.
void Instruction::AnalyzeStackUsage(void) const {
  analyzed_stack_usage = true;
  reads_from_stack_pointer = false;
  writes_to_stack_pointer = false;
  if (GRANARY_UNLIKELY(is_stack_blind)) return;

  auto self = const_cast<Instruction *>(this);
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    AnalyzeOperandStackUsage(self, op);
  }

  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  for (auto i = 0; i < NUM_IMPLICIT_OPERANDS[isel]; ++i) {
    AnalyzeOperandStackUsage(self, IMPLICIT_OPERANDS[isel][i]);
  }
}

// Get the opcode name.
const char *Instruction::OpCodeName(void) const {
  GRANARY_ASSERT(XED_ICLASS_INVALID < iclass && XED_ICLASS_LAST > iclass);
  return xed_iclass_enum_t2str(iclass);
}

// Get the instruction selected name.
const char *Instruction::ISelName(void) const {
  GRANARY_ASSERT(XED_IFORM_INVALID < iform && XED_IFORM_LAST > iform);
  return xed_iform_enum_t2str(iform);
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
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  auto implicit_ops = IMPLICIT_OPERANDS[isel];
  for (auto i = 0; i < NUM_IMPLICIT_OPERANDS[isel]; ++i) {
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
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[isel];
  const auto implicit_ops = IMPLICIT_OPERANDS[isel];
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
