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
extern uint64_t PushImm(void);
}  // extern C

namespace {
static uint64_t DEADBEEF = 0xDEADBEEFULL;
}  // namespace

TEST_F(SimpleEncoderTest, PushMem_GPR) {
  std::function<void(IsolatedRegState *)> setup([=] (IsolatedRegState *regs) {
    using namespace granary;
    regs->ARG1 = reinterpret_cast<uint64_t>(&DEADBEEF);
  });
  RunIsolatedFunction(
      setup,
      reinterpret_cast<void *>(PushMem_GPR),
      reinterpret_cast<void *>(InstrumentAndEncode(PushMem_GPR)));
}

TEST_F(SimpleEncoderTest, PushMem_GPR_GPR) {

}

TEST_F(SimpleEncoderTest, PushMem_RIP) {

}

TEST_F(SimpleEncoderTest, PushMem_STACK_DOWN) {

}

TEST_F(SimpleEncoderTest, PushMem_STACK_TOP) {

}

TEST_F(SimpleEncoderTest, PushMem_STACK_UP) {

}

TEST_F(SimpleEncoderTest, PushImm) {

}

TEST_F(SimpleEncoderTest, PushRSP) {

}
