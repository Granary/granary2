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

