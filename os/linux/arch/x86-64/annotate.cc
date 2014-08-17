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

extern void granary_uaccess_write_seg_fs(void);
extern void granary_uaccess_write_seg_gs(void);
extern void granary_uaccess_write_seg_cs(void);
extern void granary_uaccess_write_seg_ds(void);
extern void granary_uaccess_write_seg_es(void);
extern void granary_uaccess_write_seg_ss(void);

extern void granary_uaccess_rdmsr(void);
extern void granary_uaccess_wrmsr(void);

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

// Same order as in `xed_reg_enum_t`.
ExceptionFuncPtr segment_funcs[6] = {
  granary_uaccess_write_seg_cs, granary_uaccess_write_seg_ds,
  granary_uaccess_write_seg_es, granary_uaccess_write_seg_ss,
  granary_uaccess_write_seg_fs, granary_uaccess_write_seg_gs
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

// Searches backward through the instruction list to find an
// `IA_CHANGES_INTERRUPT_STATE` that we expect to be related to a `WRMSR`
// instruction.
static AnnotationInstruction *FindInterruptChange(Instruction *instr) {
  for (; instr; instr = instr->Previous()) {
    if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_CHANGES_INTERRUPT_STATE == annot->annotation) {
        return annot;
      }
    }
  }
  GRANARY_ASSERT(false);
  return nullptr;
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
  auto remove_instr = false;
  auto load_rcx_with_mloc = true;

  MemoryOperand dest, src;
  ExceptionFuncPtr handler(nullptr);
  arch::Instruction ni;
  arch::Operand mloc;

  if (2 == instr->CountMatchedOperands(WriteTo(dest), ReadFrom(src))) {
    // TODO(pag): How do we figure out which memory location is the one that
    //            is (semantically) allowed to fault?
    //
    //            See Issue #19.


  // Writes to user space memory.
  } else if (instr->MatchOperands(WriteTo(dest))) {
    is_write = true;
    mem_size = dest.BitWidth();
    memcpy(&mloc, dest.Extract(), sizeof mloc);

  // Reads from user space memory.
  } else if (instr->MatchOperands(ReadFrom(src))) {
    mem_size = dest.BitWidth();
    memcpy(&mloc, src.Extract(), sizeof mloc);

  // Writes to a segment register.
  } else if (XED_IFORM_MOV_SEG_GPR16 == instr->instruction.iform) {
    auto seg_reg = instr->instruction.ops[0].reg.Number();
    GRANARY_ASSERT(XED_REG_CS <= seg_reg && seg_reg <= XED_REG_GS);
    auto source_reg = instr->instruction.ops[1].reg.
        WidenedTo(arch::GPR_WIDTH_BYTES).EncodeToNative();
    handler = segment_funcs[seg_reg - XED_REG_CS];
    mloc = arch::BaseDispMemOp(0, static_cast<xed_reg_enum_t>(source_reg),
                               arch::GPR_WIDTH_BITS);
    mloc.is_effective_address = true;
    remove_instr = true;

  // "Safe" read of a model-specific register.
  } else if (XED_ICLASS_RDMSR == instr->instruction.iclass) {
    handler = granary_uaccess_rdmsr;
    remove_instr = true;
    load_rcx_with_mloc = false;

  // "Safe" write to a model-specific register.
  } else if (XED_ICLASS_WRMSR == instr->instruction.iclass) {
    // Force a fragment split, while making it seem like there isn't a a change
    // in interrupt status.
    //
    // TODO(pag): This is a pretty ugly solution to the problem of the fragment
    //            containing the below call to change the MSR being put in
    //            a different partition than the fragment containing the
    //            `JRCXZ`.
    auto annot = FindInterruptChange(instr->Previous());
    annot->annotation = IA_UNKNOWN_STACK_BELOW;

    handler = granary_uaccess_wrmsr;
    remove_instr = true;
    load_rcx_with_mloc = false;

  } else {
    granary_curiosity();  // TODO(pag): This is weird.
    return;
  }

  if (!handler) {
    if (-1 == mem_size) {
      NOP_90(&(instr->instruction));
      instr->InsertAfter(lir::Jump(new NativeBasicBlock(fault_pc)));
      return;
    }
    GRANARY_ASSERT(XED_IFORM_MOV_SEG_MEMw != instr->instruction.iform);
    GRANARY_ASSERT(-1 != mem_size);
    handler = exception_funcs[recovers_from_error][is_write][Order(mem_size)];
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
  if (load_rcx_with_mloc) BEFORE(LEA_GPRv_AGEN(&ni, XED_REG_RCX, mloc));
  BEFORE(CALL_NEAR_RELBRd(&ni, handler);
         ni.is_stack_blind = true;
         ni.analyzed_stack_usage = false; );
  auto label_no_fault = new LabelInstruction;
  JRCXZ_RELBRb(&ni, label_no_fault);
  instr->UnsafeInsertBefore(new BranchInstruction(&ni, label_no_fault));

  BEFORE(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  instr->InsertBefore(lir::Jump(factory, recovery_pc, REQUEST_DENIED));

  instr->UnsafeInsertBefore(label_no_fault);
  BEFORE(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  // `instr` is here. Need to restore `RCX` before `instr` just in case
  // `RCX` is used by `instr`.
  instr->instruction.is_sticky = true;

  next_instr->InsertBefore(lir::Jump(factory, next_pc, REQUEST_CHECK_LCFG));

  // If the `handler` itself emulates the instruction, then we don't want to
  // encode the instruction. However, we don't want to clobber it into a `NOP`
  // either because then if it has some register dependencies, then those will
  // be hidden from the VR system. Therefore, we leave these instructions in
  // but mark them as non-encodable.
  if (remove_instr) instr->instruction.dont_encode = true;
}

}  // namespace os
}  // namespace granary

#endif  // GRANARY_WHERE_kernel
