/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <dlfcn.h>
#include <cstdlib>

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/client.h"
#include "granary/logging.h"

GRANARY_DEFINE_string(clients, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--clients=print_bbs,follow_jumps`.");

namespace granary {
namespace {

// Load a tool into Granary.
static void LoadClient(const char *tool_path) {
  void *tool(dlopen(tool_path, RTLD_NOW | RTLD_LOCAL));
  if (!tool) {
    Log(LogError, "Failed to load client '%s': %s\n", tool_path, dlerror());
    return;
  }

  // Initialize the client. This should register various tools.
  auto init_func = dlsym(
      tool, GRANARY_TO_STRING(GRANARY_CLIENT_INIT_FUNC_NAME));
  if (init_func) {
    UnsafeCast<void (*)(void)>(init_func)();
  }
}

enum {
  MAX_CLIENT_PATH_LEN = 1024  // TODO(pag): Likely less than `PATH_MAX`.
};

// Buffer containing the path to the directory containing client shared
// libraries.
static char client_path_buff[MAX_CLIENT_PATH_LEN] = {'\0'};

}  // namespace

// Scan the `clients` command line option and load each client in order.
void LoadClients(const char *granary_path) {
  auto len = StringLength(granary_path);
  GRANARY_ASSERT((MAX_CLIENT_PATH_LEN - 2 * MAX_CLIENT_NAME_LEN) > len);

  // Figure out if we need a trailing path separator.
  const char *path_sep = "";
  if (0 < len && '/' != granary_path[len - 1]) {
    path_sep = "/";
  }

  // Dynamically load each client.
  ForEachCommaSeparatedString<MAX_CLIENT_NAME_LEN>(
      FLAG_clients,
      [=](const char *client_name) {
        if (RegisterClient(client_name)) {
          Format(client_path_buff, MAX_CLIENT_PATH_LEN,
                 "%s%slib%s.so", granary_path, path_sep, client_name);
          LoadClient(client_path_buff);
        }
      });
}

}  // namespace granary
