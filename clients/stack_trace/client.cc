/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#ifdef GRANARY_WHERE_user

GRANARY_USING_NAMESPACE granary;

namespace {
enum : size_t {
  kInitialThreadStackIndex = 63,
  kMaxThreadStackSize = 256
};

// Stack of return addresses.
static __thread AppPC tThreadStack[kMaxThreadStackSize] = {nullptr};
static __thread size_t tThreadStackIndex = kInitialThreadStackIndex;

}  // namespace

// Copy up to `buff_size` of the most recent program counters from the stack
// trace into `buff`, and return the number of copied
size_t CopyStackTrace(AppPC *buff, size_t buff_size) {
  if (!tThreadStackIndex) return 0;
  for (auto i = 0UL; i < buff_size; ++i) {
    auto index = tThreadStackIndex - i;
    if (!index) return i;
    auto pc = tThreadStack[index];
    if (!pc) return i;
    buff[i] = pc;
  }
  return buff_size;
}

// Simple tool for static and dynamic basic block counting.
class CallStackTracer : public InstrumentationTool {
 public:
  virtual ~CallStackTracer(void) = default;

  static void Init(InitReason reason) {
    if (kInitThread == reason) ResetStack();
  }

  // Add in instrumentation at the target of function calls
  virtual void InstrumentBlock(DecodedBlock *block) {
    for (auto succ : block->Successors()) {

      // Push the return address on the stack.
      if (succ.cfi->IsFunctionCall()) {
        auto return_address = succ.cfi->DecodedPC() + succ.cfi->DecodedLength();
        succ.cfi->InsertBefore(
            lir::InlineFunctionCall(block, EnterFunction, return_address));

      // Pop the return address off the stack.
      } else if (succ.cfi->IsFunctionReturn()) {
        MemoryOperand ret_addr(arch::REG_RSP, arch::ADDRESS_WIDTH_BYTES);
        succ.cfi->InsertBefore(
            lir::InlineFunctionCall(block, LeaveFunction, ret_addr));
      }
    }
  }

 protected:

  static void ResetStack(void) {
    memset(tThreadStack, 0, sizeof tThreadStack);
    tThreadStackIndex = kInitialThreadStackIndex;
  }

  // Enter a function.
  static void EnterFunction(AppPC return_address) {
    if (GRANARY_UNLIKELY(tThreadStackIndex >= kMaxThreadStackSize)) {
      ResetStack();
    }
    tThreadStack[++tThreadStackIndex] = return_address;
  }

  // Leave a function.
  static void LeaveFunction(AppPC return_address) {
    if (!tThreadStackIndex ||
        return_address != tThreadStack[tThreadStackIndex--]) {
      ResetStack();
    }
  }
};

// Initialize the `stack_trace` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<CallStackTracer>("stack_trace");
}

#endif  // GRANARY_WHERE_user
