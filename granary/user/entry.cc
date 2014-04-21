/* Copyright 2014 Peter Goodman, all rights reserved. */

// Only include a main() or GRANARY_INIT function if we're not using the test
// target.
#ifndef GRANARY_TEST

// Yes this is redundant. It's to avoid clang's `-Wheader-guard` errors!
#define GRANARY_TEST
#undef GRANARY_TEST

#define GRANARY_INTERNAL

#include <sys/types.h>
#include <unistd.h>

#ifdef GRANARY_STANDALONE
# include <cstdio>
# include <cstdlib>
# include "granary/base/string.h"
#endif

#include "granary/base/option.h"
#include "granary/init.h"
#include "granary/logging.h"

GRANARY_DEFINE_bool(gdb_prompt, true,
    "Should a GDB process attacher helper be printed out on startup? Default "
    "is yes.");

namespace granary {
namespace {

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
  if (FLAG_gdb_prompt) {
    char buff[2];
    Log(LogOutput, "Process ID for attaching GDB: %d\n", getpid());
    Log(LogOutput, "Press enter to continue.\n");
    read(0, buff, 1);
  }
}

#ifdef GRANARY_STANDALONE
enum {
  GRANARY_PATH_BUFF_LEN = 1024  // TODO(pag): Probably should be `PATH_MAX`.
};

static char granary_path_buff[GRANARY_PATH_BUFF_LEN] = {'\0'};

// Copy the executable's path into `tool_name_path`.
static const char *GetGranaryPath(const char *granary_exe_path) {
  realpath(granary_exe_path, granary_path_buff);

  char *last_slash(nullptr);
  for (char *ch(&(granary_path_buff[0])); *ch; ++ch) {
    if ('/' == *ch) {
      last_slash = ch;
    }
  }
  if (last_slash) {
    last_slash[1] = '\0';
  }
  return &(granary_path_buff[0]);
}
#endif  // GRANARY_STANDALONE

}  // namespace
}  // namespace granary

#ifdef GRANARY_STANDALONE

extern "C" {
int main(int argc, const char *argv[]) {
  granary::InitOptions(argc, argv);
  granary::InitDebug();
  granary::Init(granary::GetGranaryPath(argv[0]));
  return 0;
}
}  // extern C
#else

GRANARY_INIT({
  granary::InitOptions(getenv("GRANARY_OPTIONS"));
  granary::InitDebug();
  granary::Init(getenv("GRANARY_PATH"));
})

#endif  // GRANARY_STANDALONE
#endif  // GRANARY_TEST
