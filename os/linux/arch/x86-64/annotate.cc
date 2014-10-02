/* Copyright 2014 Peter Goodman, all rights reserved. */

#if 0

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

extern bool FLAG_debug_log_fragments;

#define BEFORE(...) \
  do { \
    __VA_ARGS__; \
    instr->InsertBefore(new NativeInstruction(&ni)); \
  } while (0)

#define AFTER(...) \
  do { \
    __VA_ARGS__; \
    next_instr->InsertBefore(new NativeInstruction(&ni)); \
  } while (0)

extern "C" {

// These functions are defined in `os/linux/arch/x86-64/extable.asm`. They
// don't follow the normal calling convention and therefore should not be
// invoked directly.
//
// Note: These functions return `RCX = 0` on success and `RCX = 1` on failure.
//       This is because `0` means no fault, and `1` means there was a fault.

extern void granary_extable_read_8(void);
extern void granary_extable_read_16(void);
extern void granary_extable_read_32(void);
extern void granary_extable_read_64(void);

extern void granary_extable_write_8(void);
extern void granary_extable_write_16(void);
extern void granary_extable_write_32(void);
extern void granary_extable_write_64(void);

extern void granary_extable_read_error_8(void);
extern void granary_extable_read_error_16(void);
extern void granary_extable_read_error_32(void);
extern void granary_extable_read_error_64(void);

extern void granary_extable_write_error_8(void);
extern void granary_extable_write_error_16(void);
extern void granary_extable_write_error_32(void);
extern void granary_extable_write_error_64(void);

extern void granary_extable_write_seg_fs(void);
extern void granary_extable_write_seg_gs(void);
extern void granary_extable_write_seg_cs(void);
extern void granary_extable_write_seg_ds(void);
extern void granary_extable_write_seg_es(void);
extern void granary_extable_write_seg_ss(void);

extern void granary_extable_rdmsr(void);
extern void granary_extable_wrmsr(void);

extern void granary_extable_fwait(void);

}  // extern C
namespace granary {
namespace os {
namespace {

#if 0
typedef void (*ExceptionFuncPtr)(void);

ExceptionFuncPtr exception_funcs[2][2][4] = {
  {{granary_extable_read_8, granary_extable_read_16,
    granary_extable_read_32, granary_extable_read_64},
   {granary_extable_write_8, granary_extable_write_16,
    granary_extable_write_32, granary_extable_write_64}},
  {{granary_extable_read_error_8, granary_extable_read_error_16,
    granary_extable_read_error_32, granary_extable_read_error_64},
   {granary_extable_write_error_8, granary_extable_write_error_16,
    granary_extable_write_error_32, granary_extable_write_error_64}},
};

// Same order as in `xed_reg_enum_t`.
ExceptionFuncPtr segment_funcs[6] = {
  granary_extable_write_seg_cs, granary_extable_write_seg_ds,
  granary_extable_write_seg_es, granary_extable_write_seg_ss,
  granary_extable_write_seg_fs, granary_extable_write_seg_gs
};
#endif

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
static int OperandSizeOrder(NativeInstruction *instr) {
  return Order(static_cast<int>(instr->instruction.effective_operand_width));
}

#if 0
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
#endif
#if 0
extern "C" {
extern const uint8_t granary_extable_rep_movs_8;
extern const uint8_t granary_extable_rep_movs_16;
extern const uint8_t granary_extable_rep_movs_32;
extern const uint8_t granary_extable_rep_movs_64;
}  // extern C
static AppPC rep_movs_handlers[] = {
  &granary_extable_rep_movs_8, &granary_extable_rep_movs_16,
  &granary_extable_rep_movs_32, &granary_extable_rep_movs_64
};

// Annotate `REP MOVS` instructions that have associated extable entries.
static void AnnotateRepMovs(BlockFactory *factory, NativeInstruction *instr,
                            const ExceptionTableEntry *entry, AppPC next_pc) {
  auto recovers_from_error = RecoveryEntryIsError(entry);
  auto recovery_pc = FindRecoveryAddress(entry, recovers_from_error);
  auto handler = rep_movs_handlers[OperandSizeOrder(instr)];
  auto next_instr = instr->Next();

  GRANARY_ASSERT(instr->instruction.has_prefix_rep);
  GRANARY_ASSERT(!recovers_from_error);

  auto no_fault_label = new LabelInstruction;
  arch::Instruction ni;

  // TODO(pag): How do we deal with things like watchpoints? I think this
  //            type of annotation should go *after* instrumentation. BUT, then
  //            how do we handle things like specializing the case where the
  //            code can raise an exception?

  // TODO(pag): Definitely need to restructure this into some sort of dual
  //            approach. The right way might be to simply have two outgoing
  //            targets of instructions that might fault, OR: having something
  //            more like forcing a JMP to a successor block after this
  //            instruction, then duplicating the meta-data of that block in
  //            the case of an exception. That could be hairy though...

  instr->instruction.dont_encode = true;

  AFTER(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                      arch::BaseDispMemOp(-8, XED_REG_RSP,
                                          arch::GPR_WIDTH_BITS));
         ni.is_stack_blind = true;
         ni.analyzed_stack_usage = false; );

  AFTER(CALL_NEAR_RELBRd(&ni, handler);
         ni.is_stack_blind = true;
         ni.analyzed_stack_usage = false; );

  // The trick here is that the handler will replace [RSP + 8] with 0 (no fault)
  // or RSP from the handler (fault). Anding together the (0/saved RSP) with RSP
  // will get us: 0 iff there is no fault and something non-zero if there is.
  auto test_op = arch::BaseDispMemOp(8, XED_REG_RSP, arch::GPR_WIDTH_BITS);
  AFTER(TEST_MEMv_GPRv(&ni, test_op, XED_REG_RSP);
        ni.is_stack_blind = true;);
  AFTER(LEA_GPRv_AGEN(&ni, XED_REG_RSP, test_op);
        ni.is_stack_blind = true;
        ni.analyzed_stack_usage = false; );

  FLAG_debug_log_fragments = true;

  JNZ_RELBRd(&ni, no_fault_label);
  next_instr->InsertBefore(new BranchInstruction(&ni, no_fault_label));
  next_instr->InsertBefore(lir::Jump(factory, recovery_pc, REQUEST_DENIED));
  next_instr->InsertBefore(no_fault_label);
  next_instr->InsertBefore(lir::Jump(factory, next_pc,
                                     REQUEST_CHECK_INDEX_AND_LCFG));
}
#endif
}  // namespace

// Annotate an application instruction.
//
// For the Linux kernel, what we look for is instructions that might access
// user space memory, and therefore might fault. In these cases, we bring in
// the fixup code as an actual basic block, thus making the exceptional control
// flow explicit.
bool AnnotateAppInstruction(BlockFactory *, DecodedBasicBlock *,
                            NativeInstruction *instr, AppPC) {
  if (IsA<ControlFlowInstruction *>(instr)) return;

  auto fault_pc = instr->DecodedPC();
  auto module = ModuleContainingPC(fault_pc);
  if (!module) return;

  // Get this module's exception tables.
  auto exception_tables = reinterpret_cast<const ExceptionTableBounds *>(
      module->where_data);
  if (!exception_tables) return;

  // Search the exception tables for the page fault recovery address.
  auto recovery_entry = FindRecoveryEntry(exception_tables->start,
                                          exception_tables->stop,
                                          fault_pc);
  if (GRANARY_LIKELY(!recovery_entry)) return;

  instr->instruction.is_sticky = true;

  // TODO: Create an ExceptionRecovery class. It should have the recovery PC,
  //       a bunch of virtual registers representing the machine regs for the
  //       instruction before it can fault (which will be injected for restoring
  //       in the recovery path). Use inline assembly to pre-save the registers
  //       before the instruction.
  //
  // TODO: Create an ExceptionalControlFlowInstruction, that has a
  //       DirectBasicBlock as a target, but have it as REQUEST_DENIED.

  instr->os_annotation = recovery_entry;

  return true;
#if 0
  //auto recovers_from_error = RecoveryEntryIsError(recovery_entry);
  //auto recovery_pc = FindRecoveryAddress(recovery_entry, recovers_from_error);

  switch (instr->instruction.iclass) {
    case XED_ICLASS_MOVSB:
    case XED_ICLASS_MOVSS:
    case XED_ICLASS_MOVSD:
    case XED_ICLASS_MOVSQ:
      return AnnotateRepMovs(factory, instr, recovery_entry, next_pc);
    default:
      return;
      //GRANARY_ASSERT(false);
  }
#endif
#if 0
  auto is_write = false;
  auto mem_size = -1;
  auto remove_instr = false;
  auto load_rcx_with_mloc = true;

  MemoryOperand dest, src;
  ExceptionFuncPtr handler(nullptr);
  arch::Instruction ni;
  arch::Operand mloc;

  // TODO(pag): How do we figure out which memory location is the one that
  //            is (semantically) allowed to fault?
  //
  //            See Issue #19.
  NOP_90(&(instr->instruction));
  instr->InsertAfter(lir::Jump(new NativeBasicBlock(fault_pc)));
  return;

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
    handler = granary_extable_rdmsr;
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

    handler = granary_extable_wrmsr;
    remove_instr = true;
    load_rcx_with_mloc = false;

  // Raise any pending floating-point exceptions.
  } else if (XED_ICLASS_FWAIT == instr->instruction.iclass) {
    handler = granary_extable_fwait;
    remove_instr = true;
    load_rcx_with_mloc = false;

  } else {
    granary_curiosity();  // TODO(pag): This is weird.
    return;
  }

  if (!handler) {
    GRANARY_ASSERT(XED_IFORM_MOV_SEG_MEMw != instr->instruction.iform);
    GRANARY_ASSERT(-1 != mem_size);
    handler = exception_funcs[recovers_from_error][is_write][OperandSizeOrder(mem_size)];
  }

  // Double check that the stack pointer isn't operated on. This is mostly
  // just a sanity check that relates back to mangling concerns, because
  GRANARY_ASSERT(!instr->instruction.ReadsFromStackPointer() &&
                 !instr->instruction.WritesToStackPointer());

  auto saved_rcx = block->AllocateVirtualRegister();

  // High-level structure of the annotations:
  //                  <valid stack>
  //                  MOV saved_rcx, RCX
  //                  LEA RCX, [reg containing user address]
  //                  CALL granary_extable_*  // Return RCX=0 if no fault.
  //                  JRCXZ no_fault
  //        fault:    MOV RCX, saved_rcx
  //                  JMP recovery_pc
  //        no_fault: MOV RCX, saved_rcx
  //                  <instr>
  //                  ...
  //
  // TODO(pag): For REP-based instructions, need to make sure that *none* of
  //            the accessed memory faults. Would need to pre-mangle into a
  //            LOOP-form, then apply the above translation to the interior
  //            instructions.

  // Just assume that the stack is valid, it's easier that way.
  instr->InsertBefore(new AnnotationInstruction(IA_VALID_STACK));
  BEFORE(MOV_GPRv_GPRv_89(&ni, saved_rcx, XED_REG_RCX));
  if (load_rcx_with_mloc) BEFORE(LEA_GPRv_AGEN(&ni, XED_REG_RCX, mloc));
  BEFORE(CALL_NEAR_RELBRd(&ni, handler);
         ni.is_stack_blind = true;
         ni.analyzed_stack_usage = false; );
  auto label_no_fault = new LabelInstruction;
  JRCXZ_RELBRb(&ni, label_no_fault);
  instr->InsertBefore(new BranchInstruction(&ni, label_no_fault));

  BEFORE(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  instr->InsertBefore(lir::Jump(factory, recovery_pc, REQUEST_DENIED));

  instr->InsertBefore(label_no_fault);
  BEFORE(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  // `instr` is here. Need to restore `RCX` before `instr` just in case
  // `RCX` is used by `instr`.
  instr->instruction.is_sticky = true;

  //instr->InsertAfter(lir::Jump(factory, next_pc, REQUEST_CHECK_LCFG));

  // If the `handler` itself emulates the instruction, then we don't want to
  // encode the instruction. However, we don't want to clobber it into a `NOP`
  // either because then if it has some register dependencies, then those will
  // be hidden from the VR system. Therefore, we leave these instructions in
  // but mark them as non-encodable.
  if (remove_instr) instr->instruction.dont_encode = true;
#endif
}

}  // namespace os
}  // namespace granary

#endif  // GRANARY_WHERE_kernel
