/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <iostream>

#include "dependencies/xed2-intel64/granary/instruction_info.h"

static const char *INDENT = "  ";

static void GenerateOperandCheckerPrologue(void) {
  std::cout << "bool IsAmbiguousOperand(xed_iclass_enum_t iclass,\n"
            << "                        xed_iform_enum_t iform,\n"
            << "                        unsigned op_num) {\n"
            << INDENT << "if (false) {\n";
}

static void GenerateOperandCheckerEpilogue(void) {
  std::cout << INDENT << "}\n"
            << INDENT << "return false;\n"
            << "}\n";
}

static void GenerateIclassCheck(const xed_inst_t *instr) {
  std::cout << INDENT << "} else if (XED_ICLASS_"
            << xed_iclass_enum_t2str(xed_inst_iclass(instr))
            << " == iclass) {\n";
}

static void GenerateIformCheck(const xed_inst_t *instr, bool has_ambiguous) {
  std::cout << INDENT << INDENT;
  if (has_ambiguous) {
    std::cout << "} else ";
  }
  std::cout << "if (XED_IFORM_"
            << xed_iform_enum_t2str(xed_inst_iform_enum(instr))
            << " == iform) {\n";
}

static void GenerateIclassCheckEpilogue(void) {
  std::cout << INDENT << INDENT << "}\n";
}

static void GenerateOperandCheck(unsigned num, bool has_ambiguous) {
  if (has_ambiguous) {
    std::cout << " || ";
  } else {
    std::cout << INDENT << INDENT << INDENT << "return ";
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
  auto has_ambiguous_iclass = false;
  GenerateIclassCheck(*(info.templates.begin()));
  for (auto instr : info.templates) {
    if (HasAmbiguousOperands(instr)) {
      GenerateIformCheck(instr, has_ambiguous_iclass);
      has_ambiguous_iclass = true;
      bool has_ambiguous_arg = false;
      for (auto arg_num = 0U; arg_num < 8; ++arg_num) {
        if (IsAmbiguousOperand(instr, arg_num)) {
          GenerateOperandCheck(arg_num, has_ambiguous_arg);
          has_ambiguous_arg = true;
        }
      }
      GenerateOperandCheckEpilogue(has_ambiguous_arg);
    }
  }
  GenerateIclassCheckEpilogue();
}

// Identify instructions with ambiguous encodings.
static void GenerateDisambiguators(void) {
  for (InstructionInfo &info : instr_table) {
    if (info.max_num_explicit_args != info.min_num_explicit_args) {
      GenerateDisambiguator(info);
    }
  }
}

// Special cases where we allow the ambiguities to go undetected.
ignored_iclass_set_t ingored_iclasses = {
  XED_ICLASS_RET_NEAR, XED_ICLASS_RET_FAR
};

int main(void) {
  InitIclassTable(&ingored_iclasses);

  GenerateOperandCheckerPrologue();
  GenerateDisambiguators();
  GenerateOperandCheckerEpilogue();
}
