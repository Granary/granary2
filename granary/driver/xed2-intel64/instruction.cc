/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/driver/decode.h"
#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

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

// Get the opcode name.
const char *Instruction::OpCodeName(void) const {
  return xed_iclass_enum_t2str(iclass);
}

// Invoke a function on every operand.
void Instruction::ForEachOperand(
    std::function<void(granary::Operand *)> &&func) {
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    switch (op.type) {
      case XED_ENCODER_OPERAND_TYPE_REG:
      case XED_ENCODER_OPERAND_TYPE_SEG0:
      case XED_ENCODER_OPERAND_TYPE_SEG1: {
        RegisterOperand reg(&op);
        func(reinterpret_cast<granary::Operand *>(&reg));
        break;
      }

      case XED_ENCODER_OPERAND_TYPE_BRDISP:
      case XED_ENCODER_OPERAND_TYPE_IMM0:
      case XED_ENCODER_OPERAND_TYPE_SIMM0:
      case XED_ENCODER_OPERAND_TYPE_IMM1: {
        ImmediateOperand imm(&op);
        func(reinterpret_cast<granary::Operand *>(&imm));
        break;
      }
      case XED_ENCODER_OPERAND_TYPE_MEM:
      case XED_ENCODER_OPERAND_TYPE_PTR: {
        MemoryOperand mem(&op);
        func(reinterpret_cast<granary::Operand *>(&mem));
        break;
      }
      default: break;  // TODO(pag): Implement others.
    }
  }
}

// Operand matcher for multiple arguments. Returns the number of matched
// arguments, starting from the first argument.
size_t Instruction::CountMatchedOperands(
    std::initializer_list<OperandMatcher> &&matchers) {

  size_t num_matched = 0;
  bool was_matched[sizeof(ops)] = {false};

  for (auto m : matchers) {
    auto matched = false;

    for (size_t i = 0; i < sizeof(ops); ++i) {
      auto &op(ops[i]);
      if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
        return num_matched;
      } else if (was_matched[i]) {
        continue;
      }

      // Try to reject this match based on the operand's action.
      auto is_read = op.IsRead();
      auto is_write = op.IsWrite();
      if (is_read && is_write) {
        if (OperandAction::READ_ONLY == m.action ||
            OperandAction::WRITE_ONLY == m.action) {
          continue;
        }
      } else if (is_read) {
        if (OperandAction::ANY != m.action &&
            OperandAction::READ != m.action &&
            OperandAction::READ_ONLY != m.action) {
          continue;
        }
      } else if (is_write) {
        if (OperandAction::ANY != m.action &&
            OperandAction::WRITE != m.action &&
            OperandAction::WRITE_ONLY != m.action) {
          continue;
        }
      }

      // Match and bind by operand type.
      if ((op.IsRegister() && IsA<RegisterOperand *>(m.op)) ||
          (op.IsMemory() && IsA<MemoryOperand *>(m.op)) ||
          (op.IsImmediate() && IsA<ImmediateOperand *>(m.op))) {
        m.op->UnsafeReplace(&op);
        was_matched[i] = matched = true;
        ++num_matched;
        break;
      }
    }

    // If we didn't match the `OperandMatcher` against any operand then give up
    // and don't proceed to the next `OperandMatcher`.
    if (!matched) {
      return num_matched;
    }
  }
  return num_matched;
}

}  // namespace driver
}  // namespace granary
