/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_ISOLATED_FUNCTION_H_
#define TEST_ISOLATED_FUNCTION_H_

#include "granary/base/base.h"

struct IsolatedRegState {
  struct {
    uint64_t RSP;
    union {
      uint64_t RAX;
      uint64_t RETURN;
    };
    uint64_t RCX;
    union {
      uint64_t RDX;
      uint64_t ARG3;
    };
    uint64_t RBX;
    uint64_t RBP;
    union {
      uint64_t RSI;
      uint64_t ARG2;
    };
    union {
      uint64_t RDI;
      uint64_t ARG1;
    };
    uint64_t R8;
    uint64_t R9;
    uint64_t R10;
    uint64_t R11;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;
    uint64_t RFLAGS;
  };
  uint8_t redzone_low[1024];
  uint8_t stack[2048];
  uint8_t redzone_high[1024];
} __attribute__((packed));

// Runs a function and an instrumented function in an "isolated" context
// (almost full machine state), and then
void RunIsolatedFunction(std::function<void(IsolatedRegState *)> &setup_state,
                         void *func,
                         void *instrumented_func);

#endif  // TEST_ISOLATED_FUNCTION_H_
