/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"
#include "granary/base/string.h"
#include "granary/code/allocate.h"

#include "granary/compile.h"
#include "granary/driver.h"
#include "granary/environment.h"
#include "granary/init.h"
#include "granary/logging.h"
#include "granary/metadata.h"
#include "granary/module.h"
#include "granary/tool.h"

// TODO(pag): Remove me.
#include "granary/base/cast.h"
#include "granary/base/types.h"

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=print_bbs,follow_jumps`.")

#ifndef GRANARY_STANDALONE
GRANARY_DEFINE_string(attach_to, "*",
    "Comma-separated list of modules to which granary should attach. Default "
    "is `*`, representing that Granary will attach to all (non-Granary, non-"
    "tool) modules. More specific requests can be made, for example:\n"
    "\t--attach_to=[*,-libc]\t\tAttach to everything but `libc`.\n"
    "\t--attach_to=libc\t\tOnly attach to `libc`.")
#endif  // GRANARY_STANDALONE

GRANARY_DEFINE_non_negative_int(edge_cache_slab_size, 1,
    "The number of pages allocated at once to store edge code. Each "
    "environment maintains its own edge code allocator. The default value is "
    "1 pages per slab.")

namespace granary {
namespace {

// TODO(pag): Change this! Can't access the runtime value of
//            FLAG_edge_cache_slab_size at load time.
GRANARY_EARLY_GLOBAL static ModuleManager modules;
GRANARY_EARLY_GLOBAL static CodeAllocator edge_cache_allocator(FLAG_edge_cache_slab_size);
GRANARY_EARLY_GLOBAL static Environment env(&modules, &edge_cache_allocator);
}

// Scan the `tools` command line option and load each tool in order.
//
// Defined in either `granary/user/init.cc` or `granary/kernel/init.cc`.
void LoadTools(const char *granary_path);

// Initialize Granary.
void Init(InitKind kind, const char *granary_path) {

  // Initialize the driver (e.g. DynamoRIO). This usually performs from
  // architecture-specific checks to determine which architectural features
  // are enabled.
  driver::Init();

  // Dynamically load in zero or more tools. Tools are specified on the
  // command-line.
  LoadTools(granary_path);

  // Initialize all instrumentation tools for dynamic instrumentation of a
  // running binary.
  InitTools(kind);

  // Finalize the meta-data structure after tools are initialized. Tools might
  // change what meta-data is registered depending on command-line options.
  RegisterMetaData<ModuleMetaData>();  // TODO(pag): Move me!
  InitMetaData();

  // Tell granary about loaded modules.
  modules.FindBuiltInModules();

  // Initialize the code cache.
  InitCompiler();

  // TODO(pag): Remove me.
  auto pc = UnsafeCast<AppPC>(&Log);
  Compile(&env, env.AllocateBlockMetaData(pc));
}

}  // namespace granary
