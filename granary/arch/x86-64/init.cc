/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/arch/x86-64/xed.h"
#include "granary/arch/x86-64/operand.h"
#include "granary/arch/x86-64/builder.h"

#include "granary/breakpoint.h"
#include "granary/memory.h"

namespace granary {
namespace arch {

// Decoder state that sets the mode to 64-bit.
xed_state_t XED_STATE;

// Table of all implicit operands.
const Operand *IMPLICIT_OPERANDS[XED_ICLASS_LAST] = {nullptr};

// Number of implicit operands for each iclass.
int NUM_IMPLICIT_OPERANDS[XED_ICLASS_LAST] = {0};

// Categories of every iclass.
xed_category_enum_t ICLASS_CATEGORIES[XED_ICLASS_LAST] = {XED_CATEGORY_INVALID};

// Table to find the instruction selections for each iclass.
const xed_inst_t *ICLASS_SELECTIONS[XED_ICLASS_LAST] = {nullptr};

namespace {

// Number of pages allocates to hold the table of implicit operands.
static int num_implicit_operand_pages = 0;

// Initialize the table of iclass categories.
static void InitIclassTables(void) {
  for (auto sel = 0; sel < XED_MAX_INST_TABLE_NODES; ++sel) {
    auto instr = xed_inst_table_base() + sel;
    auto iclass = xed_inst_iclass(instr);
    if (!ICLASS_SELECTIONS[iclass]) {
      auto category = xed_inst_category(instr);
      ICLASS_SELECTIONS[iclass] = instr;
      ICLASS_CATEGORIES[iclass] = category;
    }
  }
}

// Invoke a function one every implicit operand of each iclass.
static void ForEachImplicitOperand(
    const std::function<void(const xed_inst_t *, const xed_operand_t *,
                        unsigned)> &cb) {
  auto last_iclass = XED_ICLASS_INVALID;
  for (auto sel = 0; sel < XED_MAX_INST_TABLE_NODES; ++sel) {
    auto instr = xed_inst_table_base() + sel;
    auto iclass = xed_inst_iclass(instr);
    auto iform = xed_inst_iform_enum(instr);
    if (iclass == last_iclass) {
      continue;
    }
    last_iclass = iclass;
    auto num_ops = xed_inst_noperands(instr);
    for (auto i = 0U; i < num_ops; ++i) {
      auto op = xed_inst_operand(instr, i);
      if (XED_OPVIS_EXPLICIT != xed_operand_operand_visibility(op) &&
          !IsAmbiguousOperand(iclass, iform, i)) {
        cb(instr, op, i);
      }
    }
  }
}

// Returns the total number of implicit operands.
static size_t CountImplicitOperands(void) {
  size_t num_implicit_ops(0);
  auto func = [&] (const xed_inst_t *instr, const xed_operand_t *, unsigned) {
    ++NUM_IMPLICIT_OPERANDS[xed_inst_iclass(instr)];
    ++num_implicit_ops;
  };
  ForEachImplicitOperand(std::cref(func));
  return num_implicit_ops;
}

// Allocate the implicit operands.
static Operand *AllocateImplicitOperands(void) {
  auto num_implicit_ops = CountImplicitOperands();
  auto ops_mem_size = num_implicit_ops * sizeof(Operand);
  auto aligned_ops_mem_size = GRANARY_ALIGN_TO(ops_mem_size,
                                               arch::PAGE_SIZE_BYTES);
  num_implicit_operand_pages = static_cast<int>(
      aligned_ops_mem_size / arch::PAGE_SIZE_BYTES);
  auto ops_mem_raw = AllocatePages(num_implicit_operand_pages);
  return reinterpret_cast<Operand *>(ops_mem_raw);
}

// Fill in an operand as if it's a register operand.
static void FillRegisterOperand(Operand *instr_op, xed_reg_enum_t reg) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->reg.DecodeFromNative(reg);
  instr_op->width = static_cast<int8_t>(instr_op->reg.BitWidth());
  instr_op->is_sticky = true;
}

// Address operands are usually used either directly as register operands (REG0
// or REG1), or as the register component of a separate memory operand (BASE0,
// BASE1).
static void FillAddressOperand(Operand *instr_op, xed_reg_enum_t reg) {
  FillRegisterOperand(instr_op, reg);
}

// Convert a non-terminal operand into a Granary operand. This will sometimes
// cheat by converting non-terminal operands into a close-enough representation
// that benefits other parts of Granary (e.g. the virtual register system). Not
// all non-terminal operands have a decoding that Granary cares about.
static bool ConvertNonTerminalOperand(Operand *instr_op,
                                      const xed_operand_t *op) {
  switch (xed_operand_nonterminal_name(op)) {
    case XED_NONTERMINAL_AR10:
      FillAddressOperand(instr_op, XED_REG_R10); return true;
    case XED_NONTERMINAL_AR11:
      FillAddressOperand(instr_op, XED_REG_R11); return true;
    case XED_NONTERMINAL_AR12:
      FillAddressOperand(instr_op, XED_REG_R12); return true;
    case XED_NONTERMINAL_AR13:
      FillAddressOperand(instr_op, XED_REG_R13); return true;
    case XED_NONTERMINAL_AR14:
      FillAddressOperand(instr_op, XED_REG_R14); return true;
    case XED_NONTERMINAL_AR15:
      FillAddressOperand(instr_op, XED_REG_R15); return true;
    case XED_NONTERMINAL_AR8:
      FillAddressOperand(instr_op, XED_REG_R8); return true;
    case XED_NONTERMINAL_AR9:
      FillAddressOperand(instr_op, XED_REG_R9); return true;
    case XED_NONTERMINAL_ARAX:
      FillAddressOperand(instr_op, XED_REG_RAX); return true;
    case XED_NONTERMINAL_ARBP:
      FillAddressOperand(instr_op, XED_REG_RBP); return true;
    case XED_NONTERMINAL_ARBX:
      FillAddressOperand(instr_op, XED_REG_RBX); return true;
    case XED_NONTERMINAL_ARCX:
      FillAddressOperand(instr_op, XED_REG_RCX); return true;
    case XED_NONTERMINAL_ARDI:
      FillAddressOperand(instr_op, XED_REG_RDI); return true;
    case XED_NONTERMINAL_ARDX:
      FillAddressOperand(instr_op, XED_REG_RDX); return true;
    case XED_NONTERMINAL_ARSI:
      FillAddressOperand(instr_op, XED_REG_RSI); return true;
    case XED_NONTERMINAL_ARSP:  // Address with RSP.
      FillAddressOperand(instr_op, XED_REG_RSP); return true;
    case XED_NONTERMINAL_OEAX:
      FillRegisterOperand(instr_op, XED_REG_EAX); return true;
    case XED_NONTERMINAL_ORAX:
      FillRegisterOperand(instr_op, XED_REG_RAX); return true;
    case XED_NONTERMINAL_ORBP:
      FillRegisterOperand(instr_op, XED_REG_RBP); return true;
    case XED_NONTERMINAL_ORDX:
      FillRegisterOperand(instr_op, XED_REG_RDX); return true;
    case XED_NONTERMINAL_ORSP:  // Output to RSP.
      FillRegisterOperand(instr_op, XED_REG_RSP); return true;
    case XED_NONTERMINAL_RIP:
      FillRegisterOperand(instr_op, XED_REG_RIP); return true;
    case XED_NONTERMINAL_SRBP:
      FillRegisterOperand(instr_op, XED_REG_RBP); return true;
    case XED_NONTERMINAL_SRSP:  // Shift RSP?
      FillRegisterOperand(instr_op, XED_REG_RSP); return true;
    case XED_NONTERMINAL_RFLAGS:
      FillRegisterOperand(instr_op, XED_REG_RFLAGS); return true;
    default:
      GRANARY_ASSERT(false);
      return false;
  }
}

// Initializes an implicit operand.
static void InitImplicitOperand(const xed_inst_t *instr,
                                const xed_operand_t *op, Operand *instr_op,
                                unsigned i) {
  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  memset(instr_op, 0, sizeof *instr_op);
  if (XED_OPERAND_TYPE_NT_LOOKUP_FN == op_type) {
    ConvertNonTerminalOperand(instr_op, op);
  } else if (xed_operand_is_register(op_name)) {
    FillRegisterOperand(instr_op, xed_operand_reg(op));
  } else if (XED_OPERAND_MEM0 == op_name || XED_OPERAND_MEM1 == op_name) {
    auto base01_op = xed_inst_operand(instr, i + 1);
    ConvertNonTerminalOperand(instr_op, base01_op);
    instr_op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  } else {
    GRANARY_ASSERT(false);
  }
  instr_op->is_sticky = true;
  instr_op->rw = xed_operand_rw(op);
}

// Initializes the implicit operands in the table.
static void InitImplicitOperands(Operand *op) {
  auto func = [&] (const xed_inst_t *instr, const xed_operand_t *instr_op,
                   unsigned i) {
    InitImplicitOperand(instr, instr_op, op, i);

    // Initialize the first implicit operand for this iclass and move to
    // initialize the next operand.
    auto iclass = xed_inst_iclass(instr);
    if (!IMPLICIT_OPERANDS[iclass]) {
      IMPLICIT_OPERANDS[iclass] = op;
    }
    op++;
  };
  ForEachImplicitOperand(std::cref(func));
}

// Initialize a table of implicit operands.
//
// TODO(pag): These tables could likely be compressed by quite a bit.
static void InitOperandTables(void) {
  if (!IMPLICIT_OPERANDS[0]) {
    auto ops = AllocateImplicitOperands();
    InitImplicitOperands(ops);
    ProtectPages(ops, num_implicit_operand_pages, MemoryProtection::READ_ONLY);
  }
}

}  // namespace

// Initialize the driver (instruction encoder/decoder).
void Init(void) {
  xed_tables_init();
  xed_state_zero(&XED_STATE);
  xed_state_init(&XED_STATE, XED_MACHINE_MODE_LONG_64,
                 XED_ADDRESS_WIDTH_64b, XED_ADDRESS_WIDTH_64b);
  InitIclassTables();
  InitOperandTables();
}

}  // namespace arch
}  // namespace granary