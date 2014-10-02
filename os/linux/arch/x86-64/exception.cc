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

extern const uint8_t granary_extable_rep_movs_8;
extern const uint8_t granary_extable_rep_movs_16;
extern const uint8_t granary_extable_rep_movs_32;
extern const uint8_t granary_extable_rep_movs_64;

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

  switch (instr->iclass) {
    case XED_ICLASS_MOVSB:
      *emulation_pc = &granary_extable_rep_movs_8;
      return true;
    case XED_ICLASS_MOVSS:
      *emulation_pc = &granary_extable_rep_movs_16;
      return true;
    case XED_ICLASS_MOVSD:
      *emulation_pc = &granary_extable_rep_movs_32;
      return true;
    case XED_ICLASS_MOVSQ:
      *emulation_pc = &granary_extable_rep_movs_64;
      return true;

    default:
      granary_curiosity();
      return false;
  }
}

#endif  // GRANARY_WHERE_kernel

}  // namespace os
}  // namespace granary
