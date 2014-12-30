/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/init.h"

#include "granary/base/option.h"

#include "granary/cache.h"
#include "granary/client.h"
#include "granary/context.h"
#include "granary/init.h"
#include "granary/metadata.h"

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
static bool gDonePreInit = false;
}  // namespace

// Runs the constructors from the initialization array.
void PreInit(void) {
  if (gDonePreInit) return;
  gDonePreInit = true;
  auto init_func = granary_begin_init_array;
  for (; init_func < granary_end_init_array; ++init_func) {
    (*init_func)();
  }
}

// Initialize Granary.
void Init(InitReason reason) {
  PreInit();  // Run the pre-init just in case it hasn't been run yet.
  gDonePreInit = false;
  os::InitHeap();  // Initialize the Granary heap.
  os::InitModuleManager();  // Initialize the global module manager.
  os::InitLog();  // Initialize the logging infrastructure.

  // Initialize the driver (e.g. XED, DynamoRIO). This usually performs some
  // architecture-specific checks to determine which architectural features
  // are enabled. This depends on heap allocation.
  arch::Init();

  InitMetaData();
  InitCodeCache();
  InitClients();
  InitContext();
  InitToolManager();
  InitTools(reason);
}

}  // namespace granary
