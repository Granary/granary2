/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/inline_assembly.h"

#include "arch/x86-64/instruction.h"
#include "arch/x86-64/select.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Categories of every iclass.
extern xed_category_enum_t ICLASS_CATEGORIES[];

// Table mapping each iclass to the set of read and written flags by *any*
// selection of that iclass.
extern FlagsSet IFORM_FLAGS[];

}  // namespace arch

namespace {

// Not very pretty, but implements a simple top-down parser for parsing
// Granary's inline assembly instructions.
class InlineAssemblyParser {
 public:
  InlineAssemblyParser(LocalControlFlowGraph *cfg_,
                       InlineAssemblyScope *scope_,
                       DecodedBasicBlock *block_,
                       Instruction *instr_,
                       const char *ch_)
      : op(nullptr),
        num_immediates(0),
        cfg(cfg_),
        scope(scope_),
        block(block_),
        instr(instr_),
        ch(ch_),
        branch_target(nullptr) {}

  // Parse the inline assembly as a sequence of instructions.
  void ParseInstructions(void) {
    char buff[20] = {'\0'};
    while (*ch) {
      bool is_locked = false;
      bool is_rep = false;
      bool is_repne = false;
    get_opcode:
      ConsumeWhiteSpace();
      ParseWord(buff);
      if (buff[0]) {
        if (StringsMatch(buff, "LOCK")) {
          is_locked = true;
          goto get_opcode;
        } else if (StringsMatch(buff, "REP") || StringsMatch(buff, "REPE")) {
          is_rep = true;
          goto get_opcode;
        } else if (StringsMatch(buff, "REPNE")) {
          is_repne = true;
          goto get_opcode;
        } else if (StringsMatch(buff, "LABEL")) {
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
          data.has_prefix_lock = is_locked;
          data.has_prefix_rep = is_rep;
          data.has_prefix_repne = is_repne;
          FinalizeInstruction();
        }
      }
    }
  }

 private:

  bool Peek(char next) {
    return *ch == next;
  }

  void Accept(char GRANARY_IF_DEBUG(next)) {
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
      if (PeekWhitespace() || Peek(';') || Peek(',') ||
          Peek(':') || Peek('[') || Peek(']')) {
        break;
      }
      *buff++ = *ch++;
    }
    *buff = '\0';
  }

  // Get a label.
  AnnotationInstruction *GetLabel(unsigned var_num)  {
    if (!scope->var_is_initialized[var_num]) {
      scope->var_is_initialized[var_num] = true;
      scope->vars[var_num].label = new LabelInstruction;
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
    op->type = XED_ENCODER_OPERAND_TYPE_REG;
    op->reg.DecodeFromNative(static_cast<int>(reg));
  }

  void ParseInPlaceOp(void) {
    auto var_num = ParseVar();
    GRANARY_ASSERT(scope->var_is_initialized[var_num]);
    auto &untyped_op(scope->vars[var_num].mem);  // Operand containers overlap.
    auto seg = op->segment;
    memcpy(op, untyped_op->Extract(), sizeof *op);
    if (op->IsMemory()) op->segment = seg;
  }

  // Parses a segment op.
  void ParseSegmentOp(void) {
    if (Peek('F')) {
      Accept('F'); Accept('S'); Accept(':');
      op->segment = XED_REG_FS;
    } else if (Peek('G')) {
      Accept('G'); Accept('S'); Accept(':');
      op->segment = XED_REG_GS;
    }
  }

  // TODO(pag): Only supports base form right now, i.e. `[%0]`, `FS:[%0]`, and
  //            `GS:[%0]`, and not the full base/index/scale/displacement form.
  void ParseMemoryOp(void) {
    Accept('[');
    ConsumeWhiteSpace();
    if (Peek('%')) {
      auto var_num = ParseVar();
      GRANARY_ASSERT(scope->var_is_initialized[var_num]);
      auto &reg_op(scope->vars[var_num].reg);
      op->reg = reg_op->Register();
    } else {
      ParseArchReg();
      ConsumeWhiteSpace();
      if (Peek('+')) {
        Accept('+');
        ConsumeWhiteSpace();

        op->is_compound = true;
        op->mem.reg_base = static_cast<xed_reg_enum_t>(
            op->reg.EncodeToNative());
        op->mem.reg_index = XED_REG_INVALID;
        op->mem.scale = 0;

        char buff[20] = {'\0'};
        ParseWord(buff);
        if ('0' == buff[0] && ('x' == buff[1] || 'X' == buff[1])) {
          DeFormat(buff, "%x", &(op->mem.disp));
        } else {
          DeFormat(buff, "%d", &(op->mem.disp));
        }
      }
    }
    op->type = XED_ENCODER_OPERAND_TYPE_MEM;
    ConsumeWhiteSpace();
    Accept(']');
  }

  // Like labels, this will create/initialize a new reg op if it isn't already
  // initialized.
  void ParseRegOp(void) {
    auto is_def = false;
    if (Peek('@')) {
      char buff[20] = {'\0'};
      ParseWord(buff);
      is_def = StringsMatch("@def", buff);
      ConsumeWhiteSpace();
    }
    auto var_num = ParseVar();
    auto &reg_op(scope->vars[var_num].reg);
    if (!scope->var_is_initialized[var_num]) {
      op->type = XED_ENCODER_OPERAND_TYPE_REG;
      op->reg = block->AllocateVirtualRegister();
      reg_op->UnsafeReplace(op);
      scope->var_is_initialized[var_num] = true;
    } else {
      memcpy(op, reg_op->Extract(), sizeof *op);
    }
    op->is_definition = is_def;
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    auto num = 0UL;
    GRANARY_IF_DEBUG( auto got = ) DeFormat(&(buff[offset]), format, &num);
    GRANARY_ASSERT(1 == got);
#pragma clang diagnostic pop

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
      op->branch_target.as_app_pc = nullptr;
      op->is_annotation_instr = true;
      op->annotation_instr = branch_target;
    } else if ('m' == op_type || 'i' == op_type || 'r' == op_type) {
      auto width = ParseWidth();
      ConsumeWhiteSpace();
      switch (op_type) {
        case 'm':  // Memory.
          ParseSegmentOp();
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
    int16_t op_size = 0;
    GRANARY_ASSERT(nullptr != xedi);
    auto i = 0U;
    for (auto &instr_op : data.ops) {
      if (XED_ENCODER_OPERAND_TYPE_INVALID == instr_op.type) {
        break;
      } else {
        auto xedi_op = xed_inst_operand(xedi, i++);
        instr_op.rw = xed_operand_rw(xedi_op);
        instr_op.is_explicit = true;
        if (instr_op.IsRegister() && instr_op.reg.IsNative() &&
            !instr_op.reg.IsGeneralPurpose()) {
          instr_op.is_sticky = true;
        }
        op_size = std::max(op_size, instr_op.width);
      }
    }
    if (XED_CATEGORY_PUSH == data.category) {
      op_size = arch::ADDRESS_WIDTH_BITS;
    }
    data.effective_operand_width = op_size;
  }

  // Finalize the instruction by adding it to the basic block's instruction
  // list.
  void FinalizeInstruction(void) {
    Instruction *new_instr(nullptr);
    FixupOperands();

    // TODO(pag): For the time being, I allow instrumentation instructions to
    //            read/write from the stack pointer; however, this is valid
    //            if and only if these instructions do not operate within a
    //            loop. If they do get put inside a loop then step 9 in
    //            `9_allocate_slots.cc` likely will not terminate.
    data.AnalyzeStackUsage();

    // Ensure that instrumentation instructions do not alter the direction
    // flag! This is because we have no reliable way of saving and restoring
    // the direction flag (lest we use `PUSHF` and `POPF`) when the stack
    // pointer is not known to be valid.
    GRANARY_IF_DEBUG( auto flags = arch::IFORM_FLAGS[data.iform]; )
    GRANARY_ASSERT(!flags.written.s.df);

    if (data.IsJump()) {
      GRANARY_ASSERT(nullptr != branch_target);
      new_instr = new BranchInstruction(&data, branch_target);
    } else if (data.IsFunctionCall()) {
      auto bb = new NativeBasicBlock(
          data.HasIndirectTarget() ? nullptr : data.BranchTargetPC());
      cfg->AddBlock(bb);
      new_instr = new ControlFlowInstruction(&data, bb);
    } else if (data.IsFunctionReturn()) {
      auto bb = new ReturnBasicBlock(cfg, nullptr /* no meta-data */);
      cfg->AddBlock(bb);
      new_instr = new ControlFlowInstruction(&data, bb);

    // Allows for injecting of `INT3`s at convenient locations.
    } else if (data.IsInterruptCall()) {
      data.analyzed_stack_usage = false;
      data.is_stack_blind = true;
      new_instr = new NativeInstruction(&data);

    } else {
      new_instr = new NativeInstruction(&data);
    }
    instr->InsertBefore(new_instr);
  }

  // Holds an in-progress instructions.
  arch::Instruction data;

  // The next operand to decode.
  arch::Operand *op;

  // The number of immediates already seen.
  int num_immediates;

  // The control-flow graph; used to materialize basic blocks.
  LocalControlFlowGraph *cfg;

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
  AnnotationInstruction *branch_target;
};
}  // namespace

namespace arch {

// Compile this inline assembly into some instructions within the block
// `block`. This places the inlined instructions before `instr`, which is
// assumed to be the `AnnotationInstruction` containing the inline assembly
// instructions.
void CompileInlineAssemblyBlock(LocalControlFlowGraph *cfg,
                                DecodedBasicBlock *block,
                                granary::Instruction *instr,
                                InlineAssemblyBlock *asm_block) {
  InlineAssemblyParser parser(cfg, asm_block->scope, block,
                              instr, asm_block->assembly);
  parser.ParseInstructions();
}

}  // namespace arch
}  // namespace granary
