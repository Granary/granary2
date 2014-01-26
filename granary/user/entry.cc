/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>

#include "granary/base/cast.h"
#include "granary/base/options.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/user/init.h"

#include "granary/breakpoint.h"
#include "granary/driver.h"
#include "granary/environment.h"
#include "granary/logging.h"
#include "granary/metadata.h"
#include "granary/tool.h"

namespace granary {

GRANARY_DEFINE_string(tools, "",
    "Colon-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=bbcount:pgo`.")

namespace {

// Load a tool into Granary.
static void LoadTool(const char *tool_name) {
  void *tool(dlopen(tool_name, RTLD_NOW | RTLD_LOCAL));
  if (!tool) {
    Log(LogError, "Failed to load tool '%s'.\n", tool_name);
    return;
  }

  // The tool's static initializers should have registered the tool.
}

// Scan the `tools` command line option and load each tool in order.
static void LoadTools(void) {
  char tool_name[256] = {'l', 'i', 'b', '\0'};
  const char *ch(FLAG_tools);
  for (int i(3); *ch; ++ch) {
    tool_name[i++] = *ch;
    if (!ch[1] || ':' == ch[1]) {
      tool_name[i] = '.';
      tool_name[i + 1] = 's';
      tool_name[i + 2] = 'o';
      tool_name[i + 3] = '\0';
      LoadTool(tool_name);
      i = 3;
      ++ch;
    }
  }
}

// Initialize Granary for debugging by GDB. For example, if one is doing:
//
//    grr --tools=foo -- ls
//
// Then in another terminal, one can do:
//
//    gdb ls
//    > attach <pid that is printed out>
//    > c
//
// Then press the ENTER key in the origin terminal (where `grr ... ls` is) to
// continue execution under GDB's supervision.
static void InitDebug(void) {
#ifdef GRANARY_DEBUG
  char buff[2];
  Log(LogOutput, "Process ID for attaching GDB: %d\n", getpid());
  Log(LogOutput, "Press enter to continue.\n");
  read(0, buff, 1);
#endif
}

}  // namespace

// Initialize Granary.
static void Init(void) {
  InitDebug();

  // Initialize the driver (e.g. DynamoRIO). This usually performs from
  // architecture-specific checks to determine which architectural features
  // are enabled.
  driver::Init();

  // Dynamically load in zero or more tools. Tools are specified on the
  // command-line.
  LoadTools();

  // Initialize all instrumentation tools for dynamic instrumentation of a
  // running binary.
  InitTools(InitKind::DYNAMIC);

  // Finalize the meta-data structure after tools are initialized. Tools might
  // change what meta-data is registered depending on command-line options.
  InitMetaData();
}

}  // namespace granary

#ifdef GRANARY_STANDALONE

extern "C" {
int main(int argc, const char *argv[]) {
  granary::InitOptions(argc, argv);
  granary::Init();
  return 0;
}
}  // extern C
#else

GRANARY_INIT(granary, {
  granary::InitOptions(getenv("GRANARY_OPTIONS"));
  granary::Init();
})

#endif
