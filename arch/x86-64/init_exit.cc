/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "arch/x86-64/xed.h"
#include "arch/x86-64/operand.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/register.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

namespace granary {
namespace arch {

// Decoder state that sets the mode to 64-bit.
xed_state_t XED_STATE;

// Table of all implicit operands for each `isel`.
const Operand *IMPLICIT_OPERANDS[XED_MAX_INST_TABLE_NODES] = {nullptr};

// Number of implicit operands for each `isel`.
uint8_t NUM_IMPLICIT_OPERANDS[XED_MAX_INST_TABLE_NODES] = {0};

// Categories of every iclass.
xed_category_enum_t ICLASS_CATEGORIES[XED_ICLASS_LAST] = {XED_CATEGORY_INVALID};

// Table to find the instruction selections for each iclass.
const xed_inst_t *ICLASS_SELECTIONS[XED_ICLASS_LAST] = {nullptr};
const xed_inst_t *LAST_ICLASS_SELECTION = nullptr;

// Table mapping each iclass/iform to the set of read and written flags by *any*
// selection of that iclass/iform.
FlagActions ICLASS_FLAG_ACTIONS[XED_ICLASS_LAST];
FlagsSet IFORM_FLAGS[XED_IFORM_LAST];

// Returns a bitmap representing all arithmetic flags being live.
extern uint32_t AllArithmeticFlags(void);

// Initialize the block tracer.
extern void InitBlockTracer(void);

namespace {

// Number of pages allocates to hold the table of implicit operands.
static size_t gNumImplicitOperandPages = 0;
static void *gImplicitOperandPages = nullptr;

// Initialize the table of iclass categories.
static void InitIclassTables(void) {
  LAST_ICLASS_SELECTION = xed_inst_table_base() + XED_MAX_INST_TABLE_NODES;
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

// Updates the flag actions for an iclass based on a xedi.
static void UpdateFlagActions(const xed_inst_t *xedi,
                              xed_iclass_enum_t iclass) {
  if (auto num_ops = xed_inst_noperands(xedi)) {
    auto last_op = xed_inst_operand(xedi, num_ops - 1);
    auto last_op_type = xed_operand_type(last_op);
    auto nt_name = xed_operand_nonterminal_name(last_op);

    // Make sure that conditional writes of the flags are treated as reads.
    if (XED_OPERAND_TYPE_NT_LOOKUP_FN == last_op_type &&
        XED_NONTERMINAL_RFLAGS == nt_name) {
      auto &actions(ICLASS_FLAG_ACTIONS[iclass]);
      switch (xed_operand_rw(last_op)) {
        case XED_OPERAND_ACTION_RW:
        case XED_OPERAND_ACTION_RCW:

        // Treated as a read so flags from below propagate through.
        case XED_OPERAND_ACTION_CW:
        case XED_OPERAND_ACTION_CRW:
          actions.is_read = true;
          actions.is_write = true;
          break;

        case XED_OPERAND_ACTION_R:
        case XED_OPERAND_ACTION_CR:
          actions.is_read = true;
          break;

        case XED_OPERAND_ACTION_W:
          actions.is_write = true;
          break;

        default: break;
      }
      switch (xed_operand_rw(last_op)) {
        case XED_OPERAND_ACTION_RCW:
        case XED_OPERAND_ACTION_CW:
          actions.is_conditional_write = true;
          break;
        default: break;
      }
    }
  }
}

// Initialize the table of iclass flags.
static void InitIclassFlags(void) {
  memset(&(ICLASS_FLAG_ACTIONS[0]), 0, sizeof ICLASS_FLAG_ACTIONS);

  for (auto sel = 0; sel < XED_MAX_INST_TABLE_NODES; ++sel) {
    auto xedi = xed_inst_table_base() + sel;
    auto iclass = xed_inst_iclass(xedi);

    UpdateFlagActions(xedi, iclass);
  }

  // Special case `INTn` instructions. The reason why we do this is that these
  // end up being a bit screwy with our assembly-time flags analysis. That is,
  // we find that these read/write to flags, but that we can't necessarily save
  // or restore all those flags. Also, we assume that the saving/restoring is
  // beyond our control anyway (i.e. handled by OS or debugger).
  ICLASS_FLAG_ACTIONS[XED_ICLASS_INT].is_write = false;
  ICLASS_FLAG_ACTIONS[XED_ICLASS_INT].is_conditional_write = false;

  ICLASS_FLAG_ACTIONS[XED_ICLASS_INTO].is_write = false;
  ICLASS_FLAG_ACTIONS[XED_ICLASS_INTO].is_conditional_write = false;

  ICLASS_FLAG_ACTIONS[XED_ICLASS_INT3].is_write = false;
  ICLASS_FLAG_ACTIONS[XED_ICLASS_INT3].is_conditional_write = false;
}

// Initialize the table of `iform` flags.
static void InitIformFlags(void) {
  memset(&(IFORM_FLAGS[0]), 0, sizeof IFORM_FLAGS);

  xed_decoded_inst_t xedd;
  memset(&xedd, 0, sizeof xedd);
  const auto all_flags = AllArithmeticFlags();

  for (auto sel = 0; sel < XED_MAX_INST_TABLE_NODES; ++sel) {
    auto xedi = xed_inst_table_base() + sel;

    xedd._inst = xedi;
    auto &iform_flags(IFORM_FLAGS[xed_inst_iform_enum(xedi)]);
    auto flags = xed_decoded_inst_get_rflags_info(&xedd);

    // Either there are no flags, or there are complex flags interactions. In
    // the case of complex flags interactions that depend on things like
    // prefixes or the values of immediates, we will simply be conservative and
    // assume all flags are read/written.
    if (!flags || xedi->_flag_complex) {
      const auto actions = ICLASS_FLAG_ACTIONS[xed_inst_iclass(xedi)];
      if (actions.is_read) iform_flags.read.flat |= all_flags;
      if (actions.is_write) iform_flags.written.flat |= all_flags;

    // We've got precise flags information.
    } else {
      iform_flags.read.flat |= flags->read.flat;
      iform_flags.written.flat |= flags->written.flat;

      // Turns conditionally written flags into read flags.
      if (flags->may_write) iform_flags.read.flat |= flags->written.flat;
    }
  }
}

// Invoke a function one every implicit operand of each `iclass`.
template <typename FuncT>
static void ForEachImplicitOperand(FuncT func) {
  for (auto isel = 0U; isel < XED_MAX_INST_TABLE_NODES; ++isel) {
    auto instr = xed_inst_table_base() + isel;
    auto iform = xed_inst_iform_enum(instr);
    if (XED_IFORM_INVALID == iform) continue;

    auto iclass = xed_inst_iclass(instr);
    auto num_ops = xed_inst_noperands(instr);
    for (auto i = 0U; i < num_ops; ++i) {
      auto op = xed_inst_operand(instr, i);
      // Ignore `BASE0` and `BASE1` mem ops because we'll record the same info
      // in the memory operand itself.
      auto op_name = xed_operand_name(op);
      if (XED_OPERAND_BASE0 == op_name ||
          XED_OPERAND_BASE1 == op_name) {
        continue;
      }
      if (XED_OPVIS_EXPLICIT != xed_operand_operand_visibility(op) &&
          !IsAmbiguousOperand(iclass, iform, i)) {
        func(instr, op, i, isel);
      }
    }
  }
}

// Returns the total number of implicit operands.
static size_t CountImplicitOperands(void) {
  size_t num_implicit_ops(0);
  auto func = [&] (const xed_inst_t *, const xed_operand_t *, unsigned,
                   unsigned isel) {
    GRANARY_IF_DEBUG( auto new_num_ops = ) ++NUM_IMPLICIT_OPERANDS[isel];
    GRANARY_ASSERT(11 >= new_num_ops);  // Max case is `PUSHAD`.
    ++num_implicit_ops;
  };
  ForEachImplicitOperand(func);
  return num_implicit_ops;
}

// Allocate the implicit operands.
static Operand *AllocateImplicitOperands(void) {
  auto num_implicit_ops = CountImplicitOperands();
  auto ops_mem_size = num_implicit_ops * sizeof(Operand);
  auto aligned_ops_mem_size = ops_mem_size + arch::PAGE_SIZE_BYTES - 1;
  gNumImplicitOperandPages = aligned_ops_mem_size / arch::PAGE_SIZE_BYTES;
  gImplicitOperandPages = os::AllocateDataPages(gNumImplicitOperandPages);
  return reinterpret_cast<Operand *>(gImplicitOperandPages);
}

// Fill in an operand as if it's a register operand.
static void FillRegisterOperand(Operand *instr_op, xed_reg_enum_t reg) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->reg.DecodeFromNative(reg);
  instr_op->width = static_cast<uint16_t>(instr_op->reg.BitWidth());
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
    case XED_NONTERMINAL_ORDX:  // Output to RDX, e.g. in MUL_GPRv.
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

// Set the size of an implicit operand based on its xtype.
static void InitOpSizeByXtype(Operand *instr_op,
                              xed_operand_element_xtype_enum_t xtype) {
  switch (xtype) {
    case XED_OPERAND_XTYPE_B80: instr_op->width = 80; return;
    case XED_OPERAND_XTYPE_F16: instr_op->width = 16; return;
    case XED_OPERAND_XTYPE_F32: instr_op->width = 32; return;
    case XED_OPERAND_XTYPE_F64: instr_op->width = 64; return;
    case XED_OPERAND_XTYPE_F80: instr_op->width = 80; return;
    case XED_OPERAND_XTYPE_I1: instr_op->width = 1; return;
    case XED_OPERAND_XTYPE_I16: instr_op->width = 16; return;
    case XED_OPERAND_XTYPE_I32: instr_op->width = 32; return;
    case XED_OPERAND_XTYPE_I64: instr_op->width = 64; return;
    case XED_OPERAND_XTYPE_I8: instr_op->width = 8; return;
    //case XED_OPERAND_XTYPE_INT:
    //case XED_OPERAND_XTYPE_STRUCT:
    case XED_OPERAND_XTYPE_U128: instr_op->width = 128; return;
    case XED_OPERAND_XTYPE_U16: instr_op->width = 16; return;
    case XED_OPERAND_XTYPE_U256: instr_op->width = 256; return;
    case XED_OPERAND_XTYPE_U32: instr_op->width = 32; return;
    case XED_OPERAND_XTYPE_U64: instr_op->width = 64; return;
    case XED_OPERAND_XTYPE_U8: instr_op->width = 8; return;
    //case XED_OPERAND_XTYPE_UINT:
    //case XED_OPERAND_XTYPE_VAR:
    default: return;
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
  InitOpSizeByXtype(instr_op, xed_operand_xtype(op));
}

// Initializes the implicit operands in the table.
static void InitImplicitOperands(Operand *op) {
  auto func = [&] (const xed_inst_t *instr, const xed_operand_t *instr_op,
                   unsigned i, unsigned isel) {
    InitImplicitOperand(instr, instr_op, op, i);

    // Initialize the first implicit operand for this iform and move to
    // initialize the next operand.
    if (!IMPLICIT_OPERANDS[isel]) {
      IMPLICIT_OPERANDS[isel] = op;
    }
    op++;
  };
  ForEachImplicitOperand(func);
}

// Initialize a table of implicit operands.
//
// TODO(pag): These tables could likely be compressed by quite a bit.
static void InitOperandTables(void) {
  auto ops = AllocateImplicitOperands();
  InitImplicitOperands(ops);
}

// Initialize the register objects. This needs to be done after XED's internal
// tables have been initialized.
static void InitVirtualRegs(void) {
  REG_RFLAGS = VirtualRegister::FromNative(XED_REG_RFLAGS);
  REG_EFLAGS = VirtualRegister::FromNative(XED_REG_EFLAGS);
  REG_FLAGS = VirtualRegister::FromNative(XED_REG_FLAGS);

  REG_AX = VirtualRegister::FromNative(XED_REG_AX);
  REG_CX = VirtualRegister::FromNative(XED_REG_CX);
  REG_DX = VirtualRegister::FromNative(XED_REG_DX);
  REG_BX = VirtualRegister::FromNative(XED_REG_BX);
  REG_SP = VirtualRegister::FromNative(XED_REG_SP);
  REG_BP = VirtualRegister::FromNative(XED_REG_BP);
  REG_SI = VirtualRegister::FromNative(XED_REG_SI);
  REG_DI = VirtualRegister::FromNative(XED_REG_DI);
  REG_R8W = VirtualRegister::FromNative(XED_REG_R8W);
  REG_R9W = VirtualRegister::FromNative(XED_REG_R9W);
  REG_R10W = VirtualRegister::FromNative(XED_REG_R10W);
  REG_R11W = VirtualRegister::FromNative(XED_REG_R11W);
  REG_R12W = VirtualRegister::FromNative(XED_REG_R12W);
  REG_R13W = VirtualRegister::FromNative(XED_REG_R13W);
  REG_R14W = VirtualRegister::FromNative(XED_REG_R14W);
  REG_R15W = VirtualRegister::FromNative(XED_REG_R15W);
  REG_EAX = VirtualRegister::FromNative(XED_REG_EAX);
  REG_ECX = VirtualRegister::FromNative(XED_REG_ECX);
  REG_EDX = VirtualRegister::FromNative(XED_REG_EDX);
  REG_EBX = VirtualRegister::FromNative(XED_REG_EBX);
  REG_ESP = VirtualRegister::FromNative(XED_REG_ESP);
  REG_EBP = VirtualRegister::FromNative(XED_REG_EBP);
  REG_ESI = VirtualRegister::FromNative(XED_REG_ESI);
  REG_EDI = VirtualRegister::FromNative(XED_REG_EDI);
  REG_R8D = VirtualRegister::FromNative(XED_REG_R8D);
  REG_R9D = VirtualRegister::FromNative(XED_REG_R9D);
  REG_R10D = VirtualRegister::FromNative(XED_REG_R10D);
  REG_R11D = VirtualRegister::FromNative(XED_REG_R11D);
  REG_R12D = VirtualRegister::FromNative(XED_REG_R12D);
  REG_R13D = VirtualRegister::FromNative(XED_REG_R13D);
  REG_R14D = VirtualRegister::FromNative(XED_REG_R14D);
  REG_R15D = VirtualRegister::FromNative(XED_REG_R15D);
  REG_RAX = VirtualRegister::FromNative(XED_REG_RAX);
  REG_RCX = VirtualRegister::FromNative(XED_REG_RCX);
  REG_RDX = VirtualRegister::FromNative(XED_REG_RDX);
  REG_RBX = VirtualRegister::FromNative(XED_REG_RBX);
  REG_RSP = VirtualRegister::FromNative(XED_REG_RSP);
  REG_RBP = VirtualRegister::FromNative(XED_REG_RBP);
  REG_RSI = VirtualRegister::FromNative(XED_REG_RSI);
  REG_RDI = VirtualRegister::FromNative(XED_REG_RDI);
  REG_R8 = VirtualRegister::FromNative(XED_REG_R8);
  REG_R9 = VirtualRegister::FromNative(XED_REG_R9);
  REG_R10 = VirtualRegister::FromNative(XED_REG_R10);
  REG_R11 = VirtualRegister::FromNative(XED_REG_R11);
  REG_R12 = VirtualRegister::FromNative(XED_REG_R12);
  REG_R13 = VirtualRegister::FromNative(XED_REG_R13);
  REG_R14 = VirtualRegister::FromNative(XED_REG_R14);
  REG_R15 = VirtualRegister::FromNative(XED_REG_R15);
  REG_AL = VirtualRegister::FromNative(XED_REG_AL);
  REG_CL = VirtualRegister::FromNative(XED_REG_CL);
  REG_DL = VirtualRegister::FromNative(XED_REG_DL);
  REG_BL = VirtualRegister::FromNative(XED_REG_BL);
  REG_SPL = VirtualRegister::FromNative(XED_REG_SPL);
  REG_BPL = VirtualRegister::FromNative(XED_REG_BPL);
  REG_SIL = VirtualRegister::FromNative(XED_REG_SIL);
  REG_DIL = VirtualRegister::FromNative(XED_REG_DIL);
  REG_R8B = VirtualRegister::FromNative(XED_REG_R8B);
  REG_R9B = VirtualRegister::FromNative(XED_REG_R9B);
  REG_R10B = VirtualRegister::FromNative(XED_REG_R10B);
  REG_R11B = VirtualRegister::FromNative(XED_REG_R11B);
  REG_R12B = VirtualRegister::FromNative(XED_REG_R12B);
  REG_R13B = VirtualRegister::FromNative(XED_REG_R13B);
  REG_R14B = VirtualRegister::FromNative(XED_REG_R14B);
  REG_R15B = VirtualRegister::FromNative(XED_REG_R15B);
  REG_AH = VirtualRegister::FromNative(XED_REG_AH);
  REG_CH = VirtualRegister::FromNative(XED_REG_CH);
  REG_DH = VirtualRegister::FromNative(XED_REG_DH);
  REG_BH = VirtualRegister::FromNative(XED_REG_BH);
  REG_ERROR = VirtualRegister::FromNative(XED_REG_ERROR);
  REG_RIP = VirtualRegister::FromNative(XED_REG_RIP);
  REG_EIP = VirtualRegister::FromNative(XED_REG_EIP);
  REG_IP = VirtualRegister::FromNative(XED_REG_IP);
  REG_K0 = VirtualRegister::FromNative(XED_REG_K0);
  REG_K1 = VirtualRegister::FromNative(XED_REG_K1);
  REG_K2 = VirtualRegister::FromNative(XED_REG_K2);
  REG_K3 = VirtualRegister::FromNative(XED_REG_K3);
  REG_K4 = VirtualRegister::FromNative(XED_REG_K4);
  REG_K5 = VirtualRegister::FromNative(XED_REG_K5);
  REG_K6 = VirtualRegister::FromNative(XED_REG_K6);
  REG_K7 = VirtualRegister::FromNative(XED_REG_K7);
  REG_MMX0 = VirtualRegister::FromNative(XED_REG_MMX0);
  REG_MMX1 = VirtualRegister::FromNative(XED_REG_MMX1);
  REG_MMX2 = VirtualRegister::FromNative(XED_REG_MMX2);
  REG_MMX3 = VirtualRegister::FromNative(XED_REG_MMX3);
  REG_MMX4 = VirtualRegister::FromNative(XED_REG_MMX4);
  REG_MMX5 = VirtualRegister::FromNative(XED_REG_MMX5);
  REG_MMX6 = VirtualRegister::FromNative(XED_REG_MMX6);
  REG_MMX7 = VirtualRegister::FromNative(XED_REG_MMX7);
  REG_CS = VirtualRegister::FromNative(XED_REG_CS);
  REG_DS = VirtualRegister::FromNative(XED_REG_DS);
  REG_ES = VirtualRegister::FromNative(XED_REG_ES);
  REG_SS = VirtualRegister::FromNative(XED_REG_SS);
  REG_FS = VirtualRegister::FromNative(XED_REG_FS);
  REG_GS = VirtualRegister::FromNative(XED_REG_GS);
  REG_ST0 = VirtualRegister::FromNative(XED_REG_ST0);
  REG_ST1 = VirtualRegister::FromNative(XED_REG_ST1);
  REG_ST2 = VirtualRegister::FromNative(XED_REG_ST2);
  REG_ST3 = VirtualRegister::FromNative(XED_REG_ST3);
  REG_ST4 = VirtualRegister::FromNative(XED_REG_ST4);
  REG_ST5 = VirtualRegister::FromNative(XED_REG_ST5);
  REG_ST6 = VirtualRegister::FromNative(XED_REG_ST6);
  REG_ST7 = VirtualRegister::FromNative(XED_REG_ST7);
  REG_XCR0 = VirtualRegister::FromNative(XED_REG_XCR0);
  REG_XMM0 = VirtualRegister::FromNative(XED_REG_XMM0);
  REG_XMM1 = VirtualRegister::FromNative(XED_REG_XMM1);
  REG_XMM2 = VirtualRegister::FromNative(XED_REG_XMM2);
  REG_XMM3 = VirtualRegister::FromNative(XED_REG_XMM3);
  REG_XMM4 = VirtualRegister::FromNative(XED_REG_XMM4);
  REG_XMM5 = VirtualRegister::FromNative(XED_REG_XMM5);
  REG_XMM6 = VirtualRegister::FromNative(XED_REG_XMM6);
  REG_XMM7 = VirtualRegister::FromNative(XED_REG_XMM7);
  REG_XMM8 = VirtualRegister::FromNative(XED_REG_XMM8);
  REG_XMM9 = VirtualRegister::FromNative(XED_REG_XMM9);
  REG_XMM10 = VirtualRegister::FromNative(XED_REG_XMM10);
  REG_XMM11 = VirtualRegister::FromNative(XED_REG_XMM11);
  REG_XMM12 = VirtualRegister::FromNative(XED_REG_XMM12);
  REG_XMM13 = VirtualRegister::FromNative(XED_REG_XMM13);
  REG_XMM14 = VirtualRegister::FromNative(XED_REG_XMM14);
  REG_XMM15 = VirtualRegister::FromNative(XED_REG_XMM15);
  REG_XMM16 = VirtualRegister::FromNative(XED_REG_XMM16);
  REG_XMM17 = VirtualRegister::FromNative(XED_REG_XMM17);
  REG_XMM18 = VirtualRegister::FromNative(XED_REG_XMM18);
  REG_XMM19 = VirtualRegister::FromNative(XED_REG_XMM19);
  REG_XMM20 = VirtualRegister::FromNative(XED_REG_XMM20);
  REG_XMM21 = VirtualRegister::FromNative(XED_REG_XMM21);
  REG_XMM22 = VirtualRegister::FromNative(XED_REG_XMM22);
  REG_XMM23 = VirtualRegister::FromNative(XED_REG_XMM23);
  REG_XMM24 = VirtualRegister::FromNative(XED_REG_XMM24);
  REG_XMM25 = VirtualRegister::FromNative(XED_REG_XMM25);
  REG_XMM26 = VirtualRegister::FromNative(XED_REG_XMM26);
  REG_XMM27 = VirtualRegister::FromNative(XED_REG_XMM27);
  REG_XMM28 = VirtualRegister::FromNative(XED_REG_XMM28);
  REG_XMM29 = VirtualRegister::FromNative(XED_REG_XMM29);
  REG_XMM30 = VirtualRegister::FromNative(XED_REG_XMM30);
  REG_XMM31 = VirtualRegister::FromNative(XED_REG_XMM31);
  REG_YMM0 = VirtualRegister::FromNative(XED_REG_YMM0);
  REG_YMM1 = VirtualRegister::FromNative(XED_REG_YMM1);
  REG_YMM2 = VirtualRegister::FromNative(XED_REG_YMM2);
  REG_YMM3 = VirtualRegister::FromNative(XED_REG_YMM3);
  REG_YMM4 = VirtualRegister::FromNative(XED_REG_YMM4);
  REG_YMM5 = VirtualRegister::FromNative(XED_REG_YMM5);
  REG_YMM6 = VirtualRegister::FromNative(XED_REG_YMM6);
  REG_YMM7 = VirtualRegister::FromNative(XED_REG_YMM7);
  REG_YMM8 = VirtualRegister::FromNative(XED_REG_YMM8);
  REG_YMM9 = VirtualRegister::FromNative(XED_REG_YMM9);
  REG_YMM10 = VirtualRegister::FromNative(XED_REG_YMM10);
  REG_YMM11 = VirtualRegister::FromNative(XED_REG_YMM11);
  REG_YMM12 = VirtualRegister::FromNative(XED_REG_YMM12);
  REG_YMM13 = VirtualRegister::FromNative(XED_REG_YMM13);
  REG_YMM14 = VirtualRegister::FromNative(XED_REG_YMM14);
  REG_YMM15 = VirtualRegister::FromNative(XED_REG_YMM15);
  REG_YMM16 = VirtualRegister::FromNative(XED_REG_YMM16);
  REG_YMM17 = VirtualRegister::FromNative(XED_REG_YMM17);
  REG_YMM18 = VirtualRegister::FromNative(XED_REG_YMM18);
  REG_YMM19 = VirtualRegister::FromNative(XED_REG_YMM19);
  REG_YMM20 = VirtualRegister::FromNative(XED_REG_YMM20);
  REG_YMM21 = VirtualRegister::FromNative(XED_REG_YMM21);
  REG_YMM22 = VirtualRegister::FromNative(XED_REG_YMM22);
  REG_YMM23 = VirtualRegister::FromNative(XED_REG_YMM23);
  REG_YMM24 = VirtualRegister::FromNative(XED_REG_YMM24);
  REG_YMM25 = VirtualRegister::FromNative(XED_REG_YMM25);
  REG_YMM26 = VirtualRegister::FromNative(XED_REG_YMM26);
  REG_YMM27 = VirtualRegister::FromNative(XED_REG_YMM27);
  REG_YMM28 = VirtualRegister::FromNative(XED_REG_YMM28);
  REG_YMM29 = VirtualRegister::FromNative(XED_REG_YMM29);
  REG_YMM30 = VirtualRegister::FromNative(XED_REG_YMM30);
  REG_YMM31 = VirtualRegister::FromNative(XED_REG_YMM31);
  REG_ZMM0 = VirtualRegister::FromNative(XED_REG_ZMM0);
  REG_ZMM1 = VirtualRegister::FromNative(XED_REG_ZMM1);
  REG_ZMM2 = VirtualRegister::FromNative(XED_REG_ZMM2);
  REG_ZMM3 = VirtualRegister::FromNative(XED_REG_ZMM3);
  REG_ZMM4 = VirtualRegister::FromNative(XED_REG_ZMM4);
  REG_ZMM5 = VirtualRegister::FromNative(XED_REG_ZMM5);
  REG_ZMM6 = VirtualRegister::FromNative(XED_REG_ZMM6);
  REG_ZMM7 = VirtualRegister::FromNative(XED_REG_ZMM7);
  REG_ZMM8 = VirtualRegister::FromNative(XED_REG_ZMM8);
  REG_ZMM9 = VirtualRegister::FromNative(XED_REG_ZMM9);
  REG_ZMM10 = VirtualRegister::FromNative(XED_REG_ZMM10);
  REG_ZMM11 = VirtualRegister::FromNative(XED_REG_ZMM11);
  REG_ZMM12 = VirtualRegister::FromNative(XED_REG_ZMM12);
  REG_ZMM13 = VirtualRegister::FromNative(XED_REG_ZMM13);
  REG_ZMM14 = VirtualRegister::FromNative(XED_REG_ZMM14);
  REG_ZMM15 = VirtualRegister::FromNative(XED_REG_ZMM15);
  REG_ZMM16 = VirtualRegister::FromNative(XED_REG_ZMM16);
  REG_ZMM17 = VirtualRegister::FromNative(XED_REG_ZMM17);
  REG_ZMM18 = VirtualRegister::FromNative(XED_REG_ZMM18);
  REG_ZMM19 = VirtualRegister::FromNative(XED_REG_ZMM19);
  REG_ZMM20 = VirtualRegister::FromNative(XED_REG_ZMM20);
  REG_ZMM21 = VirtualRegister::FromNative(XED_REG_ZMM21);
  REG_ZMM22 = VirtualRegister::FromNative(XED_REG_ZMM22);
  REG_ZMM23 = VirtualRegister::FromNative(XED_REG_ZMM23);
  REG_ZMM24 = VirtualRegister::FromNative(XED_REG_ZMM24);
  REG_ZMM25 = VirtualRegister::FromNative(XED_REG_ZMM25);
  REG_ZMM26 = VirtualRegister::FromNative(XED_REG_ZMM26);
  REG_ZMM27 = VirtualRegister::FromNative(XED_REG_ZMM27);
  REG_ZMM28 = VirtualRegister::FromNative(XED_REG_ZMM28);
  REG_ZMM29 = VirtualRegister::FromNative(XED_REG_ZMM29);
  REG_ZMM30 = VirtualRegister::FromNative(XED_REG_ZMM30);
  REG_ZMM31 = VirtualRegister::FromNative(XED_REG_ZMM31);
}

}  // namespace

// Initialize the driver (instruction encoder/decoder).
void Init(void) {
  xed_tables_init();
  xed_state_zero(&XED_STATE);
  xed_state_init(&XED_STATE, XED_MACHINE_MODE_LONG_64,
                 XED_ADDRESS_WIDTH_64b, XED_ADDRESS_WIDTH_64b);
  InitIclassTables();
  InitIclassFlags();
  InitIformFlags();
  InitOperandTables();
  InitVirtualRegs();
  InitBlockTracer();
}

// Exit the driver.
void Exit(void) {
  memset(IMPLICIT_OPERANDS, 0, sizeof IMPLICIT_OPERANDS);
  memset(NUM_IMPLICIT_OPERANDS, 0, sizeof NUM_IMPLICIT_OPERANDS);
  os::FreeDataPages(gImplicitOperandPages, gNumImplicitOperandPages);
}
}  // namespace arch
}  // namespace granary
