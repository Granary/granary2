/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <iostream>
#include <cassert>

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

static void GenerateImplicitOperandBuilder(InstructionInfo *info,
                                           const xed_inst_t *instr,
                                           const xed_operand_t *op) {
  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  if (xed_operand_is_register(op_name)) {
      GenerateImplicitRegisterBuilder(xed_operand_reg(op), xed_operand_rw(op));

  } else if (XED_OPERAND_IMM0SIGNED == op_name) {
    std::cout << INDENT << "ImmediateBuilder(" << xed_operand_imm(op)
              << ", XED_ENCODER_OPERAND_TYPE_SIMM0).Build(instr);\n";

  } else if (XED_OPERAND_IMM0 == op_name) {
    std::cout << INDENT << "ImmediateBuilder(" << xed_operand_imm(op)
              << ", XED_ENCODER_OPERAND_TYPE_IMM0).Build(instr);\n";

  } else {
    assert(false);
  }
}

static void GenerateInstructionBuilder(InstructionInfo *info,
                                       const xed_inst_t *instr) {

  ops_bitset_t explicit_args;
  auto num_explicit_ops = ExplicitArgumentCount(instr, &explicit_args);
  auto max_num_explicit_ops = info->num_explicit_args;
  auto num_ops = xed_inst_noperands(instr);

  // Template typename list for arguments.
  if (num_explicit_ops) {
    std::cout << "template <";
    auto sep = "";
    for (auto i = 0; i < num_explicit_ops; ++i) {
      std::cout << sep << "typename A" << i;
      sep = ", ";
    }
    std::cout << ">\n";
  }

  // Function name, and beginning of arg list.
  auto iform = xed_inst_iform_enum(instr);
  std::cout << "inline static void "
            << xed_iform_enum_t2str(iform)
            << "(Instruction *instr";

  // Arg list.
  for (auto i = 0; i < num_explicit_ops; ++i) {
    std::cout << ", A" << i << " a" << i;
  }

  auto isel = instr - xed_inst_table_base();
  std::cout << ") {\n"
            << INDENT << "BuildInstruction(instr, XED_ICLASS_"
                      << xed_iclass_enum_t2str(xed_inst_iclass(instr))
                      << ", XED_IFORM_"
                      << xed_iform_enum_t2str(iform)
                      << ", " << isel << ", XED_CATEGORY_"
                      << xed_category_enum_t2str(xed_inst_category(instr))
                      << ");\n";

  auto explicit_op = 0U;
  for (auto i = 0U; i < num_ops; ++i) {
    auto op = xed_inst_operand(instr, i);
    if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op)) {
      GenerateExplicitOperandBuilder(info, instr, op, explicit_op++);
    } else if (is_ambiguous_arg[iform][i]) {
      GenerateImplicitOperandBuilder(info, instr, op);
    } else {
      break;
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
          XED_IFORM_BNDCN_BND_AGEN != iform &&  // Specially handled.
          XED_IFORM_BNDCU_BND_AGEN != iform &&  // Specially handled.
          XED_IFORM_BNDCL_BND_AGEN != iform &&  // Specially handled.
          XED_IFORM_BNDMK_BND_AGEN != iform &&  // Specially handled.
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
            << "namespace arch {\n"
            << "class Instruction;\n";
  GenerateInstructionBuilders();
  std::cout << "}  // namespace arch\n"
            << "}  // namespace granary\n"
            << "#endif  // DEPENDENCIES_XED2_INTEL64_INSTRUCTION_BUILDER_CC_\n";
}
