/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/options.h"
#include "granary/base/string.h"

#include "granary/compile.h"
#include "granary/driver.h"
#include "granary/init.h"
#include "granary/logging.h"
#include "granary/metadata.h"
#include "granary/module.h"
#include "granary/tool.h"

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

namespace granary {

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
  InitMetaData();

  // Tell granary about loaded modules.
  InitModules(kind);

  // Initialize the code cache.
  InitCompiler();
}


}  // namespace granary
