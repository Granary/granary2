/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/driver.h"

#include "granary/base/cast.h"

#include "os/exception.h"
#include "os/module.h"

#ifdef GRANARY_WHERE_kernel
# include "os/linux/kernel/module.h"
#endif  // GRANARY_WHERE_kernel

extern "C" {
#ifdef GRANARY_WHERE_kernel
extern const uint8_t granary_extable_rep_movs_8;
extern const uint8_t granary_extable_rep_movs_16;
extern const uint8_t granary_extable_rep_movs_32;
extern const uint8_t granary_extable_rep_movs_64;

extern const uint8_t granary_extable_write_seg_cs;
extern const uint8_t granary_extable_write_seg_ds;
extern const uint8_t granary_extable_write_seg_es;
extern const uint8_t granary_extable_write_seg_ss;
extern const uint8_t granary_extable_write_seg_fs;
extern const uint8_t granary_extable_write_seg_gs;

extern const unsigned char granary_extable_write_8;
extern const unsigned char granary_extable_write_16;
extern const unsigned char granary_extable_write_32;
extern const unsigned char granary_extable_write_64;

extern const unsigned char granary_extable_write_error_8;
extern const unsigned char granary_extable_write_error_16;
extern const unsigned char granary_extable_write_error_32;
extern const unsigned char granary_extable_write_error_64;

extern const unsigned char granary_extable_read_8;
extern const unsigned char granary_extable_read_16;
extern const unsigned char granary_extable_read_32;
extern const unsigned char granary_extable_read_64;

extern const unsigned char granary_extable_read_error_8;
extern const unsigned char granary_extable_read_error_16;
extern const unsigned char granary_extable_read_error_32;
extern const unsigned char granary_extable_read_error_64;

extern const unsigned char granary_extable_xchg_8;
extern const unsigned char granary_extable_xchg_16;
extern const unsigned char granary_extable_xchg_32;
extern const unsigned char granary_extable_xchg_64;

extern const unsigned char granary_extable_rdmsr;
extern const unsigned char granary_extable_wrmsr;
extern const unsigned char granary_extable_fwait;
extern const unsigned char granary_extable_fxrstor64;
extern const unsigned char granary_extable_prefetcht0;

#endif  // GRANARY_WHERE_kernel

}  // extern C
namespace granary {
namespace os {

#ifdef GRANARY_WHERE_user
// Returns true if the instruction `instr` can cause an exception, and if
// so, updates the application-specific recovery PC, and updates the Granary-
// specific emulation PC for the instruction.
bool GetExceptionInfo(const arch::Instruction *, AppPC *, AppPC *) {
  return false;
}

#else

namespace {

// Same order as in `xed_reg_enum_t`.
static const AppPC emulate_write_seg[6] = {
  &granary_extable_write_seg_cs, &granary_extable_write_seg_ds,
  &granary_extable_write_seg_es, &granary_extable_write_seg_ss,
  &granary_extable_write_seg_fs, &granary_extable_write_seg_gs
};

static const AppPC emulate_write_mem[2][4] = {
  {
    &granary_extable_write_8, &granary_extable_write_16,
    &granary_extable_write_32, &granary_extable_write_64
  },
  {
    &granary_extable_write_error_8, &granary_extable_write_error_16,
    &granary_extable_write_error_32, &granary_extable_write_error_64
  }
};

static const AppPC emulate_read_mem[2][4] = {
  {
    &granary_extable_read_8, &granary_extable_read_16,
    &granary_extable_read_32, &granary_extable_read_64
  },
  {
    &granary_extable_read_error_8, &granary_extable_read_error_16,
    &granary_extable_read_error_32, &granary_extable_read_error_64
  }
};


static const AppPC emulate_xchg[4] = {
  &granary_extable_xchg_8, &granary_extable_xchg_16,
  &granary_extable_xchg_32, &granary_extable_xchg_64
};

// Returns the faulting PC of an exception table entry.
static AppPC FaultPC(const ExceptionTableEntry *entry) {
  return UnsafeCast<AppPC>(&(entry->fault_addr_rel32)) +
         entry->fault_addr_rel32;
}

// Performs a binary search of the exception tables from some module,
static const ExceptionTableEntry *FindRecoveryEntry(
    const ExceptionTableEntry *first,
    const ExceptionTableEntry *last, AppPC pc) {
  while (first <= last) {
    const ExceptionTableEntry *mid = first + ((last - first) / 2);
    auto fault_pc = FaultPC(mid);
    if (fault_pc < pc) {
      first = mid + 1;
    } else if (fault_pc > pc) {
      last = mid - 1;
    } else {
      return mid;
    }
  }
  return nullptr;
}


// Checks to see if a recovery PC is a `extable_err` entry. These are created
// by the `_ASM_EXTABLE_EX` kernel macro. Non-`extable_err` entries are
// created with `_ASM_EXTABLE`.
static bool RecoveryEntryIsError(const ExceptionTableEntry *entry) {
  return (entry->fixup_addr_rel32 - entry->fault_addr_rel32) >=
         (0x7ffffff0 - 4);
}

// Returns the recovery PC of an exception table entry.
static AppPC RecoveryPC(const ExceptionTableEntry *entry) {
  return UnsafeCast<AppPC>(&(entry->fixup_addr_rel32)) +
         entry->fixup_addr_rel32;
}

static AppPC FindRecoveryAddress(const ExceptionTableEntry *entry,
                                 bool recovers_from_error) {
  auto recovery_pc = RecoveryPC(entry);
  if (recovers_from_error) {
    recovery_pc -= 0x7ffffff0;
  }
  return recovery_pc;
}

// Log base 2 of a number.
static int Order(int bit_width) {
  if (64 == bit_width) return 3;
  if (32 == bit_width) return 2;
  if (16 == bit_width) return 1;
  return 0;
}

// Log base 2 of an instruction's effective operand bit width.
static int Order(const arch::Operand &op) {
  return Order(op.BitWidth());
}

// Gets the emulation PC for an instruction that overwrites a segment
// register.
static bool GetExceptionInfoSeg(const arch::Instruction *instr,
                                AppPC *emulation_pc) {
  auto seg_reg = instr->ops[0].reg.Number();
  GRANARY_ASSERT(XED_REG_CS <= seg_reg && seg_reg <= XED_REG_GS);
  *emulation_pc = emulate_write_seg[seg_reg - XED_REG_CS];
  return true;
}

#ifdef GRANARY_TARGET_debug
static bool NotASegmentOffset(const arch::Operand &op) {
  return XED_REG_INVALID == op.segment || XED_REG_DS == op.segment;
}
#endif  // GRANARY_TARGET_debug

}  // namespace

// Returns true if the instruction `instr` can cause an exception, and if
// so, updates the application-specific recovery PC, and updates the Granary-
// specific emulation PC for the instruction.
bool GetExceptionInfo(const arch::Instruction *instr, AppPC *recovery_pc,
                      AppPC *emulation_pc) {
  auto fault_pc = instr->DecodedPC();
  auto module = ModuleContainingPC(fault_pc);
  if (!module) return false;

  // Get this module's exception tables.
  auto exception_tables = reinterpret_cast<const ExceptionTableBounds *>(
      module->where_data);
  if (!exception_tables) return false;

  // Search the exception tables for the page fault recovery address.
  auto recovery_entry = FindRecoveryEntry(exception_tables->start,
                                          exception_tables->stop,
                                          fault_pc);
  if (GRANARY_LIKELY(!recovery_entry)) return false;

  auto recovers_from_error = RecoveryEntryIsError(recovery_entry);
  *recovery_pc = FindRecoveryAddress(recovery_entry, recovers_from_error);

  switch (instr->iform) {
    case XED_IFORM_MOVSB:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_rep_movs_8;
      return true;

    case XED_IFORM_MOVSW:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_rep_movs_16;
      return true;

    case XED_IFORM_MOVSD:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_rep_movs_32;
      return true;

    case XED_IFORM_MOVSQ:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_rep_movs_64;
      return true;

    case XED_IFORM_WRMSR:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_wrmsr;
      return true;

    case XED_IFORM_RDMSR:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_rdmsr;
      return true;

    case XED_IFORM_FWAIT:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_fwait;
      return true;

    case XED_IFORM_FXRSTOR64_MEMmfpxenv:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_fxrstor64;
      return true;

    case XED_IFORM_PREFETCHT0_MEMmprefetch:
      GRANARY_ASSERT(!recovers_from_error);
      *emulation_pc = &granary_extable_prefetcht0;
      return true;

    case XED_IFORM_XCHG_MEMb_GPR8:
      GRANARY_ASSERT(!recovers_from_error);
      GRANARY_ASSERT(NotASegmentOffset(instr->ops[0]));
      *emulation_pc = &granary_extable_xchg_8;
      return true;

    case XED_IFORM_XCHG_MEMv_GPRv:
      GRANARY_ASSERT(!recovers_from_error);
      GRANARY_ASSERT(NotASegmentOffset(instr->ops[0]));
      *emulation_pc = emulate_xchg[Order(instr->ops[0])];
      return true;

    case XED_IFORM_MOV_SEG_MEMw:
    case XED_IFORM_MOV_SEG_GPR16:
      GRANARY_ASSERT(!recovers_from_error);
      return GetExceptionInfoSeg(instr, emulation_pc);

    case XED_IFORM_MOV_MEMb_GPR8:
    case XED_IFORM_MOV_MEMb_IMMb:
      GRANARY_ASSERT(NotASegmentOffset(instr->ops[0]));
      *emulation_pc = emulate_write_mem[recovers_from_error][0];
      return true;

    case XED_IFORM_MOV_MEMv_GPRv:
      GRANARY_ASSERT(NotASegmentOffset(instr->ops[0]));
      *emulation_pc = emulate_write_mem[recovers_from_error]
                                       [Order(instr->ops[0])];
      return true;

    case XED_IFORM_MOV_GPR8_MEMb:
    case XED_IFORM_MOV_GPRv_MEMv:
      GRANARY_ASSERT(NotASegmentOffset(instr->ops[1]));
      *emulation_pc = emulate_read_mem[recovers_from_error]
                                      [Order(instr->ops[1])];
      return true;

    default:
      granary_curiosity();
      return false;
  }
}

#endif  // GRANARY_WHERE_kernel

}  // namespace os
}  // namespace granary
