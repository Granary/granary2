/* Copyright 2014 Peter Goodman, all rights reserved. */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wheader-guard"

// Only include GRANARY_INIT function if we're not building the `test` target.
// target.
#ifndef GRANARY_TARGET_test

#define GRANARY_INTERNAL

#include "granary/base/option.h"
#include "granary/base/pc.h"

#include "granary/breakpoint.h"
#include "granary/context.h"
#include "granary/init.h"
#include "granary/translate.h"

GRANARY_DECLARE_bool(help);

extern "C" {
// Dynamically imported from `libc`.
extern char ** __attribute__((weak)) environ;

// Path to the loaded Granary library. Code cache `mmap`s are associated with
// this file.
extern char granary_mmap_path[];

// Defined in `os/linux/arch/*/syscall.asm`.
extern void exit_group(int) __attribute__((noreturn));

}  // extern C

namespace granary {
namespace {
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

// Attach to the program by means of taking of the return address of `_init`.
//
// See `os/linux/arch/*/init.asm` to see the definition of `_init` and the
// pass-through to `granary_init`.
static void Attach(AppPC *start_pc_ptr) {
  if (auto context = GlobalContext()) {
    auto meta = context->AllocateBlockMetaData(*start_pc_ptr);
    *start_pc_ptr = TranslateEntryPoint(context, meta, ENTRYPOINT_USER_LOAD);
  }

  // TODO(pag): Attach to signals.
}

}  // namespace
}  // namespace granary

extern "C" {

// Initialize and attach Granary. Invoked by means of a tail-call from `_init`.
void granary_init(granary::AppPC *attach_pc_ptr) {
  GRANARY_USING_NAMESPACE granary;
  PreInit();
  strncpy(&(granary_mmap_path[0]), GetEnv("GRANARY_PATH"), 1023);
  InitOptions(GetEnv("GRANARY_OPTIONS"));
  if (FLAG_help) {
    PrintAllOptions();
    exit_group(0);
    GRANARY_ASSERT(false);  // Not reached.
  }
  Init(INIT_PROGRAM);
  Attach(attach_pc_ptr);
}
}  // extern "C"

#endif  // GRANARY_TARGET_test
#pragma clang diagnostic pop
