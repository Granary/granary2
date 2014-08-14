/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifdef GRANARY_WHERE_kernel

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/base/cast.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"

#include "os/annotate.h"
#include "os/module.h"

#include "os/linux/kernel/module.h"

#define BEFORE(...) \
  do { \
    __VA_ARGS__; \
    instr->UnsafeInsertBefore(new NativeInstruction(&ni)); \
  } while (0)

#define AFTER(...) \
  do { \
    __VA_ARGS__; \
    next_instr->UnsafeInsertBefore(new NativeInstruction(&ni)); \
  } while (0)

extern "C" {

// These functions are defined in `os/linux/arch/x86-64/uaccess.asm`. They
// don't follow the normal calling convention and therefore should not be
// invoked directly.
//
// Note: These functions return `RCX = 0` on success and `RCX = 1` on failure.
//       This is because `0` means no fault, and `1` means there was a fault.

extern void granary_uaccess_read_8(void);
extern void granary_uaccess_read_16(void);
extern void granary_uaccess_read_32(void);
extern void granary_uaccess_read_64(void);

extern void granary_uaccess_write_8(void);
extern void granary_uaccess_write_16(void);
extern void granary_uaccess_write_32(void);
extern void granary_uaccess_write_64(void);

extern void granary_uaccess_read_error_8(void);
extern void granary_uaccess_read_error_16(void);
extern void granary_uaccess_read_error_32(void);
extern void granary_uaccess_read_error_64(void);

extern void granary_uaccess_write_error_8(void);
extern void granary_uaccess_write_error_16(void);
extern void granary_uaccess_write_error_32(void);
extern void granary_uaccess_write_error_64(void);

}  // extern C
namespace granary {
namespace os {
namespace {

typedef void (*ExceptionFuncPtr)(void);

ExceptionFuncPtr exception_funcs[2][2][4] = {
  {{granary_uaccess_read_8, granary_uaccess_read_16,
    granary_uaccess_read_32, granary_uaccess_read_64},
   {granary_uaccess_write_8, granary_uaccess_write_16,
    granary_uaccess_write_32, granary_uaccess_write_64}},
  {{granary_uaccess_read_error_8, granary_uaccess_read_error_16,
    granary_uaccess_read_error_32, granary_uaccess_read_error_64},
   {granary_uaccess_write_error_8, granary_uaccess_write_error_16,
    granary_uaccess_write_error_32, granary_uaccess_write_error_64}},
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
    auto mid = first + ((last - first) / 2);
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

// Checks to see if a recovery PC is a `uaccess_err` entry. These are created
// by the `_ASM_EXTABLE_EX` kernel macro. Non-`uaccess_err` entries are
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

// Log base 2 of a memory operand's bit width.
static int Order(int bit_width) {
  if (64 == bit_width) return 3;
  if (32 == bit_width) return 2;
  if (16 == bit_width) return 1;
  return 0;
}

}  // namespace

// Annotate an application instruction.
//
// For the Linux kernel, what we look for is instructions that might access
// user space memory, and therefore might fault. In these cases, we bring in
// the fixup code as an actual basic block, thus making the exceptional control
// flow explicit.
void AnnotateAppInstruction(BlockFactory *factory, DecodedBasicBlock *block,
                            NativeInstruction *instr, AppPC next_pc) {
  if (IsA<ControlFlowInstruction *>(instr)) return;

  auto fault_pc = instr->DecodedPC();
  auto module = FindModuleContainingPC(fault_pc);

  // Get this module's exception tables.
  auto exception_tables = reinterpret_cast<const ExceptionTableBounds *>(
      module->where_data);
  if (!exception_tables) return;

  // Search the exception tables for the page fault recovery address.
  auto recovery_entry = FindRecoveryEntry(exception_tables->start,
                                          exception_tables->stop,
                                          fault_pc);
  if (GRANARY_LIKELY(!recovery_entry)) return;

  auto recovers_from_error = RecoveryEntryIsError(recovery_entry);
  auto recovery_pc = FindRecoveryAddress(recovery_entry, recovers_from_error);


  auto is_write = false;
  auto mem_size = -1;

  MemoryOperand dest, src;
  arch::Instruction ni;
  arch::Operand mloc;

  // TODO!!!! Not all exception table entries seem to be "right"... Figure
  //          this out! They might not be sorted :-(

  if (2 == instr->CountMatchedOperands(WriteTo(dest), ReadFrom(src))) {
    // TODO(pag): How do we figure out which memory location is the one that
    //            is (semantically) allowed to fault?
    //
    //            See Issue #19.
    NOP_90(&(instr->instruction));
    instr->InsertAfter(lir::Jump(new NativeBasicBlock(fault_pc)));
    return;

  // Writes to user space memory.
  } else if (instr->MatchOperands(WriteTo(dest))) {
    is_write = true;
    mem_size = dest.BitWidth();
    memcpy(&mloc, dest.Extract(), sizeof mloc);

  // Reads from user space memory.
  } else if (instr->MatchOperands(ReadFrom(src))) {
    mem_size = dest.BitWidth();
    memcpy(&mloc, src.Extract(), sizeof mloc);

  } else {
    // TODO(pag): This is weird.
    return;
  }

  // Double check that the stack pointer isn't operated on. This is mostly
  // just a sanity check that relates back to mangling concerns, because
  GRANARY_ASSERT(!instr->instruction.ReadsFromStackPointer() &&
                 !instr->instruction.WritesToStackPointer());

  auto saved_rcx = block->AllocateVirtualRegister();
  auto next_instr = instr->Next();

  // Make sure the next instruction is the `IA_END_BASIC_BLOCK` instruction.
  GRANARY_ASSERT(!next_instr->Next());

  // Just assume that the stack is valid, it's easier that way.
  instr->UnsafeInsertBefore(new AnnotationInstruction(IA_VALID_STACK));
  BEFORE(MOV_GPRv_GPRv_89(&ni, saved_rcx, XED_REG_RCX));
  BEFORE(LEA_GPRv_AGEN(&ni, XED_REG_RCX, mloc));
  BEFORE(CALL_NEAR_RELBRd(&ni, exception_funcs[recovers_from_error]
                                              [is_write]
                                              [Order(mem_size)]);
         ni.is_stack_blind = true; );
  auto label_no_fault = new LabelInstruction;
  JRCXZ_RELBRb(&ni, label_no_fault);
  instr->UnsafeInsertBefore(new BranchInstruction(&ni, label_no_fault));

  BEFORE(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  instr->InsertBefore(lir::Jump(factory, recovery_pc));

  instr->UnsafeInsertBefore(label_no_fault);

  // <instr goes here>
  instr->instruction.is_sticky = true;
  AFTER(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  next_instr->InsertBefore(lir::Jump(factory, next_pc));
}

}  // namespace os
}  // namespace granary

#endif  // GRANARY_WHERE_kernel
