/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "test/util/isolated_function.h"
#include "test/util/simple_encoder.h"

namespace {
static uint64_t DEADBEEF = 0xDEADBEEFULL;
static uint64_t DEADBEEFS[16] = {
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL,
  0xDEADBEEFULL
};
}  // namespace

class EarlyMangleTest : public SimpleEncoderTest {};

#define TEST_EARLY_MANGLE(name, ...) \
  extern "C" { \
    extern void TestEarlyMangle_ ## name (void); \
  } \
  TEST_F(EarlyMangleTest, name) { \
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

TEST_EARLY_MANGLE(PushMem_GPR,
    regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_EARLY_MANGLE(PushMem_GPR_GPR,
    regs->ARG1 = reinterpret_cast<uint64_t>(&((&DEADBEEF)[-1]));
    regs->ARG2 = 8; )

TEST_EARLY_MANGLE(PushMem_RIP)

TEST_EARLY_MANGLE(PushMem_STACK_DOWN,
    regs->ARG1 = DEADBEEF; )

TEST_EARLY_MANGLE(PushMem_STACK_TOP,
    regs->ARG1 = DEADBEEF; )

TEST_EARLY_MANGLE(PushMem_STACK_UP,
    regs->ARG1 = DEADBEEF; )

TEST_EARLY_MANGLE(PushImmWord)

TEST_EARLY_MANGLE(PushImmQuadWord)

TEST_EARLY_MANGLE(PushRSP)

TEST_EARLY_MANGLE(PushPopRSP)
TEST_EARLY_MANGLE(PopRSP)
TEST_EARLY_MANGLE(PopMem_RSP_TOP)
TEST_EARLY_MANGLE(PopMem_RSP_UP)
TEST_EARLY_MANGLE(PopMem_RSP_DOWN)

TEST_EARLY_MANGLE(PopMem_GPR,
    regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_EARLY_MANGLE(PopMem_GPR_GPR,
    regs->ARG1 = reinterpret_cast<uint64_t>(&((&DEADBEEF)[-1]));
    regs->ARG2 = 8; )

TEST_EARLY_MANGLE(PushPopGS)
TEST_EARLY_MANGLE(PushwPopwGS)

TEST_EARLY_MANGLE(SwapStacks_MOV)
TEST_EARLY_MANGLE(SwapStacks_XCHG_SELF)
TEST_EARLY_MANGLE(SwapStacks_XCHG_OTHER)

TEST_EARLY_MANGLE(AccesTLSBase_Direct)
TEST_EARLY_MANGLE(AccesTLSBase_Indirect)
TEST_EARLY_MANGLE(AccesTLSBase_Indirect32)
TEST_EARLY_MANGLE(AccesTLSBase_Indirect64)

TEST_EARLY_MANGLE(XLAT,
    regs->RBX = reinterpret_cast<uint64_t>(&DEADBEEF);
    regs->RAX = 1; )

TEST_EARLY_MANGLE(ENTER_0,
    regs->RBP = reinterpret_cast<uint64_t>(&DEADBEEF); )
TEST_EARLY_MANGLE(ENTER_1,
    regs->RBP = reinterpret_cast<uint64_t>(&DEADBEEF); )
TEST_EARLY_MANGLE(ENTER_16,
    regs->RBP = reinterpret_cast<uint64_t>(&DEADBEEFS[16]); )

TEST_EARLY_MANGLE(ENTER_0_LEAVE,
    regs->RBP = reinterpret_cast<uint64_t>(&DEADBEEF); )
TEST_EARLY_MANGLE(ENTER_1_LEAVE,
    regs->RBP = reinterpret_cast<uint64_t>(&DEADBEEF); )
TEST_EARLY_MANGLE(ENTER_16_LEAVE,
    regs->RBP = reinterpret_cast<uint64_t>(&DEADBEEFS[16]); )

TEST_EARLY_MANGLE(PUSHFW)
TEST_EARLY_MANGLE(PUSHFQ)

TEST_EARLY_MANGLE(PREFETCH,
    regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF); )

TEST_EARLY_MANGLE(XMM_SAVE_RESTORE,
    regs->ARG1 = reinterpret_cast<uint64_t>(&(DEADBEEFS[0])); )
