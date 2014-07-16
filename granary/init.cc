/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/init.h"

#include "granary/base/container.h"
#include "granary/base/option.h"

#include "granary/attach.h"
#include "granary/client.h"
#include "granary/context.h"
#include "granary/init.h"
#include "granary/memory.h"

GRANARY_DEFINE_string(clients, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--clients=print_bbs,follow_jumps`.");

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=print_bbs,follow_jumps`.");

GRANARY_DEFINE_bool(help, false,
    "Print this message.");

namespace granary {
namespace {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
GRANARY_EARLY_GLOBAL static Container<Context> context;
#pragma clang diagnostic pop

}  // namespace

// Initialize Granary.
void Init(const char *granary_path) {
  InitHeap();  // Initialize the Granary heap.

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

    // Attach Granary to the running binary.
    Attach(context.AddressOf());

#ifdef GRANARY_STANDALONE
    context.Destroy();
#endif  // GRANARY_STANDALONE
  } else {
    PrintAllOptions();
  }

#ifdef GRANARY_STANDALONE
  UnloadClients();
#endif  // GRANARY_STANDALONE
}

}  // namespace granary
