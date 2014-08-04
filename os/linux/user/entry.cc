/* Copyright 2014 Peter Goodman, all rights reserved. */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wheader-guard"

// Only include GRANARY_INIT function if we're not building the `test` target.
// target.
#ifndef GRANARY_TEST

#define GRANARY_INTERNAL

#include "granary/base/option.h"
#include "granary/init.h"

#include "os/logging.h"

GRANARY_DEFINE_bool(show_gdb_prompt, true,
    "Should a GDB process attacher helper be printed out on startup? Default "
    "is `yes`.");

GRANARY_DECLARE_bool(help);

extern "C" {
extern char ** __attribute__((weak)) environ;
extern int granary_getpid(void);
extern long long granary_read(int __fd, void *__buf, size_t __nbytes);
}

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
  if (FLAG_show_gdb_prompt && !FLAG_help) {
    char buff[2];
    os::Log(os::LogOutput, "Process ID for attaching GDB: %d\n", granary_getpid());
    os::Log(os::LogOutput, "Press enter to continue.\n");
    granary_read(0, buff, 1);
  }
}

// Searches for a specific environment variable.
static const char *GetEnv(const char *var_name) {
  if (!environ) return nullptr;
  for (auto env(environ); *env; ++env) {
    auto chr = *env;
    for (auto var_chr(var_name); *chr; ++chr, ++var_chr) {
      if ('=' == *chr) {
        if (*var_chr) {
          break;  // Didn't match.
        }
      } else if (*chr != *var_chr) {
        break;  // Didn't match.
      } else {
        continue;  // In the process of matching.
      }
      return chr + 1;  // Matched.
    }
  }
  return nullptr;
}

}  // namespace
}  // namespace granary

GRANARY_INIT({
  granary::InitOptions(GetEnv("GRANARY_OPTIONS"));
  granary::InitDebug();
  granary::Init();
})

#endif  // GRANARY_TEST
#pragma clang diagnostic pop
