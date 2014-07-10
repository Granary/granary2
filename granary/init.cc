/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/cpu.h"
#include "granary/arch/init.h"

#include "granary/base/container.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/client.h"
#include "granary/context.h"
#include "granary/init.h"
#include "granary/logging.h"
#include "granary/translate.h"

// TODO(pag): Remove me.
#include "granary/base/cast.h"
#include "granary/base/pc.h"

#include <unistd.h>

#ifndef GRANARY_STANDALONE
GRANARY_DEFINE_string(attach_to, "*",
    "Comma-separated list of modules to which granary should attach. Default "
    "is `*`, representing that Granary will attach to all (non-Granary, non-"
    "tool) modules. More specific requests can be made, for example:\n"
    "\t--attach_to=[*,-libc]\t\tAttach to everything but `libc`.\n"
    "\t--attach_to=libc\t\tOnly attach to `libc`.");
#endif  // GRANARY_STANDALONE

extern "C" {
extern int granary_test_mangle(int);
}

GRANARY_DEFINE_string(clients, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--clients=print_bbs,follow_jumps`.");

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=print_bbs,follow_jumps`.");

GRANARY_DEFINE_bool(help, false,
    "Print this message.");

enum {
  NUM_ITERATIONS = 10,
  MAX_FIB = 20
};

namespace granary {
namespace {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
GRANARY_EARLY_GLOBAL static Container<Context> context;
#pragma clang diagnostic pop

extern "C" int fibonacci(int n);
extern "C" int (*fib_indirect)(int);
extern "C" int (*fib)(int);

extern "C" int (*fib_indirect)(int) = fibonacci;
extern "C" int (*fib)(int) = nullptr;
extern "C" int fibonacci(int n) {
  if (!n) return n;
  if (1 == n) return 1;
  return fib_indirect(n - 1) + fib_indirect(n - 2);
}

}  // namespace

// Initialize Granary.
void Init(const char *granary_path) {

  // Initialize the driver (e.g. XED, DynamoRIO). This usually performs from
  // architecture-specific checks to determine which architectural features
  // are enabled.
  arch::Init();

  // Dynamically load in zero or more clients. In user space, clients are
  // specified on the command-line. In kernel-space, clients are compiled in
  // with the Granary binary.
  //
  // We do this before finding and registering all built-in modules so that
  // module registration picks up on existing clients.
  LoadClients(FLAG_clients, granary_path);

  if (!FLAG_help) {
    context.Construct();
    context->InitTools(FLAG_tools);

    if (!FLAG_help) {
      auto start_pc = Translate(context.AddressOf(), fibonacci);
      fib = UnsafeCast<int (*)(int)>(start_pc);
      for (auto i = 2; i < 30; ++i) {
        Log(LogOutput, "fibonacci(%d) = %d; fib(%d) = %d\n",
            i, fib(i), i, fib(i));
      }
#if 1
      sleep(1);
      auto native_start = cpu::CycleCount();
      for (auto iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (auto n = 0; n < MAX_FIB; ++n) {
          fib_indirect(n);
        }
      }
      auto native_end = cpu::CycleCount();
      sleep(1);
      auto inst_start = cpu::CycleCount();
      for (auto iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (auto n = 0; n < MAX_FIB; ++n) {
          fib(n);
        }
      }
      auto inst_end = cpu::CycleCount();
      auto ticks_per_native = (native_end - native_start) / NUM_ITERATIONS;
      auto ticks_per_inst = (inst_end - inst_start) / NUM_ITERATIONS;
      Log(LogOutput, "Ticks per native = %lu\nTicks per instrumented = %lu\n",
          ticks_per_native, ticks_per_inst);
#endif

    } else {
      for (auto i = 0; i < 10; ++i) {
        Log(LogOutput, "fib(%d) = %d\n", i, granary_test_mangle(i));
      }
      //granary_test_mangle(3);
      //Translate(context.AddressOf(), granary_test_mangle);
    }

    context.Destroy();
  } else {
    PrintAllOptions();
  }

  UnloadClients();
}

}  // namespace granary
