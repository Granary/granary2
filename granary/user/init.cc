/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <dlfcn.h>
#include <cstdlib>

#include "granary/base/options.h"
#include "granary/base/string.h"
#include "granary/logging.h"

GRANARY_DECLARE_string(tools)

#ifndef GRANARY_STANDALONE
GRANARY_DECLARE_string(attach_to)
#endif  // GRANARY_STANDALONE

namespace granary {
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

static char tool_path_buff[TOOL_NAME_BUFF_LEN] = {'\0'};

}  // namespace

// Scan the `tools` command line option and load each tool in order.
void LoadTools(const char *granary_path) {
  auto tool_name = &(tool_path_buff[CopyString(
    tool_path_buff, TOOL_NAME_BUFF_LEN, granary_path)]);

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
      tool_path_buff[TOOL_NAME_BUFF_LEN - 1] = '\0';
      LoadTool(tool_path_buff);
      i = 0;
      ++ch;
    }
  }
}

}  // namespace granary
