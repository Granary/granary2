/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/driver.h"
#include "arch/x86-64/slot.h"

#include "granary/base/base.h"

#include "granary/cache.h"
#include "granary/context.h"

// After `cache.h` to get `NativeAddress`.
#include "arch/x86-64/builder.h"

#define ENC(...) \
  do { \
    __VA_ARGS__ ; \
    GRANARY_IF_DEBUG( auto ret = ) stage_enc.Encode(&ni, pc); \
    GRANARY_ASSERT(ret); \
    GRANARY_IF_DEBUG( ret = ) commit_enc.EncodeNext(&ni, &pc); \
    GRANARY_ASSERT(ret); \
  } while (0)

namespace granary {
namespace arch {

// Generates code that disables interrupts.
void GenerateInterruptDisableCode(Context *, CachePC pc) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // Save flags.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Disable interrupts.
  ENC(CLI(&ni));

  // Copy saved flags (before disabling interrupts) into the CPU-private
  // spill slot.
  ENC(POP_MEMv(&ni, SlotMemOp(os::SLOT_SAVED_FLAGS, 0, GPR_WIDTH_BITS)));

  // Return back into the code cache.
  ENC(RET_NEAR(&ni); ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS; );

  GRANARY_ASSERT(arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES >= (pc - start_pc));
}

#ifdef GRANARY_TARGET_debug
extern "C" {

// Function with a GDB breakpoint that helps warn about interrupts being
// accidentally enabled.
extern const unsigned char granary_interrupts_enabled;

}  // extern C
namespace {

// TODO(pag): Potential leak.
static NativeAddress *interrupts_enabled_addr = nullptr;

}  // namespace
#endif  // GRANARY_TARGET_debug

// Generates code that re-enables interrupts (if they were disabled by the
// interrupt disabling routine).
void GenerateInterruptEnableCode(Context *, CachePC pc) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // Spill the flags. This represents the "native" flag state, with the
  // exception that interrupts might have been abnormally disabled. We need to
  // decide if we should re-enable them.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

#ifdef GRANARY_TARGET_debug
  // Test to see if interrupts were erroneosly re-enabled.
  ENC(BT_MEMv_IMMb(&ni, BaseDispMemOp(0, XED_REG_RSP, GPR_WIDTH_BITS),
                        static_cast<uint8_t>(9)));

  // JNB_RELBRd (6) + CALL_REBRd (5)
  if (AddrIsOffsetReachable(pc, &granary_interrupts_enabled)) {
    ENC(JNB_RELBRd(&ni, pc + 6 + 5));
    ENC(CALL_NEAR_RELBRd(&ni, &granary_interrupts_enabled));

  // JNB_RELBRd (6) + CALL_MEMv (7)
  } else {
    ENC(JNB_RELBRd(&ni, pc + 6 + 7));
    ENC(CALL_NEAR_GLOBAL(&ni, pc, &granary_interrupts_enabled,
                                  &interrupts_enabled_addr));
  }

#endif  // GRANARY_TARGET_debug

  // Test to see if we should re-enable interrupts.
  ENC(BT_MEMv_IMMb(&ni, SlotMemOp(os::SLOT_SAVED_FLAGS, 0, GPR_WIDTH_BITS),
                        static_cast<uint8_t>(9)));

  // If the interrupt flag was `0` in the spilled flags, then we jump to
  // `pc + 14`, which is the `POPFQ` below, so that we restore the saved flags.
  // The idea being: in the disable interrupt code, we might have double-
  // disabled the interrupts, so skip around the code that would re-enable
  // interrupts in the saved flags on the stack.
  ENC(JNB_RELBRd(&ni, pc + 8 + 6));  // JNB rel8 = 6 bytes, OR m64, imm32 = 8.

  // Re-enable interrupts by changing the flags that were `PUSHFQ`d onto the
  // stack.
  ENC(OR_MEMv_IMMz(&ni, BaseDispMemOp(0, XED_REG_RSP, GPR_WIDTH_BITS),
                        1U << 9U));

  // Restore the flags. This *might* re-enable interrupts.
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Return back into the code cache.
  ENC(RET_NEAR(&ni); ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS; );

  GRANARY_ASSERT(arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES >= (pc - start_pc));
}

}  // namespace arch
}  // namespace granary
