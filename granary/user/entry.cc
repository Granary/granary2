/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#ifdef GRANARY_STANDALONE
# include <cstdlib>
#endif

#include "granary/base/cast.h"
#include "granary/base/options.h"
#include "granary/base/string.h"

#include "granary/user/init.h"

#include "granary/driver.h"
#include "granary/instrument.h"
#include "granary/logging.h"
#include "granary/metadata.h"
#include "granary/module.h"
#include "granary/tool.h"

namespace granary {

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=bbcount,pgo`.")

namespace {

// Load a tool into Granary.
static void LoadTool(const char *tool_name) {
  void *tool(dlopen(tool_name, RTLD_NOW | RTLD_LOCAL));
  if (!tool) {
    Log(LogError, "Failed to load tool '%s': %s\n", tool_name, dlerror());
    return;
  }

  // The tool's static initializers should have registered the tool.
}

enum {
  TOOL_NAME_BUFF_LEN = 1024  // TODO(pag): Likely less than `PATH_MAX`.
};
char tool_name_buff[TOOL_NAME_BUFF_LEN] = {'\0'};
char *tool_name(&(tool_name_buff[0]));

// Scan the `tools` command line option and load each tool in order.
static void LoadTools(void) {
  *tool_name++ = 'l';
  *tool_name++ = 'i';
  *tool_name++ = 'b';

  const char *ch(FLAG_tools);
  for (int i(0); *ch; ++ch) {
    tool_name[i++] = *ch;
    if (!ch[1] || ',' == ch[1]) {  // End of tool name list, or next tool name.
      tool_name[i] = '.';
      tool_name[i + 1] = 's';
      tool_name[i + 2] = 'o';
      tool_name[i + 3] = '\0';
      LoadTool(tool_name_buff);
      i = 0;
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

#ifdef GRANARY_STANDALONE
static void InitToolPath(const char *exe_path) {
  realpath(exe_path, tool_name_buff);
  char *ch(&(tool_name_buff[0]));
  char *last_slash(nullptr);
  for (; *ch; ++ch) {
    if ('/' == *ch) {
      last_slash = ch;
    }
  }
  if (last_slash) {
    last_slash[1] = '\0';
  }
  tool_name += StringLength(tool_name_buff);
}
#endif

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

  // Tell granary about loaded modules.
  InitModules(InitKind::DYNAMIC);

  TestInstrument();
}

}  // namespace granary

#ifdef GRANARY_STANDALONE

extern "C" {
int main(int argc, const char *argv[]) {
  GRANARY_USING_NAMESPACE granary;
  InitToolPath(argv[0]);
  InitOptions(argc, argv);
  Init();
  return 0;
}
}  // extern C
#else

GRANARY_DEFINE_string(attach_to, "*",
    "Comma-separated list of modules to which granary should attach. Default "
    "is `*`, representing that Granary will attach to all (non-Granary, non-"
    "tool) modules. More specific requests can be made, for example:\n"
    "\t--attach_to=[*,-libc]\t\tAttach to everything but `libc`.\n"
    "\t--attach_to=libc\t\tOnly attach to `libc`.")

GRANARY_INIT({
  GRANARY_USING_NAMESPACE granary;
  InitOptions(getenv("GRANARY_OPTIONS"));
  Init();
})

#endif
