/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/base/string.h"

#include "granary/code/fragment.h"

#include "granary/cache.h"

extern "C" {

// The entrypoint to the trace log. This is an assemble routine that records
// the register state in the form of a `struct RegisterState`, and then passes
// it off to `granary_trace_block_regs`.
extern void granary_trace_block(void);

struct RegisterState {
  uint64_t rflags;  // Last to be pushed.
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rax;
  uint64_t rip;  // Return address.
};

enum {
  GRANARY_BLOCK_LOG_LENGTH = 1024
};

// The recorded entries in the trace. This is a global variable so that GDB
// can see it.
RegisterState granary_block_log[GRANARY_BLOCK_LOG_LENGTH];

// The index into Granary's trace log. Also a global variable so that GDB can
// easily see it.
unsigned granary_block_log_index = 0;

// Record an entry in Granary's trace log.
void granary_trace_block_regs(const RegisterState *regs) {
  auto index = __sync_add_and_fetch(&granary_block_log_index, 1U);
  auto &log_regs(granary_block_log[index % GRANARY_BLOCK_LOG_LENGTH]);
  memcpy(&log_regs, regs, sizeof *regs);
}

}  // extern C

#define PREP(...) \
  do { \
    __VA_ARGS__ ; \
    frag->instrs.Prepend(new NativeInstruction(&ni)); \
  } while (0)

namespace granary {
namespace arch {

// Adds in some extra "tracing" instructions to the beginning of a basic block.
void AddBlockTracer(Fragment *frag, BlockMetaData *meta,
                    CachePC estimated_encode_pc) {
  Instruction ni;

  if (REDZONE_SIZE_BYTES) {
    PREP(LEA_GPRv_AGEN(&ni, XED_REG_RSP, BaseDispMemOp(REDZONE_SIZE_BYTES,
                                                       XED_REG_RSP,
                                                       ADDRESS_WIDTH_BITS)));
  }

  auto target_addr = UnsafeCast<uintptr_t>(granary_trace_block);
  auto encode_addr = reinterpret_cast<uintptr_t>(estimated_encode_pc);
  auto target_pc = reinterpret_cast<PC>(target_addr);
  auto diff = std::max(encode_addr, target_addr) -
              std::min(encode_addr, target_addr);

  // TODO(pag): Generalize the following pattern, as in the first Granary.
  if (diff > 4187593113UL) {  // > ~3.9GB away; don't risk it for a rel32.
    auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
    auto addr = new NativeAddress(target_pc, &(cache_meta->native_addresses));
    PREP(CALL_NEAR_MEMv(&ni, addr);
         ni.is_stack_blind = true;
         ni.analyzed_stack_usage = false; );
  } else {
    PREP(CALL_NEAR_RELBRd(&ni, target_pc);
         ni.is_stack_blind = true;
         ni.analyzed_stack_usage = false; );
  }
  if (REDZONE_SIZE_BYTES) {
    PREP(LEA_GPRv_AGEN(&ni, XED_REG_RSP, BaseDispMemOp(-REDZONE_SIZE_BYTES,
                                                       XED_REG_RSP,
                                                       ADDRESS_WIDTH_BITS)));
  }
}

}  // namespace arch
}  // namespace granary
