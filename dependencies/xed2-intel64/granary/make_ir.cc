/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>

#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

struct InstructionInfo {
  std::set<const xed_inst_t *> templates;
  xed_iclass_enum_t iclass;
  int min_num_explicit_args;
  int max_num_explicit_args;
};


// Maps `xed_iclass_enum_t` to information about the instruction.
InstructionInfo instr_table[XED_ICLASS_LAST];

// Populate the instruction table based on XED's internal tables.
static void fill_table(void) {
  for (int i(0); i < XED_MAX_INST_TABLE_NODES; ++i) {
    auto instr = xed_inst_table_base() + i;
    auto iclass = xed_inst_iclass(instr);
    auto info = instr_table + iclass;
    info->templates.insert(instr);
    info->iclass = iclass;
    info->min_num_explicit_args = 999;
    info->max_num_explicit_args = -1;
  }
}

// Process each entry of the instruction table.
static void process_table(void) {
  for (InstructionInfo &info : instr_table) {
    for (auto instr : info.templates) {
      for(unsigned i(0); i < xed_inst_noperands(instr); ++i) {
        auto op = xed_inst_operand(instr, i);
      }
    }
  }
}

}  // namespace driver
}  // namespace granary

int main(void) {
  using namespace granary::driver;
  fill_table();
}
