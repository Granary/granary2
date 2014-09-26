/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/base/string.h"

#include "granary/code/fragment.h"

#include "granary/cache.h"

namespace granary {
namespace arch {

#ifndef GRANARY_RECURSIVE
extern "C" {

// The entrypoint to the trace log. This is an assemble routine that records
// the register state in the form of a `struct RegisterState`, and then passes
// it off to `granary_trace_block_regs`.
extern void granary_trace_block(void);

struct RegisterState {
  uint64_t thread;  // Zero is initially pushed.
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
  uint64_t rsp;
  uint64_t rip;  // Return address; implicitly pushed via `CALL`.
};

enum {
  GRANARY_BLOCK_LOG_LENGTH = 4096,
  LEA_GPRv_AGEN_LEN = 5,
  CALL_RELBRd_LEN = 5
};

GRANARY_IF_USER( static __thread uint64_t thread_id = 0; )
GRANARY_IF_USER( static std::atomic<uint64_t> num_threads(ATOMIC_VAR_INIT(1)); )

void InitThreadId(RegisterState *regs) {
#ifdef GRANARY_WHERE_user
  if (!thread_id) thread_id = num_threads.fetch_add(1);
  regs->thread = thread_id;
#else
  regs->thread = regs->rsp / (2 * PAGE_SIZE_BYTES);
#endif
}

// The recorded entries in the trace. This is a global variable so that GDB
// can see it.
RegisterState granary_block_log[GRANARY_BLOCK_LOG_LENGTH];

// The index into Granary's trace log. Also a global variable so that GDB can
// easily see it.
alignas(CACHE_LINE_SIZE_BYTES) unsigned granary_block_log_index = 0;

// Record an entry in Granary's trace log.
void granary_trace_block_regs(const RegisterState *regs) {
  auto index = __sync_fetch_and_add(&granary_block_log_index, 1U);
  auto &log_regs(granary_block_log[index % GRANARY_BLOCK_LOG_LENGTH]);

  memcpy(&log_regs, regs, sizeof *regs);

  // Adjust the logged stack pointer to account for the return address and the
  // potential user-space redzone. Also adjust the instruction pointer pointer
  // to adjust for the size of the call and potential stack shifting
  // instructions.
  log_regs.rsp += ADDRESS_WIDTH_BYTES + REDZONE_SIZE_BYTES;
  log_regs.rip -= CALL_RELBRd_LEN GRANARY_IF_USER( + LEA_GPRv_AGEN_LEN);

  InitThreadId(&log_regs);
}

}  // extern C

#define PREP(...) \
  do { \
    __VA_ARGS__ ; \
    frag->instrs.Prepend(new NativeInstruction(&ni)); \
  } while (0)

// Adds in some extra "tracing" instructions to the beginning of a basic block.
void AddBlockTracer(Fragment *frag, BlockMetaData *meta,
                    CachePC estimated_encode_pc) {
  Instruction ni;

  // Note: Instructions prepended in reverse to get desired effect.

  if (REDZONE_SIZE_BYTES) PREP(UNSHIFT_REDZONE(&ni));
  auto target_pc = UnsafeCast<PC>(granary_trace_block);
  if (!AddrIsOffsetReachable(target_pc, estimated_encode_pc)) {
    auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
    auto addr = new NativeAddress(target_pc, &(cache_meta->native_addresses));
    PREP(CALL_NEAR_MEMv(&ni, addr));
  } else {
    PREP(CALL_NEAR_RELBRd(&ni, target_pc));
  }
  if (REDZONE_SIZE_BYTES) PREP(SHIFT_REDZONE(&ni));
}

#else

// Adds in some extra "tracing" instructions to the beginning of a basic block.
void AddBlockTracer(Fragment *, BlockMetaData *, CachePC) {}

#endif  // GRANARY_RECURSIVE

}  // namespace arch
}  // namespace granary
