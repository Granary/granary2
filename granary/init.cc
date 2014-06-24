/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

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

#ifndef GRANARY_STANDALONE
GRANARY_DEFINE_string(attach_to, "*",
    "Comma-separated list of modules to which granary should attach. Default "
    "is `*`, representing that Granary will attach to all (non-Granary, non-"
    "tool) modules. More specific requests can be made, for example:\n"
    "\t--attach_to=[*,-libc]\t\tAttach to everything but `libc`.\n"
    "\t--attach_to=libc\t\tOnly attach to `libc`.");
#endif  // GRANARY_STANDALONE

extern "C" {
extern void granary_test_mangle(void);
}

GRANARY_DEFINE_bool(help, false,
    "Print this message.");

namespace granary {
namespace {

GRANARY_EARLY_GLOBAL static Container<Context> context;

static int fibonacci(int n) {
  if (!n) return n;
  if (1 == n) return 1;
  return fibonacci(n - 1) + fibonacci(n - 2);
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
  LoadClients(granary_path);

  if (!FLAG_help) {
    context.Construct();

    auto start_pc = Translate(context.AddressOf(), fibonacci);
    auto fib = UnsafeCast<int (*)(int)>(start_pc);
    for (auto i = 0; i < 30; ++i) {
      Log(LogOutput, "fibonacci(%d) = %d; fib(%d) = %d\n", i, fibonacci(i), i, fib(i));
    }

    context.Destroy();
  } else {
    PrintAllOptions();
  }

  UnloadClients();
}

}  // namespace granary
