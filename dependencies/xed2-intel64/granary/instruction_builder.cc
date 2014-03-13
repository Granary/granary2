/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <iostream>

#include "dependencies/xed2-intel64/granary/instruction_info.h"

const char *INDENT = "  ";

static void GenerateExplicitOperandBuilder(InstructionInfo *info,
                                           const xed_inst_t *instr,
                                           const xed_operand_t *op,
                                           unsigned arg_num) {
  auto op_name = xed_operand_name(op);
  auto action = xed_operand_rw(op);
  auto action_str = xed_operand_action_enum_t2str(action);

  if (xed_operand_is_register(op_name)) {
    std::cout << INDENT << "RegisterBuilder(a" << arg_num
              << ", XED_OPERAND_ACTION_" << action_str
              << ").Build(instr);\n";
  } else if (XED_OPERAND_MEM0 == op_name || XED_OPERAND_MEM1 == op_name) {
    std::cout << INDENT << "MemoryBuilder(a" << arg_num
              << ", XED_OPERAND_ACTION_" << action_str << ").Build(instr);\n";

  } else if (XED_OPERAND_IMM0SIGNED == op_name) {
    std::cout << INDENT << "ImmediateBuilder(a" << arg_num
              << ", XED_ENCODER_OPERAND_TYPE_SIMM0).Build(instr);\n";

  } else if (XED_OPERAND_IMM0 == op_name) {
    std::cout << INDENT << "ImmediateBuilder(a" << arg_num
              << ", XED_ENCODER_OPERAND_TYPE_IMM0).Build(instr);\n";

  } else if (XED_OPERAND_IMM1 == op_name || XED_OPERAND_IMM1_BYTES == op_name) {
    std::cout << INDENT << "ImmediateBuilder(a" << arg_num
              << ", XED_ENCODER_OPERAND_TYPE_IMM1).Build(instr);\n";

  } else if (XED_OPERAND_RELBR == op_name) {
    std::cout << INDENT << "BranchTargetBuilder(a" << arg_num
              << ").Build(instr);\n";
  }
}

static void GenerateImplicitRegisterBuilder(xed_reg_enum_t reg,
                                            xed_operand_action_enum_t action) {
  std::cout << INDENT << "RegisterBuilder(XED_REG_"
                << xed_reg_enum_t2str(reg) << ", XED_OPERAND_ACTION_"
                << xed_operand_action_enum_t2str(action)
                << ").Build(instr);\n";
}

// Convert a non-terminal operand into a register operand. This will sometimes
// cheat by converting non-terminal operands into a close-enough representation
// that benefits other parts of Granary (e.g. the virtual register system). Not
// all non-terminal operands have a decoding that Granary cares about.
static void ConvertNonTerminalOperand(const xed_operand_t *op) {
  switch (xed_operand_nonterminal_name(op)) {
    case XED_NONTERMINAL_AR10:
      GenerateImplicitRegisterBuilder(XED_REG_R10, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR11:
      GenerateImplicitRegisterBuilder(XED_REG_R11, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR12:
      GenerateImplicitRegisterBuilder(XED_REG_R12, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR13:
      GenerateImplicitRegisterBuilder(XED_REG_R13, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR14:
      GenerateImplicitRegisterBuilder(XED_REG_R14, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR15:
      GenerateImplicitRegisterBuilder(XED_REG_R15, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR8:
      GenerateImplicitRegisterBuilder(XED_REG_R8, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_AR9:
      GenerateImplicitRegisterBuilder(XED_REG_R9, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARAX:
      GenerateImplicitRegisterBuilder(XED_REG_RAX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARBP:
      GenerateImplicitRegisterBuilder(XED_REG_RBP, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARBX:
      GenerateImplicitRegisterBuilder(XED_REG_RBX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARCX:
      GenerateImplicitRegisterBuilder(XED_REG_RCX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARDI:
      GenerateImplicitRegisterBuilder(XED_REG_RDI, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARDX:
      GenerateImplicitRegisterBuilder(XED_REG_RDX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARSI:
      GenerateImplicitRegisterBuilder(XED_REG_RSI, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ARSP:
      GenerateImplicitRegisterBuilder(XED_REG_RSP, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_OEAX:
      GenerateImplicitRegisterBuilder(XED_REG_EAX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ORAX:
      GenerateImplicitRegisterBuilder(XED_REG_RAX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ORBP:
      GenerateImplicitRegisterBuilder(XED_REG_RBP, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ORDX:
      GenerateImplicitRegisterBuilder(XED_REG_RDX, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_ORSP:
      GenerateImplicitRegisterBuilder(XED_REG_RSP, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_RIP:
      GenerateImplicitRegisterBuilder(XED_REG_RIP, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_SRBP:
      GenerateImplicitRegisterBuilder(XED_REG_RBP, xed_operand_rw(op)); break;
    case XED_NONTERMINAL_SRSP:
      GenerateImplicitRegisterBuilder(XED_REG_RSP, xed_operand_rw(op)); break;
    default: break;
  }
}

static void GenerateImplicitOperandBuilder(InstructionInfo *info,
                                           const xed_inst_t *instr,
                                           const xed_operand_t *op) {
  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  if (XED_OPERAND_TYPE_NT_LOOKUP_FN == op_type) {
    ConvertNonTerminalOperand(op);
  } else if (xed_operand_is_register(op_name)) {
      GenerateImplicitRegisterBuilder(xed_operand_reg(op), xed_operand_rw(op));

  } else if (XED_OPERAND_IMM0SIGNED == op_name) {
    std::cout << INDENT << "ImmediateBuilder(" << xed_operand_imm(op)
              << ", XED_ENCODER_OPERAND_TYPE_SIMM0).Build(instr);\n";

  } else if (XED_OPERAND_IMM0 == op_name) {
    std::cout << INDENT << "ImmediateBuilder(" << xed_operand_imm(op)
              << ", XED_ENCODER_OPERAND_TYPE_IMM0).Build(instr);\n";

  }
}

static void GenerateInstructionBuilder(InstructionInfo *info,
                                       const xed_inst_t *instr) {

  ops_bitset_t explicit_args;
  auto num_explicit_ops = ExplicitArgumentCount(instr, &explicit_args);
  auto max_num_explicit_ops = info->max_num_explicit_args;
  auto num_ops = xed_inst_noperands(instr);

  // Template typename list for arguments.
  if (num_explicit_ops) {
    std::cout << "template <";
    for (auto i = 0; i < num_explicit_ops; ++i) {
      if (0 < i) {
        std::cout << ", ";
      }
      std::cout << "typename A" << i;
    }
    std::cout << ">\n";
  }

  // Function name, and beginning of arg list.
  std::cout << "inline static void "
            << xed_iform_enum_t2str(xed_inst_iform_enum(instr))
            << "(Instruction *instr";

  // Arg list.
  for (auto i = 0; i < num_explicit_ops; ++i) {
    std::cout << ", A" << i << " a" << i;
  }

  std::cout << ") {\n"
            << INDENT << "BuildInstruction(instr, XED_ICLASS_"
                      << xed_iclass_enum_t2str(xed_inst_iclass(instr))
                      << ", XED_CATEGORY_"
                      << xed_category_enum_t2str(xed_inst_category(instr))
                      << ", " << num_explicit_ops << ");\n";

  for (auto i = 0U; i < num_ops; ++i) {
    auto op = xed_inst_operand(instr, i);
    if (i < num_explicit_ops) {
      GenerateExplicitOperandBuilder(info, instr, op, i);
    } else {
      GenerateImplicitOperandBuilder(info, instr, op);
    }
  }
  std::cout << "}\n";
}

static std::set<xed_category_enum_t> ignore_categories = {
  XED_CATEGORY_3DNOW,
  XED_CATEGORY_AES,
  XED_CATEGORY_AVX,
  XED_CATEGORY_AVX2,
  XED_CATEGORY_AVX2GATHER,
  XED_CATEGORY_BDW,
  XED_CATEGORY_CONVERT,
  XED_CATEGORY_DECIMAL,
  XED_CATEGORY_FMA4,
  XED_CATEGORY_LOGICAL_FP,
  XED_CATEGORY_MMX,
  XED_CATEGORY_PREFETCH,
  XED_CATEGORY_PCLMULQDQ,
  XED_CATEGORY_SSE,
  XED_CATEGORY_VFMA,
  XED_CATEGORY_VTX,
  XED_CATEGORY_WIDENOP,
  XED_CATEGORY_X87_ALU,
  XED_CATEGORY_STRINGOP  // Don't want complex base/disp mem ops.
};

static bool already_generated[XED_IFORM_LAST] = {false};

static bool HasIgnorableOperand(const xed_inst_t *instr) {
  auto num_ops = xed_inst_noperands(instr);
  for (auto i = 0U; i < num_ops; ++i) {
    auto op = xed_inst_operand(instr, i);
    if (XED_OPERAND_TYPE_NT_LOOKUP_FN == xed_operand_type(op)) {
      auto reg = xed_operand_nonterminal_name(op);
      if (XED_NONTERMINAL_XMM_B <= reg && reg <= XED_NONTERMINAL_XMM_SE64) {
        return true;
      }
    }
  }
  return false;
}

static void GenerateInstructionBuilders(void) {
  for (InstructionInfo &info : instr_table) {
    for (auto instr : info.templates) {
      auto iclass = xed_inst_iclass(instr);
      auto iform = xed_inst_iform_enum(instr);
      auto icategory = xed_inst_category(instr);
      if (XED_ICLASS_INVALID != iclass &&
          XED_ICLASS_LEA != iclass &&  // Specially handled.
          XED_ICLASS_CALL_FAR != iclass &&  // Not handled.
          XED_ICLASS_JMP_FAR != iclass &&  // Not handled.
          !ignore_categories.count(icategory) &&
          !already_generated[iform] &&
          !HasIgnorableOperand(instr)) {
        already_generated[iform] = true;
        GenerateInstructionBuilder(&info, instr);
      }
    }
  }
}

int main(void) {
  InitIclassTable(nullptr);
  std::cout << "#ifndef DEPENDENCIES_XED2_INTEL64_INSTRUCTION_BUILDER_CC_\n"
            << "#define DEPENDENCIES_XED2_INTEL64_INSTRUCTION_BUILDER_CC_\n"
            << "namespace granary {\n"
            << "namespace driver {\n"
            << "class Instruction;\n";
  GenerateInstructionBuilders();
  std::cout << "}  // namespace driver\n"
            << "}  // namespace granary\n"
            << "#endif  // DEPENDENCIES_XED2_INTEL64_INSTRUCTION_BUILDER_CC_\n";
}
