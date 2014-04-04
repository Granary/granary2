/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"
#include "granary/base/new.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/arch/relativize.h"
#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/xed.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

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
typedef void (CFIBuilder)(Instruction *, PC);
CFIBuilder * const kConditionalCFIBuilders[] = {
  JB_RELBRd<PC>,
  JBE_RELBRd<PC>,
  JL_RELBRd<PC>,
  JLE_RELBRd<PC>,
  nullptr,
  nullptr,
  JNB_RELBRd<PC>,
  JNBE_RELBRd<PC>,
  JNL_RELBRd<PC>,
  JNLE_RELBRd<PC>,
  JNO_RELBRd<PC>,
  JNP_RELBRd<PC>,
  JNS_RELBRd<PC>,
  JNZ_RELBRd<PC>,
  JO_RELBRd<PC>,
  JP_RELBRd<PC>,
  nullptr,
  JS_RELBRd<PC>,
  JZ_RELBRd<PC>
};

// Relativize a conditional branch by turning it into an indirect jump through
// a `NativeAddress`, then add instructions around the new indirect jump to
// jump around the indirect jump when the original condition is not satisfied.
static void RelativizeConditionalBranch(ControlFlowInstruction *cfi,
                                        Instruction *instr, PC target_pc) {
  auto iclass = kReversedConditionalCFIs[instr->iclass - XED_ICLASS_JB];
  auto iclass_builder = kConditionalCFIBuilders[iclass - XED_ICLASS_JB];

  Instruction neg_bri;
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
static void RelativizeLoop(ControlFlowInstruction *cfi, Instruction *instr,
                           PC target_pc, bool target_is_far_away) {
  Instruction jmp_try_loop;
  Instruction loop_do_loop;

  memcpy(&loop_do_loop, instr, sizeof loop_do_loop);
  loop_do_loop.SetBranchTarget(nullptr);

  JMP_RELBRz<PC>(&jmp_try_loop, nullptr);

  if (target_is_far_away) {
    JMP_MEMv(instr, new NativeAddress(target_pc));
  } else {
    JMP_RELBRd<PC>(instr, target_pc);
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
void RelativizeDirectCFI(ControlFlowInstruction *cfi, Instruction *instr,
                         PC target_pc, bool target_is_far_away) {
  auto iclass = instr->iclass;
  if (XED_ICLASS_CALL_NEAR == iclass) {
    if (target_is_far_away) CALL_NEAR_MEMv(instr, new NativeAddress(target_pc));

  } else if (XED_ICLASS_JMP == iclass) {
    if (target_is_far_away) JMP_MEMv(instr, new NativeAddress(target_pc));

  // Always need to mangle this.
  } else if (XED_ICLASS_JRCXZ == iclass ||
             (XED_ICLASS_LOOP <= iclass && XED_ICLASS_LOOPNE >= iclass)) {
    RelativizeLoop(cfi, instr, target_pc, target_is_far_away);

  // Conditional jumps. We translate these by converting them into a negated
  // conditional jump around an indirect jump to the far-away instruction.
  } else if (instr->IsConditionalJump()) {
    if (target_is_far_away) RelativizeConditionalBranch(cfi, instr, target_pc);

  } else {
    GRANARY_ASSERT(false);
  }
}

// Relativize a instruction with a memory operand, where the operand loads some
// value from `mem_addr`
void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *ninstr,
                     const MemoryOperand &mloc, const void *mem_addr) {
  Instruction lea;
  auto addr_reg = block->AllocateVirtualRegister(arch::ADDRESS_WIDTH_BYTES);
  LEA_GPRv_IMMv(&lea, addr_reg, reinterpret_cast<uintptr_t>(mem_addr));
  ninstr->UnsafeInsertBefore(new NativeInstruction(&lea));

  MemoryOperand rel_mloc(addr_reg, mloc.Width());
  GRANARY_IF_DEBUG(auto replaced = ) mloc.Ref().ReplaceWith(rel_mloc);
  GRANARY_ASSERT(replaced);
}

}  // namespace arch
}  // namespace granary
