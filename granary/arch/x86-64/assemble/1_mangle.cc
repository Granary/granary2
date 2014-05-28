/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"
#include "granary/base/new.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/xed.h"

#include "granary/breakpoint.h"

namespace granary {

// Represents an allocated address that is nearby the code cache and can be used
// to indirectly resolve the problem of PC-relative targets being too far away.
//
// TODO(pag): Need a mechanism of garbage collecting these on cache flushes.
union NativeAddress {
 public:
  NativeAddress(PC pc_)
      : pc(pc_) {}

  const void *addr;
  PC pc;

  GRANARY_DEFINE_NEW_ALLOCATOR(NativeAddress, {
    SHARED = true,
    ALIGNMENT = 8
  })

 private:
  NativeAddress(void) = delete;
} __attribute__((packed));

static_assert(sizeof(NativeAddress) == sizeof(void *),
    "Invalid packing of `union NativeAddress`. Must be pointer-sized.");

namespace {

// Instruction iclass reversers for conditional branches, indexed by
// `instr->iclass - XED_ICLASS_JB`.
const xed_iclass_enum_t kReversedConditionalCFIs[] = {
  XED_ICLASS_JNB,
  XED_ICLASS_JNBE,
  XED_ICLASS_JNL,
  XED_ICLASS_JNLE,
  XED_ICLASS_INVALID,
  XED_ICLASS_INVALID,
  XED_ICLASS_JB,
  XED_ICLASS_JBE,
  XED_ICLASS_JL,
  XED_ICLASS_JLE,
  XED_ICLASS_JO,
  XED_ICLASS_JP,
  XED_ICLASS_JS,
  XED_ICLASS_JZ,
  XED_ICLASS_JNO,
  XED_ICLASS_JNP,
  XED_ICLASS_INVALID,
  XED_ICLASS_JNS,
  XED_ICLASS_JNZ,
};

// Instruction builders for conditional branches, indexed by
// `instr->iclass - XED_ICLASS_JB`.
typedef void (CFIBuilder)(arch::Instruction *, PC);
CFIBuilder * const kConditionalCFIBuilders[] = {
  arch::JB_RELBRd<PC>,
  arch::JBE_RELBRd<PC>,
  arch::JL_RELBRd<PC>,
  arch::JLE_RELBRd<PC>,
  nullptr,
  nullptr,
  arch::JNB_RELBRd<PC>,
  arch::JNBE_RELBRd<PC>,
  arch::JNL_RELBRd<PC>,
  arch::JNLE_RELBRd<PC>,
  arch::JNO_RELBRd<PC>,
  arch::JNP_RELBRd<PC>,
  arch::JNS_RELBRd<PC>,
  arch::JNZ_RELBRd<PC>,
  arch::JO_RELBRd<PC>,
  arch::JP_RELBRd<PC>,
  nullptr,
  arch::JS_RELBRd<PC>,
  arch::JZ_RELBRd<PC>
};

// Relativize a conditional branch by turning it into an indirect jump through
// a `NativeAddress`, then add instructions around the new indirect jump to
// jump around the indirect jump when the original condition is not satisfied.
static void RelativizeConditionalBranch(ControlFlowInstruction *cfi,
                                        arch::Instruction *instr, PC target_pc) {
  auto iclass = kReversedConditionalCFIs[instr->iclass - XED_ICLASS_JB];
  auto iclass_builder = kConditionalCFIBuilders[iclass - XED_ICLASS_JB];

  arch::Instruction neg_bri;
  iclass_builder(&neg_bri, static_cast<PC>(nullptr));

  auto label = new LabelInstruction;
  auto neg_br = new BranchInstruction(&neg_bri, label);

  instr->iclass = XED_ICLASS_JMP;
  instr->category = XED_CATEGORY_UNCOND_BR;

  // Have a negated conditional branch jump around the old conditional branch.
  cfi->UnsafeInsertBefore(neg_br);
  cfi->UnsafeInsertAfter(label);

  // Overwrite the conditional branch with an indirect JMP.
  JMP_MEMv(instr, new NativeAddress(target_pc));
}

// Relativize a loop instruction. This turns an instruction like `jecxz <foo>`
// or `loop <foo>` into:
//                    jmp   <try_loop>
//        do_loop:    jmp   <foo>
//        try_loop:   loop  <do_loop>
static void RelativizeLoop(ControlFlowInstruction *cfi,
                           arch::Instruction *instr,
                           PC target_pc, bool target_is_far_away) {
  arch::Instruction jmp_try_loop;
  arch::Instruction loop_do_loop;

  memcpy(&loop_do_loop, instr, sizeof loop_do_loop);
  loop_do_loop.SetBranchTarget(nullptr);

  arch::JMP_RELBRz<PC>(&jmp_try_loop, nullptr);
  if (target_is_far_away) {
    arch::JMP_MEMv(instr, new NativeAddress(target_pc));
  } else {
    arch::JMP_RELBRd<PC>(instr, target_pc);
  }

  auto do_loop = new LabelInstruction;
  auto try_loop = new LabelInstruction;

  do_loop->UnsafeInsertBefore(new BranchInstruction(&jmp_try_loop, try_loop));
  cfi->UnsafeInsertBefore(do_loop);
  cfi->UnsafeInsertAfter(try_loop);
  try_loop->UnsafeInsertAfter(new BranchInstruction(&loop_do_loop, do_loop));
}

}  // namespace

// Relativize a direct control-flow instruction.
void RelativizeDirectCFI(ControlFlowInstruction *cfi, arch::Instruction *instr,
                         PC target_pc, bool target_is_far_away) {
  auto iclass = instr->iclass;
  if (XED_ICLASS_CALL_NEAR == iclass) {
    if (target_is_far_away) {
      arch::CALL_NEAR_MEMv(instr, new NativeAddress(target_pc));
    }
  } else if (XED_ICLASS_JMP == iclass) {
    if (target_is_far_away) {
      arch::JMP_MEMv(instr, new NativeAddress(target_pc));
    }

  // Always need to mangle this.
  } else if (XED_ICLASS_JRCXZ == iclass ||
             (XED_ICLASS_LOOP <= iclass && XED_ICLASS_LOOPNE >= iclass)) {
    RelativizeLoop(cfi, instr, target_pc, target_is_far_away);

  // Conditional jumps. We translate these by converting them into a negated
  // conditional jump around an indirect jump to the far-away instruction.
  } else if (instr->IsConditionalJump()) {
    if (target_is_far_away) {
      RelativizeConditionalBranch(cfi, instr, target_pc);
    }
  } else {
    GRANARY_ASSERT(false);
  }
}

// Performs mangling of an indirect CFI instruction.
void MangleIndirectCFI(DecodedBasicBlock *block, ControlFlowInstruction *cfi) {
  if (!cfi->IsFunctionCall()) return;
  auto ret_address = new AnnotationInstruction(IA_RETURN_ADDRESS);
  arch::Instruction instr;
  arch::Operand op;
  auto ret_address_reg = block->AllocateVirtualRegister();
  auto decoded_pc = cfi->instruction.decoded_pc;
  op.type = XED_ENCODER_OPERAND_TYPE_PTR;
  op.is_effective_address = true;
  op.is_annot_encoded_pc = true;
  op.ret_address = ret_address;
  arch::LEA_GPRv_AGEN(&instr, ret_address_reg, op);
  cfi->UnsafeInsertBefore(new NativeInstruction(&instr));
  arch::PUSH_GPRv_50(&instr, ret_address_reg);
  instr.decoded_pc = decoded_pc;  // Mark as application.
  instr.AnalyzeStackUsage();
  cfi->UnsafeInsertBefore(new NativeInstruction(&instr));
  cfi->UnsafeInsertAfter(ret_address);

  // Note: The final mangling of indirect calls and indirect jumps happens in
  //       `9_allocate_slots.cc` in the function `RemoveIndirectCallsAndJumps`.
}

// Relativize a instruction with a memory operand, where the operand loads some
// value from `mem_addr`
void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *ninstr,
                     const MemoryOperand &mloc, const void *mem_addr) {
  auto op = mloc.UnsafeExtract();
  if (XED_ENCODER_OPERAND_TYPE_PTR != op->type) return;

  // 32-bit absolute address (seg=DS), RIP-relative address that was converted
  // into 32-bit absolute (seg=DS), or segment-offsetted address (seg=GS/FS).
  if (XED_REG_INVALID != op->segment) return;

  arch::Instruction ni;
  auto addr_reg = block->AllocateVirtualRegister(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_GPRv_IMMv(&ni, addr_reg, reinterpret_cast<uintptr_t>(mem_addr));
  ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS;
  ninstr->UnsafeInsertBefore(new NativeInstruction(&ni));

  GRANARY_ASSERT(!op->is_sticky && op->is_explicit && !op->is_compound);
  op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  op->reg = addr_reg;
}

}  // namespace granary
