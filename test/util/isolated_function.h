/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_UTIL_ISOLATED_FUNCTION_H_
#define TEST_UTIL_ISOLATED_FUNCTION_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"

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
  };
  uint8_t redzone_low[1024];
  uint8_t stack[8192];
  uint8_t redzone_high[1024];
} __attribute__((packed));

// Runs a function and an instrumented function in an "isolated" context
// (almost full machine state), and then
void RunIsolatedFunction(std::function<void(IsolatedRegState *)> &setup_state,
                         void *func,
                         void *instrumented_func);

namespace detail {

inline static void SetArg(int, std::initializer_list<uint64_t *>) {}

// Set an argument in the `IsolatedRegState` structure.
template <typename T, typename... Args>
inline static void SetArg(int arg_num, std::initializer_list<uint64_t *> regs,
                          T arg, Args... rest) {
  *(regs.begin()[arg_num]) = granary::UnsafeCast<uint64_t>(arg);
  SetArg(arg_num + 1, regs, rest...);
}

}  // namespace detail

template <typename R, typename... Args>
void RunIsolatedFunction(R (*func)(Args...), void *instrumented_func,
                         Args... args) {
  std::function<void(IsolatedRegState *)> setup([=] (IsolatedRegState *regs) {
    auto arg_regs = {&(regs->ARG1), &(regs->ARG2), &(regs->ARG3)};
    detail::SetArg(0, arg_regs, args...);
  });
  RunIsolatedFunction(setup, granary::UnsafeCast<void *>(func),
                      instrumented_func);
}

#endif  // TEST_UTIL_ISOLATED_FUNCTION_H_
