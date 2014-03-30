/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef DEPENDENCIES_XED2_INTEL64_GRANARY_INSTRUCTION_INFO_H_
#define DEPENDENCIES_XED2_INTEL64_GRANARY_INSTRUCTION_INFO_H_

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <set>

extern "C" {
#include "dependencies/xed2-intel64/include/xed-interface.h"
}

typedef std::bitset<8> ops_bitset_t;
typedef std::set<xed_iclass_enum_t> ignored_iclass_set_t;

// Info about an instruction. We group together all xed_inst_t's for a given
// iclass. Based on the size of the instruction decode table and the number
// of iforms, it's not a 1-to-1 mapping of iform to xed_inst_t's, but with this
// setup we can get close enough to discover ambiguous encodings (defined in
// terms of the same operand being explicit in one iform but implicit in
// another) in an iclass-specific way.
struct InstructionInfo {
  std::set<const xed_inst_t *> templates;
  const xed_inst_t *xedi_with_max_ops;
  ops_bitset_t ops;
  int min_num_explicit_args;
  int max_num_explicit_args;
  int max_num_args;
};

// Maps `xed_iclass_enum_t` to information about the instruction.
static InstructionInfo instr_table[XED_ICLASS_LAST];
static bool has_ambiguous_arg[XED_IFORM_LAST] = {false};
static bool is_ambiguous_arg[XED_IFORM_LAST][8] = {{false}};

// Populate the instruction table based on XED's internal tables.
static void FillTable(ignored_iclass_set_t *ignored_iclasses_set) {
  for (auto i = 0; i < XED_MAX_INST_TABLE_NODES; ++i) {
    auto instr = xed_inst_table_base() + i;
    auto iclass = xed_inst_iclass(instr);

    if (ignored_iclasses_set && ignored_iclasses_set->count(iclass)) {
      continue;
    }

    auto info = instr_table + iclass;
    info->templates.insert(instr);
    info->min_num_explicit_args = 999;
    info->max_num_explicit_args = -1;
    info->max_num_args = -1;
  }
}

static int ExplicitArgumentCount(const xed_inst_t *instr, ops_bitset_t *args) {
  int num_explicit_args = 0;
  auto num_ops = xed_inst_noperands(instr);
  for (auto i = 0U; i < num_ops; ++i) {
    auto op = xed_inst_operand(instr, i);
    if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op)) {
      if (args) args->set(i, true);
      ++num_explicit_args;
    }
  }
  return num_explicit_args;
}

// Process each entry of the instruction table.
static void CountOperands(void) {
  for (InstructionInfo &info : instr_table) {
    for (auto instr : info.templates) {

      auto num_ops = static_cast<int>(xed_inst_noperands(instr));
      if (num_ops > info.max_num_args) {
        info.xedi_with_max_ops = instr;
        info.max_num_args = num_ops;
      }

      auto num = ExplicitArgumentCount(instr, &(info.ops));
      info.max_num_explicit_args = std::max(info.max_num_explicit_args, num);
      info.min_num_explicit_args = std::min(info.min_num_explicit_args, num);
    }
  }
}

// Output code to handle an instruction with a potentially ambiguous decoding.
static void FindAmbiguousOperands(InstructionInfo &info) {
  for (auto instr : info.templates) {
    ops_bitset_t args;
    auto num = ExplicitArgumentCount(instr, &args);
    if (args == info.ops) {
      continue;
    }
    auto iform = xed_inst_iform_enum(instr);
    auto ambiguous_args = args ^ info.ops;
    for (auto arg_num = 0; arg_num < 8; ++arg_num) {
      if (ambiguous_args[arg_num] && info.ops[arg_num]) {
        has_ambiguous_arg[iform] = true;
        is_ambiguous_arg[iform][arg_num] = true;
      }
    }
  }
}

static bool HasAmbiguousOperands(const xed_inst_t *instr) {
  return has_ambiguous_arg[xed_inst_iform_enum(instr)];
}

static bool IsAmbiguousOperand(const xed_inst_t *instr, unsigned op_num) {
  return is_ambiguous_arg[xed_inst_iform_enum(instr)][op_num];
}

// Identify instructions with ambiguous encodings.
static void FindAmbiguousEncodings(void) {
  for (InstructionInfo &info : instr_table) {
    if (info.max_num_explicit_args != info.min_num_explicit_args) {
      FindAmbiguousOperands(info);
    }
  }
}

static void InitIclassTable(ignored_iclass_set_t *ignored_iclasses_set) {
  FillTable(ignored_iclasses_set);
  CountOperands();
  FindAmbiguousEncodings();
}

#endif  // DEPENDENCIES_XED2_INTEL64_GRANARY_INSTRUCTION_INFO_H_
