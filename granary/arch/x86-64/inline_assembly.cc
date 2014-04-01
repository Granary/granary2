/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/inline_assembly.h"

#include "granary/arch/x86-64/instruction.h"
#include "granary/arch/x86-64/select.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {
// Categories of every iclass.
extern xed_category_enum_t ICLASS_CATEGORIES[];

// Number of implicit operands for each iclass.
extern const int NUM_IMPLICIT_OPERANDS[];

}  // namespace arch
namespace {

// Not very pretty, but implements a simple top-down parser for parsing
// Granary's inline assembly instructions.
class InlineAssemblyParser {
 public:
  InlineAssemblyParser(InlineAssemblyScope *scope_,
                       DecodedBasicBlock *block_,
                       Instruction *instr_,
                       const char *ch_)
      : op(nullptr),
        num_immediates(0),
        scope(scope_),
        block(block_),
        instr(instr_),
        ch(ch_),
        branch_target(nullptr) {}

  // Parse the inline assembly as a sequence of instructions.
  void ParseInstructions(void) {
    char buff[20] = {'\0'};
    while (*ch) {
      ConsumeWhiteSpace();
      ParseWord(buff);
      if (buff[0]) {
        if (StringsMatch(buff, "LABEL")) {
          ParseLabel();
          Accept(':');
        } else {
          memset(&data, 0, sizeof data);
          auto iclass = str2xed_iclass_enum_t(buff);
          GRANARY_ASSERT(XED_ICLASS_INVALID != iclass);
          data.iclass = iclass;
          data.category = arch::ICLASS_CATEGORIES[iclass];
          op = &(data.ops[0]);
          num_immediates = 0;
          branch_target = nullptr;
          do {
            if (Peek(',')) {
              Accept(',');
            }
            ConsumeWhiteSpace();
          } while (ParseOperand());
          Accept(';');
          FinalizeInstruction();
        }
      }
    }
  }

 private:

  bool Peek(char next) {
    return *ch == next;
  }

  void Accept(char next) {
    GRANARY_ASSERT(Peek(next));
    ch++;
  }

  bool PeekWhitespace(void) {
    return ' ' == *ch || '\t' == *ch || '\n' == *ch;
  }

  void ConsumeWhiteSpace(void) {
    for (; *ch && PeekWhitespace(); ++ch) {}
  }

  void ParseWord(char *buff) {
    for (; *ch; ) {
      if (PeekWhitespace() || Peek(';') || Peek(',') || Peek(':')) {
        break;
      }
      *buff++ = *ch++;
    }
    *buff = '\0';
  }

  // Get a label.
  LabelInstruction *GetLabel(unsigned var_num)  {
    if (!scope->var_is_initialized.Get(var_num)) {
      scope->var_is_initialized.Set(var_num, true);
      scope->vars[var_num].label = new LabelInstruction();
    }
    return scope->vars[var_num].label;
  }

  void ParseLabel(void) {
    ConsumeWhiteSpace();
    auto var_num = ParseVar();
    auto label = GetLabel(var_num);
    instr->InsertBefore(std::unique_ptr<Instruction>(label));
  }

  int ParseNumber(void) {
    char buff[20] = {'\0'};
    ParseWord(buff);
    int num = -1;
    GRANARY_IF_DEBUG( auto got = ) DeFormat(buff, "%d", &num);
    GRANARY_ASSERT(1 == got);
    return num;
  }

  unsigned ParseVar(void) {
    Accept('%');
    auto num = ParseNumber();
    GRANARY_ASSERT(0 <= num && MAX_NUM_INLINE_VARS > num);
    return static_cast<unsigned>(num);
  }

  int ParseWidth(void) {
    auto width = ParseNumber();
    GRANARY_ASSERT(8 == width || 16 == width || 32 == width || 64 == width);
    return width;
  }

  void ParseArchReg(void) {
    char buff[20] = {'\0'};
    ParseWord(buff);
    auto reg = str2xed_reg_enum_t(buff);
    GRANARY_ASSERT(XED_REG_INVALID != reg);
    op->reg.DecodeFromNative(static_cast<int>(reg));
  }

  void ParseInPlaceOp(void) {
    auto var_num = ParseVar();
    GRANARY_ASSERT(scope->var_is_initialized.Get(var_num));
    auto &untyped_op(scope->vars[var_num].mem);  // Operand containers overlap.
    memcpy(op, untyped_op->Extract(), sizeof *op);
  }

  // TODO(pag): Only supports base form right now, i.e. `[%0]` and not the full
  //            `segment:[disp + base + index * scale]` form.
  void ParseMemoryOp(void) {
    Accept('[');
    ConsumeWhiteSpace();
    auto var_num = ParseVar();
    GRANARY_ASSERT(scope->var_is_initialized.Get(var_num));
    auto &reg_op(scope->vars[var_num].reg);
    op->type = XED_ENCODER_OPERAND_TYPE_MEM;
    op->reg = reg_op->Register();
    ConsumeWhiteSpace();
    Accept(']');
  }

  // Like labels, this will create/initialize a new reg op if it isn't already
  // initialized.
  void ParseRegOp(void) {
    auto var_num = ParseVar();
    auto &reg_op(scope->vars[var_num].reg);
    if (!scope->var_is_initialized.Get(var_num)) {
      op->type = XED_ENCODER_OPERAND_TYPE_REG;
      op->reg = block->AllocateVirtualRegister();
      reg_op->UnsafeReplace(op);
      scope->var_is_initialized.Set(var_num, true);
    } else {
      memcpy(op, reg_op->Extract(), sizeof *op);
    }
  }

  void ParseImmediate(void) {
    char buff[20] = {'\0'};
    ParseWord(buff);
    auto negate = false;
    auto offset = 0;

    if (buff[0] == '-') {
      negate = true;
      offset = 1;
    }

    auto format = "%lu";
    if ('0' == buff[offset] &&
        ('x' == buff[offset + 1] || 'X' == buff[offset + 1])) {
      offset += 2;
      format = "%lx";
    }

    auto num = 0UL;
    GRANARY_IF_DEBUG( auto got = ) DeFormat(&(buff[offset]), format, &num);
    GRANARY_ASSERT(1 == got);

    if (!num_immediates) {
      op->type = negate ? XED_ENCODER_OPERAND_TYPE_SIMM0
                        : XED_ENCODER_OPERAND_TYPE_IMM0;
      num = negate ? static_cast<unsigned long>(-static_cast<long>(num)) : num;
      num_immediates++;
    } else {
      op->type = XED_ENCODER_OPERAND_TYPE_IMM1;

    }
    op->imm.as_uint = num;
  }

  // Parse an inline assembly operand.
  bool ParseOperand(void) {
    auto op_type = *ch++;
    if ('l' == op_type) {  // Handle labels as special cases.
      ConsumeWhiteSpace();
      branch_target = GetLabel(ParseVar());
      op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
      op->rw = XED_OPERAND_ACTION_R;
    } else if ('m' == op_type || 'i' == op_type || 'r' == op_type) {
      auto width = ParseWidth();
      ConsumeWhiteSpace();
      switch (op_type) {
        case 'm':  // Memory.
          if (Peek('%')) {
            ParseInPlaceOp();
          } else {
            ParseMemoryOp();
          }
          break;
        case 'i':  // Immediate.
          if (Peek('%')) {
            ParseInPlaceOp();
          } else {
            ParseImmediate();
          }
          break;
        case 'r':  // Register.
          if (Peek('%')) {
            ParseRegOp();
          } else {
            ParseArchReg();
          }
          op->reg.Widen(width / 8);
          break;
        default: break;
      }
      op->width = static_cast<decltype(op->width)>(width);
    } else {
      ch--;
      return false;
    }
    ++data.num_explicit_ops;
    ++op;
    return true;
  }

  // Fix-up the operands by matching the instruction to a specific instruction
  // selection, and then super-imposing the r/w actions of those operands onto
  // the assembled operands.
  void FixupOperands(void) {
    auto xedi = SelectInstruction(&data);
    GRANARY_ASSERT(nullptr != xedi);
    auto i = 0U;
    for (auto &instr_op : data.ops) {
      if (XED_ENCODER_OPERAND_TYPE_INVALID == instr_op.type) {
        break;
      } else {
        auto xedi_op = xed_inst_operand(xedi, i++);
        instr_op.rw = xed_operand_rw(xedi_op);
        instr_op.is_explicit = true;
      }
    }
  }

  // Finalize the instruction by adding it to the basic block's instruction
  // list.
  void FinalizeInstruction(void) {
    std::unique_ptr<Instruction> new_instr;
    FixupOperands();
    if (data.IsJump()) {
      GRANARY_ASSERT(nullptr != branch_target);
      new_instr.reset(new BranchInstruction(&data, branch_target));
    } else {
      new_instr.reset(new NativeInstruction(&data));
    }
    instr->InsertBefore(std::move(new_instr));
  }

  // Holds an in-progress instructions.
  arch::Instruction data;

  // The next operand to decode.
  arch::Operand *op;

  // The number of immediates already seen.
  int num_immediates;

  // Scope from which local/input variables can be looked up.
  InlineAssemblyScope * const scope;

  // Basic block into which instructions are being placed. Used to allocate
  // new virtual registers.
  DecodedBasicBlock * const block;

  // Instruction before which all assembly instructions will be placed.
  Instruction * const instr;

  // The next character to parse.
  const char *ch;

  // Label that is the target of this instruction, in the event that this
  // instruction is a branch.
  LabelInstruction *branch_target;
};
}  // namespace

// Compile this inline assembly into some instructions within the block
// `block`. This places the inlined instructions before `instr`, which is
// assumed to be the `AnnotationInstruction` containing the inline assembly
// instructions.
void InlineAssemblyBlock::Compile(DecodedBasicBlock *block,
                                  Instruction *instr) {
  InlineAssemblyParser parser(scope, block, instr, assembly);
  parser.ParseInstructions();
}

}  // namespace granary
