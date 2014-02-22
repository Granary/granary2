/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <iostream>

#include "dependencies/xed2-intel64/granary/instruction_info.h"

const char *INDENT = "  ";

static void GenerateExplicitOperandBuilder(InstructionInfo *info,
                                           const xed_inst_t *instr,
                                           const xed_operand_t *op,
                                           unsigned op_num,
                                           unsigned arg_num) {
  std::cout << INDENT << "ImportOperand(instr, &(instr->ops["
            << op_num << "]), XED_OPERAND_ACTION_"
            << xed_operand_action_enum_t2str(xed_operand_rw(op))
            << ", a" << arg_num;

  auto op_name = xed_operand_name(op);
  if (XED_OPERAND_IMM0 == op_name) {
    std::cout << ", XED_ENCODER_OPERAND_TYPE_IMM0";
  } else if (XED_OPERAND_IMM0SIGNED == op_name) {
    std::cout << ", XED_ENCODER_OPERAND_TYPE_SIMM0";
  } else if (XED_OPERAND_IMM1 == op_name || XED_OPERAND_IMM1_BYTES == op_name) {
    std::cout << ", XED_ENCODER_OPERAND_TYPE_IMM1";
  }

  std::cout << ");\n";
}

static void GenerateImplicitOperandBuilder(InstructionInfo *info,
                                           const xed_inst_t *instr,
                                           const xed_operand_t *op,
                                           unsigned op_num) {
  std::cout << INDENT << "// TODO!!\n";
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
            << INDENT << "ImportInstruction(instr, XED_ICLASS_"
                      << xed_iclass_enum_t2str(xed_inst_iclass(instr))
                      << ", XED_CATEGORY_"
                      << xed_category_enum_t2str(xed_inst_category(instr))
                      << ", " << max_num_explicit_ops << ");\n";

  for (auto i = 0U, arg_offset = 0U; i < num_ops; ++i) {
    auto op = xed_inst_operand(instr, i);
    if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op)) {
      GenerateExplicitOperandBuilder(info, instr, op, i, arg_offset++);
    } else if (IsAmbiguousOperand(instr, i)) {
      GenerateImplicitOperandBuilder(info, instr, op, i);
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
  XED_CATEGORY_X87_ALU
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
            << "class Instruction;\n"
            << "void ImportInstruction(Instruction *instr, "
            << "xed_iclass_enum_t iclass,\n"
            << "                       xed_category_enum_t category, "
            << "int8_t num_ops);\n";
  GenerateInstructionBuilders();
  std::cout << "}  // namespace driver\n"
            << "}  // namespace granary\n"
            << "#endif  // DEPENDENCIES_XED2_INTEL64_INSTRUCTION_BUILDER_CC_\n";
}
