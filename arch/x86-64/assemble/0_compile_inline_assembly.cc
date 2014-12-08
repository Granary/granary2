/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/inline_assembly.h"

#include "arch/x86-64/builder.h"
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
        cfg(cfg_),
        scope(scope_),
        block(block_),
        instr(instr_),
        ch(ch_),
        num_immediates(0) {}

  void ParseInstructions(void) {
    while (*ch) {
      ConsumeWhiteSpace();
      if (!*ch) break;
      memset(&data, 0, sizeof data);
      num_immediates = 0;
      op = &(data.ops[0]);
      ParseInstruction();
    }
  }

 private:
  // Returns true if this instruction uses an effective address operand.
  //
  // Note: These need to be kept consistent with `ConvertMemoryOperand` in
  //       `decode.cc` and with `MemoryBuilder::Build`.
  //
  // TODO(pag): This should be turned into a utility function.
  bool IsEffectiveAddress(void) {
    return XED_ICLASS_BNDCL == data.iclass ||
           XED_ICLASS_BNDCN == data.iclass ||
           XED_ICLASS_BNDCU == data.iclass ||
           XED_ICLASS_BNDMK == data.iclass ||
           XED_ICLASS_CLFLUSH == data.iclass ||
           XED_ICLASS_CLFLUSHOPT == data.iclass ||
           XED_ICLASS_LEA == data.iclass ||
           (XED_ICLASS_PREFETCHNTA <= data.iclass &&
            XED_ICLASS_PREFETCH_RESERVED >= data.iclass);
  }

  unsigned ParseVar(void) {
    Accept('%');
    ParseWord();
    unsigned var_num = kMaxNumInlineVars;
    DeFormat(buff, "%u", &var_num);
    GRANARY_ASSERT(kMaxNumInlineVars > var_num);
    return var_num;
  }

  // Get a label.
  void InitLabelVar(unsigned var_num)  {
    auto &aop(scope->vars[var_num]);
    if (!scope->var_is_initialized[var_num]) {
      scope->var_is_initialized[var_num] = true;
      aop->annotation_instr = new LabelInstruction;
      aop->is_annotation_instr = true;
      aop->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
      aop->width = arch::ADDRESS_WIDTH_BITS;
    }
  }

  // Get a virtual register.
  void InitRegVar(unsigned var_num) {
    auto &aop(scope->vars[var_num]);
    if (!scope->var_is_initialized[var_num]) {
      scope->var_is_initialized[var_num] = true;
      aop->reg = block->AllocateVirtualRegister();
      aop->type = XED_ENCODER_OPERAND_TYPE_REG;
      aop->width = arch::GPR_WIDTH_BITS;
    }
  }

  // Parse a label instruction.
  void ParseLabelInstruction(void) {
    auto var_num = ParseVar();
    InitLabelVar(var_num);
    instr->InsertBefore(scope->vars[var_num]->annotation_instr);
  }

  // Parse the next thing as an explicitly state architectural register.
  VirtualRegister ParseArchRegister(void) {
    ParseWord();
    ConsumeWhiteSpace();
    return VirtualRegister::FromNative(static_cast<int>(
        str2xed_reg_enum_t(buff)));
  }

  // Parse the next thisng as an already initialized variable operand, and
  // return the virtual register associated with that operand.
  VirtualRegister ParseRegisterVar(void) {
    auto var_num = ParseVar();
    ConsumeWhiteSpace();
    GRANARY_ASSERT(scope->var_is_initialized[var_num]);
    GRANARY_ASSERT(scope->vars[var_num]->IsRegister());
    return scope->vars[var_num]->reg.WidenedTo(arch::GPR_WIDTH_BYTES);
  }

  // Parse the next thing as a generic, already initialized variable.
  const arch::Operand &ParseOperandVar(void) {
    auto var_num = ParseVar();
    ConsumeWhiteSpace();
    GRANARY_ASSERT(scope->var_is_initialized[var_num]);
    return *(scope->vars[var_num]);
  }

  // Treat this memory operand as a pointer.
  void ParsePointerOperand(void) {
    op->type = XED_ENCODER_OPERAND_TYPE_PTR;
    ParseWord();
    if ('0' == buff[0]) {
      DeFormat(buff, "%lx", &(op->addr.as_uint));
    } else {
      DeFormat(buff, "%lu", &(op->addr.as_uint));
    }
  }

  // Parse a compound memory operand. This is quite tricky and almost nearly
  // handles the full generality of base/disp memory operands, with the ability
  // to mix in input virtual registers and immediates, as well as literal
  // registers and immediates for the various components.
  void ParseCompoundMemoryOperand(void) {
    enum {
      ParseReg,
      InterpretRegAsBase,
      InterpretRegAsIndex,
      TryParseIndexOrDisp,
      ParseScale,
      ParseDisp
    } state = ParseReg;

    VirtualRegister reg;
    Accept('[');
    ConsumeWhiteSpace();

    if ('0' <= *ch && '9' >= *ch) {  // Point.
      ParsePointerOperand();
      ConsumeWhiteSpace();
      Accept(']');
      return;
    }

    for (; !Peek(']');) {
      switch (state) {
        case ParseReg:
          if (Peek('%')) {
            reg = ParseRegisterVar();
          } else {
            reg = ParseArchRegister();
          }
          if (Peek('+') || Peek(']')) {
            state = InterpretRegAsBase;
            // Fall-through.

          } else if (Peek('*')) {
            state = InterpretRegAsIndex;
            break;
          }

        [[clang::fallthrough]];
        case InterpretRegAsBase:
          GRANARY_ASSERT(reg.IsValid());
          op->mem.base = reg;
          if (Peek('+')) {
            Accept('+');
            ConsumeWhiteSpace();
            state = TryParseIndexOrDisp;
          } else {
            GRANARY_ASSERT(Peek(']'));
          }
          break;

        case InterpretRegAsIndex:
          GRANARY_ASSERT(reg.IsValid());
          op->mem.index = reg;
          op->mem.scale = 1;
          if (Peek('*')) {
            Accept('*');
            ConsumeWhiteSpace();
            state = ParseScale;
          } else if (Peek('+')) {
            Accept('+');
            ConsumeWhiteSpace();
            state = ParseDisp;
          } else {
            GRANARY_ASSERT(false);
          }
          break;

        case TryParseIndexOrDisp:
          if ('0' <= *ch && '9' >= *ch) {  // Literal displacement.
            state = ParseDisp;

          } else if ('A' <= *ch && 'Z' >= *ch) {  // Index arch reg.
            reg = ParseArchRegister();
            state = InterpretRegAsIndex;

          } else if (Peek('%')) {  // Index var reg, or displacement imm reg.
            auto &aop(ParseOperandVar());
            if (aop.IsRegister()) {
              reg = aop.reg.WidenedTo(arch::GPR_WIDTH_BYTES);
              state = InterpretRegAsIndex;

            } else if (aop.IsImmediate()) {
              op->mem.disp = static_cast<int32_t>(aop.imm.as_int);
              GRANARY_ASSERT(op->mem.disp == aop.imm.as_int);
              goto done;

            } else {
              GRANARY_ASSERT(false);
            }
          } else {
            GRANARY_ASSERT(false);
          }
          break;

        case ParseScale:
          ParseWord();
          ConsumeWhiteSpace();
          switch (buff[0]) {
            case '1': op->mem.scale = 1; break;
            case '2': op->mem.scale = 2; break;
            case '4': op->mem.scale = 4; break;
            case '8': op->mem.scale = 8; break;
            default: GRANARY_ASSERT(false); break;
          }
          if (Peek('+')) {
            Accept('+');
            ConsumeWhiteSpace();
            state = ParseDisp;
          } else {
            goto done;
          }
          break;

        case ParseDisp:
          if ('0' <= *ch && '9' >= *ch) {  // Literal displacement.
            ParseWord();
            if ('0' == buff[0]) {
              DeFormat(buff, "%x", &(op->mem.disp));
            } else {
              DeFormat(buff, "%d", &(op->mem.disp));
            }
          } else {
            auto &aop(ParseOperandVar());
            GRANARY_ASSERT(aop.IsImmediate());
            op->mem.disp = static_cast<int32_t>(aop.imm.as_int);
            GRANARY_ASSERT(op->mem.disp == aop.imm.as_int);
          }
          goto done;
      }
    }
  done:
    GRANARY_ASSERT(op->mem.base.IsValid() || op->mem.index.IsValid());
    ConsumeWhiteSpace();
    Accept(']');

    op->type = XED_ENCODER_OPERAND_TYPE_MEM;
    op->is_compound = op->mem.disp || 1 < op->mem.scale ||
                      (op->mem.base.IsValid() && op->mem.index.IsValid());

    // Canonicalize.
    if (!op->is_compound && op->mem.index.IsValid()) {
      op->mem.base = op->mem.index;
      op->mem.index = VirtualRegister();
      op->mem.scale = 0;
    }
  }

  // Parse a memory operand. This might be a compound memory operand, or it
  // might reference an input operand. This alos might need to mark memory
  // operands as not actually reading/writing from/to memory.
  void ParseMemoryOperand(void) {
    auto seg_reg = XED_REG_INVALID;
    if (Peek('[')) {
      ParseCompoundMemoryOperand();

    } else if (Peek('F')) {
      ParseWord();
      GRANARY_ASSERT(StringsMatch(buff, "FS"));
      seg_reg = XED_REG_FS;
      Accept(':');
      ParseCompoundMemoryOperand();

    } else if (Peek('G')) {
      ParseWord();
      GRANARY_ASSERT(StringsMatch(buff, "GS"));
      seg_reg = XED_REG_GS;
      Accept(':');
      ParseCompoundMemoryOperand();

    } else if (Peek('%')) {
      auto var_num = ParseVar();
      auto &aop(scope->vars[var_num]);
      if (aop->IsRegister()) {
        seg_reg = static_cast<xed_reg_enum_t>(aop->reg.EncodeToNative());
        GRANARY_ASSERT(seg_reg && XED_REG_DS != seg_reg);
        Accept(':');
        ParseCompoundMemoryOperand();
      } else {
        GRANARY_ASSERT(aop->IsMemory());
        *op = *aop;
      }
    }
    op->segment = seg_reg;
    op->is_effective_address = IsEffectiveAddress();
    GRANARY_ASSERT(!(op->is_effective_address && op->segment));
  }

  // Parse a virtual register.
  void ParseVirtRegisterOperand(void) {
    auto var_num = ParseVar();
    InitRegVar(var_num);
    *op = *(scope->vars[var_num]);
  }

  // Parse an explicitly specified architectural register.
  void ParseArchRegisterOperand(void) {
    ParseWord();
    auto reg = str2xed_reg_enum_t(buff);
    GRANARY_ASSERT(XED_REG_INVALID != reg);
    op->reg.DecodeFromNative(static_cast<int>(reg));
    op->type = XED_ENCODER_OPERAND_TYPE_REG;
  }

  // Parse a register operand. This might be a virtual or explicitly specified
  // architectural register.
  void ParseRegisterOperand(unsigned width) {
    if (Peek('%')) {
      ParseVirtRegisterOperand();
    } else {
      ParseArchRegisterOperand();
    }
    op->reg.Widen(static_cast<int>(width / arch::BYTE_WIDTH_BITS));
  }

  // Parse an immediate literal operand.
  void ParseImmediateLiteralOperand(void) {
    ParseWord();
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

  // Parse an immediate operand.
  void ParseImmediateOperand(void) {
    if (Peek('%')) {
      auto var_num = ParseVar();
      GRANARY_ASSERT(scope->var_is_initialized[var_num]);
      auto &aop(scope->vars[var_num]);
      GRANARY_ASSERT(aop->IsImmediate());
      *op = *aop;

    } else {
      ParseImmediateLiteralOperand();
    }
  }

  // Used in branch targets and calculation of effective addresses, for later
  // indirect jumps, return address stuff, etc.
  void ParseLabelOperand(void) {
    auto var_num = ParseVar();
    InitLabelVar(var_num);
    auto &aop(scope->vars[var_num]);
    auto annot_instr = aop->annotation_instr;

    // Increment the refcount; for branch instructions, the `BranchInstruction`
    // class does this.
    if (!data.IsJump()) {
      if (auto label_instr = DynamicCast<LabelInstruction *>(annot_instr)) {
        label_instr->DataRef<uintptr_t>() += 1;
      }
    }

    *op = *aop;
    if ((op->is_effective_address = IsEffectiveAddress())) {
      op->type = XED_ENCODER_OPERAND_TYPE_PTR;
    }
  }

  // Parse a generic operand.
  void ParseOperand(void) {
    char type = '\0';
    unsigned width = 0;
    ParseWord();
    DeFormat(buff, "%c%u", &type, &width);
    ConsumeWhiteSpace();
    switch (type) {
      case 'm': ParseMemoryOperand(); break;
      case 'r': ParseRegisterOperand(width); break;
      case 'i': ParseImmediateOperand(); break;
      case 'l': ParseLabelOperand(); break;
      default: GRANARY_ASSERT(false); break;
    }

    op->width = static_cast<uint16_t>(width);
    op->rw = XED_OPERAND_ACTION_INVALID;
    op->is_explicit = true;
    op->is_sticky = false;
  }

  void ParseInstructionPrefixes(void) {
    for (;;) {
      ConsumeWhiteSpace();
      ParseWord();
      if (!buff[0]) return;

      if (StringsMatch(buff, "LOCK")) {
        data.has_prefix_lock = true;
      } else if (StringsMatch(buff, "REP") || StringsMatch(buff, "REPE")) {
        data.has_prefix_rep = true;
      } else if (StringsMatch(buff, "REPNE")) {
        data.has_prefix_repne = true;
      } else {
        break;
      }
    }
  }

  // Parse this instruction as a label instruction.
  //
  // Note: This re-uses `buff` from `ParseInstructionPrefixes`.
  bool TryParseLabelInstruction(void) {
    if (StringsMatch(buff, "LABEL")) {
      ConsumeWhiteSpace();
      ParseLabelInstruction();
      Accept(':');
      return true;
    }
    return false;
  }

  // Parse the opcode of the instruction.
  //
  // Note: This re-uses `buff` from `ParseInstructionPrefixes`.
  void ParseInstructionOpcode(void) {
    data.iclass = str2xed_iclass_enum_t(buff);
    GRANARY_ASSERT(XED_ICLASS_INVALID != data.iclass);
    data.category = arch::ICLASS_CATEGORIES[data.iclass];
  }


  // Fix-up the operands by matching the instruction to a specific instruction
  // selection, and then super-imposing the r/w actions of those operands onto
  // the assembled operands.
  void FixupOperands(void) {
    auto xedi = SelectInstruction(&data);
    GRANARY_ASSERT(nullptr != xedi);

    FinalizeInstruction(&data);
    uint16_t op_size = 0;

    for (auto i = 0U; i < data.num_explicit_ops; ++i) {
      auto &instr_op(data.ops[i]);
      GRANARY_ASSERT(XED_ENCODER_OPERAND_TYPE_INVALID != instr_op.type);

      auto xedi_op = xed_inst_operand(xedi, i);
      instr_op.rw = xed_operand_rw(xedi_op);
      instr_op.is_explicit = true;
      instr_op.is_sticky = instr_op.IsRegister() && instr_op.reg.IsNative() &&
                           !instr_op.reg.IsGeneralPurpose();

      // Note: Things like label operands won't have a width.
      op_size = std::max(op_size, instr_op.width);
    }

    // TODO(pag): This is not right in all cases, e.g. PUSHFW, but then we'll
    //            likely detect it and solve it when it's an issue.
    if (XED_CATEGORY_PUSH == data.category ||
        XED_CATEGORY_POP == data.category) {
      op_size = arch::STACK_WIDTH_BITS;
    }
    data.effective_operand_width = op_size;
  }

  // Finalize the instruction by adding it to the basic block's instruction
  // list.
  void MakeInstruction(void) {
    Instruction *new_instr(nullptr);
    FixupOperands();

    // Ensure that instrumentation instructions do not alter the direction
    // flag! This is because we have no reliable way of saving and restoring
    // the direction flag (lest we use `PUSHF` and `POPF`) when the stack
    // pointer is not known to be valid.
    GRANARY_IF_DEBUG( auto flags = arch::IFORM_FLAGS[data.iform]; )
    GRANARY_ASSERT(!flags.written.s.df);

    if (data.IsJump()) {
      GRANARY_ASSERT(data.ops[0].is_annotation_instr);
      new_instr = new BranchInstruction(
          &data, DynamicCast<LabelInstruction *>(data.ops[0].annotation_instr));
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

  // Parse a single inline assembly instructions.
  void ParseInstruction(void) {
    ParseInstructionPrefixes();
    if (TryParseLabelInstruction()) return;
    ParseInstructionOpcode();
    ConsumeWhiteSpace();
    while (!Peek(';')) {
      if (data.num_explicit_ops) {
        Accept(',');
        ConsumeWhiteSpace();
      }
      ParseOperand();
      ++data.num_explicit_ops;
      ++op;
      ConsumeWhiteSpace();
    }
    Accept(';');
    MakeInstruction();
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

  void ParseWord(void) {
    auto b = &(buff[0]);
    for (; *ch; ) {
      if (PeekWhitespace() || Peek(';') || Peek(',') ||
          Peek(':') || Peek('[') || Peek(']')) {
        break;
      }
      *b++ = *ch++;
    }
    *b = '\0';
  }

  // Holds an in-progress instructions.
  arch::Instruction data;

  // The next operand to decode.
  arch::Operand *op;

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

  char buff[20];

  // The number of immediates already seen.
  int num_immediates;
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
