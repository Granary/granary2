/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "test/isolated_function.h"
#include "test/simple_encoder.h"

namespace {
static uint64_t DEADBEEF = 0xDEADBEEFULL;
}  // namespace

#define TEST_F_ASM(name, ...) \
  extern "C" { \
    extern void TestEarlyMangle_ ## name (void); \
  } \
  TEST_F(SimpleEncoderTest, name) { \
    std::function<void(IsolatedRegState *)> setup( \
        [=] (IsolatedRegState *regs) { \
          GRANARY_UNUSED(regs); \
          __VA_ARGS__ \
        }); \
    RunIsolatedFunction( \
        setup, \
        reinterpret_cast<void *>(TestEarlyMangle_ ## name), \
        reinterpret_cast<void *>( \
            InstrumentAndEncode(TestEarlyMangle_ ## name))); \
  }

TEST_F_ASM(PushMem_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_F_ASM(PushMem_GPR_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&((&DEADBEEF)[-1]));
           regs->ARG2 = 8; )

TEST_F_ASM(PushMem_RIP)

TEST_F_ASM(PushMem_STACK_DOWN,
           regs->ARG1 = DEADBEEF; )

TEST_F_ASM(PushMem_STACK_TOP,
           regs->ARG1 = DEADBEEF; )

TEST_F_ASM(PushMem_STACK_UP,
           regs->ARG1 = DEADBEEF; )

TEST_F_ASM(PushImmWord)

TEST_F_ASM(PushImmQuadWord)

TEST_F_ASM(PushRSP)

TEST_F_ASM(PushPopRSP)
TEST_F_ASM(PopRSP)
TEST_F_ASM(PopMem_RSP_TOP)
TEST_F_ASM(PopMem_RSP_UP)
TEST_F_ASM(PopMem_RSP_DOWN)

TEST_F_ASM(PopMem_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_F_ASM(PopMem_GPR_GPR,
           regs->ARG1 = reinterpret_cast<uint64_t>(&((&DEADBEEF)[-1]));
           regs->ARG2 = 8; )

TEST_F_ASM(PushPopGS)
TEST_F_ASM(PushwPopwGS)

TEST_F_ASM(SwapStacks_MOV)
TEST_F_ASM(SwapStacks_XCHG_SELF)
TEST_F_ASM(SwapStacks_XCHG_OTHER)

TEST_F_ASM(AccesTLSBase_Direct)
TEST_F_ASM(AccesTLSBase_Indirect)
TEST_F_ASM(AccesTLSBase_Indirect32)
TEST_F_ASM(AccesTLSBase_Indirect64)

