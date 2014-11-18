/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/client.h"
#include "granary/context.h"
#include "granary/metadata.h"

#include "os/logging.h"
#include "os/memory.h"
#include "os/module.h"

namespace granary {
extern "C" {
// Exported to assembly code. This is the "fast" version of Granary's exit,
// where almost all resources are *not* cleaned up.
void granary_exit(ExitReason reason) {
#ifdef GRANARY_WITH_VALGRIND
  // If we're debugging with Valgrind then try to clean everything up. This
  // helps track down memory leaks.
  Exit(reason);
#else
  GlobalContext()->ExitTools(reason);
  os::ExitLog();
#endif  // GRANARY_WITH_VALGRIND
}

typedef void (*ExitFuncPtr)(void);

// Defined by the linker script `linker.lds`.
extern ExitFuncPtr granary_begin_fini_array[];
extern ExitFuncPtr granary_end_fini_array[];

}  // extern C
namespace {

// Runs the constructors from the initialization array.
void PostExit(void) {
  auto init_func = granary_begin_fini_array;
  for (; init_func < granary_end_fini_array; ++init_func) {
    (*init_func)();
  }
}

}  // namespace

void Exit(ExitReason reason) {
  ExitContext(reason);
  ExitClients();
  ExitMetaData();
  os::ExitLog();
  os::ExitModuleManager();
  PostExit();  // Tricky tricky!
  os::ExitHeap();
}

}  // namespace granary
