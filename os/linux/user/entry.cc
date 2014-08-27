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

#include "os/linux/user/signal.h"
#include "os/logging.h"

GRANARY_DEFINE_bool(debug_gdb_prompt, true,
    "Should a GDB process attacher helper be printed out on startup? Default "
    "is `yes`.");

GRANARY_DECLARE_bool(help);

extern "C" {

// Dynamically imported from `libc`.
extern char ** __attribute__((weak)) environ;

// Manually defined syscalls. See `os/linux/arch/*/syscall.asm`.
extern int getpid(void);
extern long long read(int __fd, void *__buf, size_t __nbytes);
extern void exit_group(int) __attribute__((noreturn));
}

namespace granary {
namespace {

// Initialize Granary for debugging by GDB. For example, if one is doing:
//
//    grr --tools=foo -- ls
//
// Then in another terminal, one can do:
//
//    sudo gdb /bin/ls
//    (gdb) a <pid that is printed out>
//    (gdb) c
//
// Then press the ENTER key in the origin terminal (where `grr ... ls` is) to
// continue execution under GDB's supervision.
void AwaitAttach(int signum, void *siginfo, void *context) {
  char buff[2];
  os::Log(os::LogOutput, "Process ID for attaching GDB: %d\n", getpid());
  os::Log(os::LogOutput, "Press enter to continue.\n");
  read(0, buff, 1);

  // Useful for debugging purposes.
  GRANARY_USED(signum);
  GRANARY_USED(siginfo);  // `siginfo_t *`.
  GRANARY_USED(context);  // `ucontext *` on Linux.
}

// Used to attach a signal handler to an arbitrary signal, such that when the
// signal is triggered, a message is printed to the screen that allows
// Granary to be attached to the process.
static void AwaitAttachOnSignal(int signum) {
  struct sigaction new_sigaction;
  memset(&new_sigaction, 0, sizeof new_sigaction);
  memset(&(new_sigaction.sa_mask), 0xFF, sizeof new_sigaction.sa_mask);
  new_sigaction.sa_sigaction = &AwaitAttach;
  new_sigaction.sa_flags = SA_SIGINFO;
  rt_sigaction(signum, &new_sigaction, nullptr, _NSIG / 8);
}

// Initialize Granary for debugging. This is geared toward GDB-based debugging,
// where we can either attach GDB on program startup. Alternatively, if
// attaching GDB somehow makes the bug being debugged disappear, then we
// register a signal handler for `SEGFAULT`s that will prompt for GDB to be
// attached.
static void InitDebug(void) {
  if (FLAG_help) return;
  if (FLAG_debug_gdb_prompt) {
    AwaitAttach(-1, nullptr, nullptr);
  } else {
    AwaitAttachOnSignal(SIGSEGV);
    AwaitAttachOnSignal(SIGILL);
    AwaitAttachOnSignal(SIGTRAP);
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

// Attach to the program by means of taking of the return address of `_init`.
//
// See `os/linux/arch/*/init.asm` to see the definition of `_init` and the
// pass-through to `granary_init`.
static void Attach(AppPC *start_pc_ptr) {
  if (auto context = GlobalContext()) {
    os::Log(os::LogOutput, "Attaching Granary.\n");
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
  InitOptions(GetEnv("GRANARY_OPTIONS"));
  if (FLAG_help) {
    PrintAllOptions();
    exit_group(0);
    GRANARY_ASSERT(false);  // Not reached.
  }

  GRANARY_IF_VALGRIND(if (false)) InitDebug();
  Init();
  Attach(attach_pc_ptr);
}
}  // extern "C"

#endif  // GRANARY_TARGET_test
#pragma clang diagnostic pop
