/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/decode.h"
#include "arch/x86-64/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Table telling us how flags are used by a particular `iclass`.
extern const FlagActions ICLASS_FLAG_ACTIONS[];

Instruction::Instruction(void) {
  memset(this, 0, sizeof *this);
}

Instruction::Instruction(const Instruction &that) {
  memcpy(this, &that, sizeof that);
}

bool Instruction::HasIndirectTarget(void) const {
  // TODO(pag): How to handle XABORT? It's a non-local jump, with no real
  //            target.
  // TODO(pag): Refactor to test `iform` with a `switch`?
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
  if (GRANARY_UNLIKELY(!analyzed_stack_usage)) AnalyzeStackUsage();
  return reads_from_stack_pointer;
}

// Returns true if an instruction writes to the stack pointer.
bool Instruction::WritesToStackPointer(void) const {
  if (GRANARY_UNLIKELY(!analyzed_stack_usage)) AnalyzeStackUsage();
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
             ops[1].IsMemory() && !ops[1].IsPointer() && ops[1].is_compound &&
             ops[1].mem.base.IsStackPointer() &&
             !ops[1].mem.index.IsValid() &&
             1 >= ops[1].mem.scale;

    // Things that appear, but aren't, constant stack pointer shifts that fall
    // into this category are: `IRET`, `CALL_FAR`, `RET_FAR`, and `LEAVE`.
    default:
      return false;
  }
}

// Returns the statically known amount by which an instruction shifts the
// stack pointer.
//
// Note: This should only be used after early mangling, as it assumes an
//       absence of `ENTER` and `LEAVE`.
int Instruction::StackPointerShiftAmount(void) const {
  if (is_stack_blind) return 0;
  int mult = -1;
  switch (iclass) {
    case XED_ICLASS_PUSHA:
      return -8 * arch::WORD_WIDTH_BYTES;

    case XED_ICLASS_POPA:
      return 8 * arch::WORD_WIDTH_BYTES;

    case XED_ICLASS_PUSHAD:
      return -8 * arch::DOUBLEWORD_WIDTH_BYTES;

    case XED_ICLASS_POPAD:
      return 8 * arch::DOUBLEWORD_WIDTH_BYTES;

    case XED_ICLASS_PUSHFD:
      return -arch::DOUBLEWORD_WIDTH_BYTES;

    case XED_ICLASS_POPFD:
      return arch::DOUBLEWORD_WIDTH_BYTES;

    case XED_ICLASS_PUSHF:
      return -arch::WORD_WIDTH_BYTES;

    case XED_ICLASS_POPF:
      return arch::WORD_WIDTH_BYTES;

    case XED_ICLASS_PUSHFQ:
      return -arch::QUADWORD_WIDTH_BYTES;

    case XED_ICLASS_POPFQ:
      return arch::QUADWORD_WIDTH_BYTES;

    case XED_ICLASS_POP:
      if (ops[0].IsRegister() && ops[0].reg.IsStackPointer()) break;
      GRANARY_ASSERT(0 != effective_operand_width);
      return effective_operand_width / arch::BYTE_WIDTH_BITS;
      // TODO(pag): `return 8;` if not `effective_operand_width`?

    case XED_ICLASS_PUSH:
      GRANARY_ASSERT(0 != effective_operand_width);
      return effective_operand_width / -arch::BYTE_WIDTH_BITS;
      // TODO(pag): `return -8;` if not `effective_operand_width`?

    case XED_ICLASS_CALL_NEAR:
      return -arch::ADDRESS_WIDTH_BYTES;

    case XED_ICLASS_RET_NEAR:
      if (ops[0].IsImmediate()) {
        return arch::ADDRESS_WIDTH_BYTES +
               static_cast<uint16_t>(ops[0].imm.as_uint);
      } else {
        return arch::ADDRESS_WIDTH_BYTES;
      }

    // Assume that this is caught by early mangling, and that no `ENTER`
    // instructions make it into the instruction stream. `LEAVE` does not shift
    // the stack by a constant amount; however, this is a good spot to verify
    // its absence (it should also be early mangled).
    //
    // TODO(pag): Might have some enter's or leaves injected by instrumentation.
    case XED_ICLASS_ENTER:
    case XED_ICLASS_LEAVE:
      GRANARY_ASSERT(false);
      break;

    case XED_ICLASS_ADD:
      mult = 1;

    [[clang::fallthrough]];
    case XED_ICLASS_SUB:
      if (!ops[0].IsRegister()) break;
      if (!ops[0].reg.IsStackPointer()) break;
      if (!ops[1].IsImmediate()) break;
      return static_cast<int32_t>(ops[1].imm.as_int) * mult;

    case XED_ICLASS_INC:
      mult = 1;

    [[clang::fallthrough]];
    case XED_ICLASS_DEC:
      if (!ops[0].IsRegister()) break;
      if (!ops[0].reg.IsStackPointer()) break;
      return mult;

    case XED_ICLASS_LEA:
      if (!ops[0].IsRegister()) break;
      if (!ops[0].reg.IsStackPointer()) break;
      if (!ops[1].IsMemory()) break;
      if (ops[1].IsPointer()) break;
      if (!ops[1].is_compound) break;
      if (!ops[1].mem.base.IsStackPointer()) break;
      if (ops[1].mem.index.IsValid()) break;
      if (1 < ops[1].mem.scale) break;
      return static_cast<int32_t>(ops[1].mem.disp);

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
//       returned, regardless of if Granary is instrumenting user space or
//       kernel code.
int Instruction::ComputedOffsetBelowStackPointer(void) const {
  if (is_stack_blind) return 0;
  if (XED_ICLASS_LEA != iclass) return 0;
  if (!ops[1].is_compound) return 0;
  if (!ops[1].mem.base.IsStackPointer()) return 0;
  if (ops[1].mem.index.IsValid()) return 0;
  if (0 <= ops[1].mem.disp) return 0;
  return ops[1].mem.disp;
}

// Returns true if an instruction reads the flags.
//
// Note: the RFLAGS register is always the last implicit operand.
bool Instruction::ReadsFlags(void) const {
  GRANARY_ASSERT(XED_ICLASS_INVALID != iclass);
  return ICLASS_FLAG_ACTIONS[iclass].is_read;
}

// Returns true if an instruction writes to the flags.
//
// Note: the RFLAGS register is always the last operand.
bool Instruction::WritesFlags(void) const {
  GRANARY_ASSERT(XED_ICLASS_INVALID != iclass);
  return ICLASS_FLAG_ACTIONS[iclass].is_write;
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
      GRANARY_ASSERT(!op.mem.index.IsStackPointer());
      if (op.mem.base.IsStackPointer()) {
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
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  GRANARY_ASSERT(num_ops >= num_explicit_ops);

  analyzed_stack_usage = true;
  reads_from_stack_pointer = false;
  writes_to_stack_pointer = false;
  if (GRANARY_UNLIKELY(is_stack_blind)) return;

  auto self = const_cast<Instruction *>(this);
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID != op.type) {
      AnalyzeOperandStackUsage(self, op);
    }
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

// Get the names of the prefixes.
//
// TODO(pag): `XACQUIRE` and `XRELEASE` prefixes?
const char *Instruction::PrefixNames(void) const {
  if (has_prefix_rep) {
    return "REP";
  } else if (has_prefix_repne) {
    return "REPNE";
  } else if (has_prefix_lock) {
    return "LOCK";
  } else {
    return "";
  }
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
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  GRANARY_ASSERT(num_ops >= num_explicit_ops);
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID != op.type) {
      CallWithOperand(&op, func);
    }
  }
}

namespace {

// Returns true if the action of the operand in the instruction matches
// the expected action in the operand matcher.
static bool OperandMatchesAction(const OperandMatcher &m, const Operand &op) {
  const auto is_read = op.IsRead();
  const auto is_write = op.IsWrite();
  if (is_read && is_write) {
    return kOperandActionReadOnly != m.action &&
           kOperandActionWriteOnly != m.action;

  } else if (is_read) {
    return kOperandActionWrite != m.action &&
           kOperandActionWriteOnly != m.action &&
           kOperandActionReadWrite != m.action;

  } else if (is_write) {
    return kOperandActionRead != m.action &&
           kOperandActionReadOnly != m.action &&
           kOperandActionReadWrite != m.action;

  } else {
    GRANARY_ASSERT(false);
    return false;
  }
}

// Returns true of the operand is matched and bound to the operand in the
// matcher.
//
// We use the `OperandMatcher::type` because we might have been passed an
// invalid `Operand`, and so the `m.op` can't tell us its type.
static bool BindOperand(const OperandMatcher &m, Operand *op) {
  if ((op->IsRegister() && kOperandTypeRegister == m.type) ||
      (op->IsMemory() && kOperandTypeMemory == m.type) ||
      (op->IsImmediate() && kOperandTypeImmediate == m.type)) {
    m.op->UnsafeReplace(op);
    return true;
  } else {
    return false;
  }
}

// Returns true of the operand is matched.
//
// TODO(pag): Extend matching beyond register operands.
static bool MatchOperand(const OperandMatcher &m, const Operand &op) {
  return m.op->IsRegister() && op.IsRegister() &&
         op.reg == UnsafeCast<RegisterOperand *>(m.op)->Register();
}

// Try to match an operand, and update the `MatchState` accordingly.
bool TryMatchOperand(const OperandMatcher &m, Operand *op) {
  if (!OperandMatchesAction(m, *op)) return false;
  if (GRANARY_LIKELY(kOperandConstraintBind == m.constraint)) {
    return BindOperand(m, op);
  } else {
    return MatchOperand(m, *op);
  }
}

}  // namespace

// Operand matcher for multiple arguments. Returns the number of matched
// arguments, starting from the first argument.
size_t Instruction::CountMatchedOperands(
    std::initializer_list<OperandMatcher> matchers) {
  GRANARY_ASSERT(XED_IFORM_INVALID != iform);
  GRANARY_ASSERT(0 != isel);
  GRANARY_ASSERT(num_ops >= num_explicit_ops);

  auto matcher_array = matchers.begin();
  auto i = 0UL;
  auto op_num = 0;

  for (const auto max_i = matchers.size(); i < max_i; ++op_num) {
    auto &op(ops[op_num]);
    if (!op.IsValid()) break;
    if (TryMatchOperand(matcher_array[i], &op)) {
      ++i;
    }
  }
  return i;
}

}  // namespace arch
}  // namespace granary
