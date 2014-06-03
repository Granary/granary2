/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "test/isolated_function.h"

extern "C" {
  extern void RunFunctionInContext(void *func, IsolatedRegState *inout);
}

// Runs a function and an instrumented function in an "isolated" context
// (almost full machine state), and then
void RunIsolatedFunction(std::function<void(IsolatedRegState *)> &setup_state,
                         void *func,
                         void *instrumented_func) {
  IsolatedRegState regs1, regs2, regs3;

  memset(&regs1, 0, sizeof regs1);
  setup_state(&regs1);
  regs1.RSP = reinterpret_cast<uintptr_t>(&(regs1.stack));
  RunFunctionInContext(reinterpret_cast<void *>(func), &regs1);
  memcpy(&regs2, &regs1, sizeof regs1);

  // Make the initial values of everything on the stack different so that we
  // can eventually distinguish what changes and what stays the same.
  memset(&regs1, 0, sizeof regs1);
  memset(&(regs1.stack), 0xAB, sizeof regs1.stack);
  setup_state(&regs1);
  regs1.RSP = reinterpret_cast<uintptr_t>(&(regs1.stack));
  RunFunctionInContext(reinterpret_cast<void *>(func), &regs1);
  memcpy(&regs3, &regs1, sizeof regs1);

  memset(&regs1, 0, sizeof regs1);
  setup_state(&regs1);
  regs1.RSP = reinterpret_cast<uintptr_t>(&(regs1.stack));
  RunFunctionInContext(reinterpret_cast<void *>(instrumented_func), &regs1);

  // Compare bytes that are the same across the two native runs. This ensures
  // that stuff in regs3 that falls outside of the redzone is not part of the
  // comparison.
  auto regs1_bytes = reinterpret_cast<uint8_t *>(&regs1);
  auto regs2_bytes = reinterpret_cast<uint8_t *>(&regs2);
  auto regs3_bytes = reinterpret_cast<uint8_t *>(&regs3);
  for (auto i = 0ULL; i < sizeof regs1; ++i) {
    if (regs2_bytes[i] == regs3_bytes[i]) {
      EXPECT_EQ(regs1_bytes[i], regs2_bytes[i]);
    }
  }
}

