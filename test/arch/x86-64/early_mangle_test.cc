/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "test/isolated_function.h"
#include "test/simple_encoder.h"

extern "C" {
extern uint64_t PushMem_GPR(uint64_t *addr);
extern uint64_t PushMem_GPR_GPR(uint64_t *base, uint64_t offset);
extern uint64_t PushMem_RIP(void);
extern uint64_t PushMem_STACK_DOWN(void);
extern uint64_t PushMem_STACK_TOP(void);
extern uint64_t PushMem_STACK_UP(void);
extern uint64_t PushImmWord(void);
extern uint64_t PushImmQuadWord(void);
extern uint64_t PushRSP(void);

extern void PushPopRSP(void);
extern void PopRSP(void);
extern void PopMem_RSP_TOP(void);
extern void PopMem_RSP_UP(void);
extern void PopMem_RSP_DOWN(void);
extern void PopMem_GPR(uint64_t *);
extern void PopMem_GPR_GPR(uint64_t *base, uint64_t offset);
extern void PushPopGS(void);
extern void PushwPopwGS(void);

extern void SwapStacks_MOV(void);
extern void SwapStacks_XCHG_SELF(void);
extern void SwapStacks_XCHG_OTHER(void);

extern uint64_t AccesTLSBase_Direct(void);
extern uint64_t AccesTLSBase_Indirect(void);
extern uint64_t AccesTLSBase_Indirect32(void);
extern uint64_t AccesTLSBase_Indirect64(void);

}  // extern C

namespace {
static uint64_t DEADBEEF = 0xDEADBEEFULL;
}  // namespace

#define TEST_F_ASM(name, func, ...) \
  TEST_F(SimpleEncoderTest, name) { \
    std::function<void(IsolatedRegState *)> setup( \
        [=] (IsolatedRegState *regs) { \
          GRANARY_UNUSED(regs); \
          __VA_ARGS__ \
        }); \
    RunIsolatedFunction( \
        setup, \
        reinterpret_cast<void *>(func), \
        reinterpret_cast<void *>(InstrumentAndEncode(func))); \
  }

TEST_F_ASM(PushMem_GPR, PushMem_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_F_ASM(PushMem_GPR_GPR, PushMem_GPR_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&((&DEADBEEF)[-1]));
           regs->ARG2 = 8; )

TEST_F_ASM(PushMem_RIP, PushMem_RIP)

TEST_F_ASM(PushMem_STACK_DOWN, PushMem_STACK_DOWN,
           regs->ARG1 = DEADBEEF; )

TEST_F_ASM(PushMem_STACK_TOP, PushMem_STACK_TOP,
           regs->ARG1 = DEADBEEF; )

TEST_F_ASM(PushMem_STACK_UP, PushMem_STACK_UP,
           regs->ARG1 = DEADBEEF; )

TEST_F_ASM(PushImmWord, PushImmWord)

TEST_F_ASM(PushImmQuadWord, PushImmQuadWord)

TEST_F_ASM(PushRSP, PushRSP)

TEST_F_ASM(PushPopRSP, PushPopRSP)
TEST_F_ASM(PopRSP, PopRSP)
TEST_F_ASM(PopMem_RSP_TOP, PopMem_RSP_TOP)
TEST_F_ASM(PopMem_RSP_UP, PopMem_RSP_UP)
TEST_F_ASM(PopMem_RSP_DOWN, PopMem_RSP_DOWN)

TEST_F_ASM(PopMem_GPR, PopMem_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_F_ASM(PopMem_GPR_GPR, PopMem_GPR_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&((&DEADBEEF)[-1]));
           regs->ARG2 = 8; )

TEST_F_ASM(PushPopGS, PushPopGS)
TEST_F_ASM(PushwPopwGS, PushwPopwGS)

TEST_F_ASM(SwapStacks_MOV, SwapStacks_MOV)
TEST_F_ASM(SwapStacks_XCHG_SELF, SwapStacks_XCHG_SELF)
TEST_F_ASM(SwapStacks_XCHG_OTHER, SwapStacks_XCHG_OTHER)

TEST_F_ASM(AccesTLSBase_Direct, AccesTLSBase_Direct)
TEST_F_ASM(AccesTLSBase_Indirect, AccesTLSBase_Indirect)
TEST_F_ASM(AccesTLSBase_Indirect32, AccesTLSBase_Indirect32)
TEST_F_ASM(AccesTLSBase_Indirect64, AccesTLSBase_Indirect64)

