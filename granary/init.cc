/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/container.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/client.h"
#include "granary/driver.h"
#include "granary/environment.h"
#include "granary/init.h"
#include "granary/logging.h"

// TODO(pag): Remove me.
#include "granary/base/cast.h"
#include "granary/base/pc.h"

#ifndef GRANARY_STANDALONE
GRANARY_DEFINE_string(attach_to, "*",
    "Comma-separated list of modules to which granary should attach. Default "
    "is `*`, representing that Granary will attach to all (non-Granary, non-"
    "tool) modules. More specific requests can be made, for example:\n"
    "\t--attach_to=[*,-libc]\t\tAttach to everything but `libc`.\n"
    "\t--attach_to=libc\t\tOnly attach to `libc`.")
#endif  // GRANARY_STANDALONE

namespace granary {
namespace {

static int curr_env = 0;
GRANARY_EARLY_GLOBAL static Container<Environment> envs[2];

}  // namespace

// Initialize Granary.
void Init(const char *granary_path) {

  // Initialize the driver (e.g. DynamoRIO). This usually performs from
  // architecture-specific checks to determine which architectural features
  // are enabled.
  driver::Init();

  // Dynamically load in zero or more clients. In user space, clients are
  // specified on the command-line. In kernel-space, clients are compiled in
  // with the Granary binary.
  //
  // We do this before finding and registering all built-in modules so that
  // module registration picks up on existing clients.
  LoadClients(granary_path);

  auto &env = envs[curr_env];
  env.Construct();

  // TODO(pag): Remove me.
  auto pc = UnsafeCast<AppPC>(&Log);

  env->Setup();
  env->AttachToAppPC(pc);
}

}  // namespace granary
