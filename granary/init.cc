/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/init.h"

#include "granary/base/option.h"

#include "granary/client.h"
#include "granary/context.h"
#include "granary/init.h"

#include "os/logging.h"
#include "os/memory.h"
#include "os/module.h"

GRANARY_DEFINE_bool(help, false,
    "Print this message.");

extern "C" {

typedef void (*InitFuncPtr)(void);

// Defined by the linker script `linker.lds`.
extern InitFuncPtr granary_begin_init_array[];
extern InitFuncPtr granary_end_init_array[];

}  // extern "C"
namespace granary {
namespace {

// Have we already pre-initialized Granary? This is mostly only relevant for
// the test cases, where a given test fixture might initialize Granary, and
// so the initialization will happen for each of that fixture's tests.
static bool done_preinit = false;

}  // namespace

// Runs the constructors from the initialization array.
void PreInit(void) {
  if (done_preinit) return;
  done_preinit = true;

  InitFuncPtr *init_func = granary_begin_init_array;
  for (; init_func < granary_end_init_array; ++init_func) {
    (*init_func)();
  }
}

// Initialize Granary.
void Init(InitReason reason) {
  os::InitHeap();  // Initialize the Granary heap.
  os::InitModuleManager();  // Initialize the global module manager.
  os::InitLog();  // Initialize the logging infrastructure.

  // Initialize the driver (e.g. XED, DynamoRIO). This usually performs some
  // architecture-specific checks to determine which architectural features
  // are enabled. This depends on heap allocation.
  arch::Init();

  InitContext();
  InitClients();
  InitTools(reason);
}

}  // namespace granary
