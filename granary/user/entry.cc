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

#include "granary/breakpoint.h"
#include "granary/driver.h"
#include "granary/environment.h"
#include "granary/logging.h"
#include "granary/metadata.h"

namespace granary {

GRANARY_DEFINE_string(tools, "",
    "Colon-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=bbcount:pgo`.")

namespace {

// void VisitBasicBlock(ControlFlowGraph *cfg, InFlightBasicBlock *entry_block);
const char * const VISIT_BASIC_BLOCK = \
    "_Z15VisitBasicBlockPN7granary16ControlFlowGraphEPNS_18InFlightBasicBlockE";

// void InitDynamic(void)
const char * const INIT_DYNAMIC = "_Z11InitDynamicv";

// Load a tool into Granary.
static void LoadTool(const char *tool_name) {
  void *tool(dlopen(tool_name, RTLD_NOW | RTLD_LOCAL));
  if (!tool) {
    Log(LogError, "Failed to load tool '%s'.\n", tool_name);
    return;
  }

  auto InitDynamic = UnsafeCast<void (*)(void)>(dlsym(tool, INIT_DYNAMIC));
  if (InitDynamic) {
    InitDynamic();
  }
}

// Scan the `tools` command line option and load each tool in order.
static void LoadTools(void) {
  char tool_name[256] = {'l', 'i', 'b', '\0'};
  const char *ch(FLAG_tools);

  if (!*ch) {
    return;
  }

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

}  // namespace

// Initialize Granary.
static void Init(void) {
  driver::Init();

#ifdef GRANARY_DEBUG
  char buff[2];
  Log(LogOutput, "Process ID for attaching GDB: %d\n", getpid());
  Log(LogOutput, "Press enter to continue.\n");
  read(0, buff, 1);
#endif

  LoadTools();
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

__attribute__((constructor(102), used))
void granary_init(void) {
  granary::InitOptions(getenv("GRANARY_OPTIONS"));
  granary::Init();
}

#endif
