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
  int num_explicit_args;
  int max_num_args;
  bool has_ambigiuous_ops;
};

// Maps `xed_iclass_enum_t` to information about the instruction.
static InstructionInfo instr_table[XED_ICLASS_LAST];
static bool has_ambiguous_arg[XED_IFORM_LAST] = {false};
static bool is_ambiguous_arg[XED_IFORM_LAST][11] = {{false}};

// Populate the instruction table based on XED's internal tables.
static void FillTable(ignored_iclass_set_t *ignored_iclasses_set) {
  for (auto i = 0; i < XED_MAX_INST_TABLE_NODES; ++i) {
    auto instr = xed_inst_table_base() + i;
    auto iclass = xed_inst_iclass(instr);

    if (ignored_iclasses_set && ignored_iclasses_set->count(iclass)) {
      continue;
    }

    auto info = &(instr_table[iclass]);
    info->templates.insert(instr);
    info->num_explicit_args = 0;
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
      num_explicit_args++;
    }
  }
  return num_explicit_args;
}

static int MaxExplicitArgumentCount(const xed_inst_t *instr, ops_bitset_t *args) {
  int num_explicit_args = 0;
  auto num_ops = xed_inst_noperands(instr);
  for (auto i = 0U; i < num_ops; ++i) {
    auto op = xed_inst_operand(instr, i);
    if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op)) {
      num_explicit_args = i + 1;
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

      auto num = MaxExplicitArgumentCount(instr, &(info.ops));
      info.num_explicit_args = std::max(info.num_explicit_args, num);
    }
  }
}

// Output code to handle an instruction with a potentially ambiguous decoding.
static void FindAmbiguousOperands(InstructionInfo &info) {
  ops_bitset_t args;
  for (auto instr : info.templates) {
    ExplicitArgumentCount(instr, &args);
  }
  info.has_ambigiuous_ops = false;
  for (auto instr : info.templates) {
    auto iform = xed_inst_iform_enum(instr);
    auto num_ops = xed_inst_noperands(instr);
    auto last_is_implicit = false;
    for (auto i = 0U; i < num_ops; ++i) {
      auto op = xed_inst_operand(instr, i);
      if (XED_OPVIS_EXPLICIT != xed_operand_operand_visibility(op)) {
        last_is_implicit = true;
        if (args[i]) {
          info.has_ambigiuous_ops = true;
          has_ambiguous_arg[iform] = true;
          is_ambiguous_arg[iform][i] = true;
        }

      } else if (last_is_implicit) {
        last_is_implicit = false;
        info.has_ambigiuous_ops = true;
        has_ambiguous_arg[iform] = true;
        is_ambiguous_arg[iform][i - 1] = true;
      }
    }

    // Sweep through the operands and try to mark other implicit operands
    // as ambiguous. This catches things like `XED_FORM_IN_AL_DX`.
    //
    // TODO(pag): It doesn't catch `XED_FORM_IN_AL_DX` anymore :-/
    if (!has_ambiguous_arg[iform]) continue;
    for (auto i = num_ops; i-- > 1; ) {
      auto prev_op = xed_inst_operand(instr, i - 1);
      if (is_ambiguous_arg[iform][i] &&
          !is_ambiguous_arg[iform][i - 1] &&
          XED_OPVIS_EXPLICIT != xed_operand_operand_visibility(prev_op)) {
        is_ambiguous_arg[iform][i - 1] = true;
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
    FindAmbiguousOperands(info);
  }
}

static void InitIclassTable(ignored_iclass_set_t *ignored_iclasses_set) {
  FillTable(ignored_iclasses_set);
  CountOperands();
  FindAmbiguousEncodings();

  // Special cases.
  instr_table[XED_ICLASS_FSCALE].has_ambigiuous_ops = true;
  has_ambiguous_arg[XED_IFORM_FSCALE_ST0_ST1] = true;
  is_ambiguous_arg[XED_IFORM_FSCALE_ST0_ST1][0] = true;
  is_ambiguous_arg[XED_IFORM_FSCALE_ST0_ST1][1] = true;

  instr_table[XED_ICLASS_FSTP].has_ambigiuous_ops = true;
  instr_table[XED_ICLASS_FSTP].num_explicit_args = 2;
  instr_table[XED_ICLASS_FSTPNCE].has_ambigiuous_ops = true;
  instr_table[XED_ICLASS_FSTPNCE].num_explicit_args = 2;
  for (int iform = XED_IFORM_FSTP_MEMm64real_ST0;
       iform <= XED_IFORM_FSTPNCE_X87_ST0;
       iform++) {
    has_ambiguous_arg[iform] = true;
    is_ambiguous_arg[iform][1] = true;
  }

  is_ambiguous_arg[XED_IFORM_IMUL_GPRv_MEMv][2] = false;
  is_ambiguous_arg[XED_IFORM_IMUL_GPRv_GPRv][2] = false;

  // Far call/jmp.
  instr_table[XED_ICLASS_JMP_FAR].has_ambigiuous_ops = false;
  instr_table[XED_ICLASS_CALL_FAR].has_ambigiuous_ops = false;

  // For returns without constant-sized additions to the stack pointer.
  instr_table[XED_ICLASS_RET_NEAR].has_ambigiuous_ops = true;
  is_ambiguous_arg[XED_IFORM_RET_NEAR][0] = false;
  is_ambiguous_arg[XED_IFORM_RET_NEAR_IMMw][0] = true;
  has_ambiguous_arg[XED_IFORM_RET_NEAR_IMMw] = true;

  instr_table[XED_ICLASS_RET_FAR].has_ambigiuous_ops = true;
  is_ambiguous_arg[XED_IFORM_RET_FAR][0] = false;
  is_ambiguous_arg[XED_IFORM_RET_FAR_IMMw][0] = true;
  has_ambiguous_arg[XED_IFORM_RET_FAR_IMMw] = true;

  // Out.
  instr_table[XED_ICLASS_OUT].has_ambigiuous_ops = true;
  for (int iform = XED_IFORM_OUT_DX_AL; iform <= XED_IFORM_OUT_IMMb_OeAX;
       iform++) {
    has_ambiguous_arg[iform] = true;
    is_ambiguous_arg[iform][0] = true;
    is_ambiguous_arg[iform][1] = true;
  }

  // In.
  instr_table[XED_ICLASS_IN].has_ambigiuous_ops = true;
  for (int iform = XED_IFORM_IN_AL_DX; iform <= XED_IFORM_IN_OeAX_IMMb;
       iform++) {
    has_ambiguous_arg[iform] = true;
    is_ambiguous_arg[iform][0] = true;
    is_ambiguous_arg[iform][1] = true;
  }
}

#endif  // DEPENDENCIES_XED2_INTEL64_GRANARY_INSTRUCTION_INFO_H_
