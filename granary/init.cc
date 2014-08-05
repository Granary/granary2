/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/init.h"

#include "granary/base/container.h"
#include "granary/base/option.h"

#include "granary/client.h"
#include "granary/context.h"
#include "granary/init.h"

#include "os/memory.h"

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
void Init(void) {
  os::InitHeap();  // Initialize the Granary heap.

  // Initialize the driver (e.g. XED, DynamoRIO). This usually performs from
  // architecture-specific checks to determine which architectural features
  // are enabled.
  arch::Init();

  if (!FLAG_help) {
    context.Construct();
    InitClients();
    context->InitTools(FLAG_tools);
    SetGlobalContext(context.AddressOf());
  } else {
    PrintAllOptions();
  }
}

}  // namespace granary
