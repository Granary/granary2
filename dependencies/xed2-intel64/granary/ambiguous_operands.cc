/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <iostream>
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <bitset>

#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

typedef std::bitset<8> ops_bitset_t;

struct InstructionInfo {
  std::set<const xed_inst_t *> templates;
  ops_bitset_t ops;
  int min_num_explicit_args;
  int max_num_explicit_args;
};


// Maps `xed_iclass_enum_t` to information about the instruction.
InstructionInfo instr_table[XED_ICLASS_LAST];

// Populate the instruction table based on XED's internal tables.
static void FillTable(void) {
  for (auto i = 0; i < XED_MAX_INST_TABLE_NODES; ++i) {
    auto instr = xed_inst_table_base() + i;
    auto iclass = xed_inst_iclass(instr);
    auto info = instr_table + iclass;
    info->templates.insert(instr);
    info->min_num_explicit_args = 999;
    info->max_num_explicit_args = -1;
  }
}

static int ExplicitArgumentCount(const xed_inst_t *instr, ops_bitset_t &args) {
  int num_explicit_args = 0;
  for (unsigned i(0); i < xed_inst_noperands(instr); ++i) {
    auto op = xed_inst_operand(instr, i);
    if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op)) {
      args.set(i, true);
      ++num_explicit_args;
    }
  }
  return num_explicit_args;
}

// Process each entry of the instruction table.
static void CountExplicitArguments(void) {
  for (InstructionInfo &info : instr_table) {
    for (auto instr : info.templates) {
      auto num = ExplicitArgumentCount(instr, info.ops);
      info.max_num_explicit_args = std::max(info.max_num_explicit_args, num);
      info.min_num_explicit_args = std::min(info.min_num_explicit_args, num);
    }
  }
}

static const char *INDENT = "  ";

static void GenerateOperandCheckerPrologue(void) {
  std::cout << "bool IsAmbiguousOperand(xed_iform_enum_t iform, " \
            << "unsigned op_num) {\n"
            << INDENT << "if (false) {\n";
}

static void GenerateOperandCheckerEpilogue(void) {
  std::cout << INDENT << "}\n"
            << INDENT << "return false;\n"
            << "}\n";
}

static void GenerateInstructionCheck(const xed_inst_t *instr) {
  std::cout << INDENT << "} else if (XED_IFORM_"
            << xed_iform_enum_t2str(xed_inst_iform_enum(instr))
            << " == iform) {\n";
}

static void GenerateOperandCheck(int num, bool has_ambiguous) {
  if (has_ambiguous) {
    std::cout << " || ";
  } else {
    std::cout << INDENT << INDENT << "return ";
  }
  std::cout << num << " == op_num";
}

static void GenerateOperandCheckEpilogue(bool has_ambiguous) {
  if (has_ambiguous) {
    std::cout << ";\n";
  }
}

// Output code to handle an instruction with a potentially ambiguous decoding.
static void GenerateDisambiguator(InstructionInfo &info) {
  for (auto instr : info.templates) {
    ops_bitset_t args;
    auto num = ExplicitArgumentCount(instr, args);
    if (args == info.ops) {
      continue;
    }

    GenerateInstructionCheck(instr);
    auto ambiguous_args = args ^ info.ops;
    auto arg_num = 0;
    bool has_ambiguous = false;
    for (auto arg_num = 0; arg_num < 8; ++arg_num) {
      if (ambiguous_args[arg_num] && info.ops[arg_num]) {
        GenerateOperandCheck(arg_num, has_ambiguous);
        has_ambiguous = true;
      }
    }
    GenerateOperandCheckEpilogue(has_ambiguous);
  }
}

// Identify instructions with ambiguous encodings.
static void FindAmbiguousEncodings(void) {
  for (InstructionInfo &info : instr_table) {
    if (info.max_num_explicit_args != info.min_num_explicit_args) {
      GenerateDisambiguator(info);
    }
  }
}

}  // namespace driver
}  // namespace granary

int main(void) {
  using namespace granary::driver;
  FillTable();
  CountExplicitArguments();
  GenerateOperandCheckerPrologue();
  FindAmbiguousEncodings();
  GenerateOperandCheckerEpilogue();
}
