/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#include "clients/user/signal.h"

using namespace granary;

GRANARY_DEFINE_bool(debug_gdb_prompt, true,
    "Should a GDB process attacher helper be printed out on startup? Default "
    "is `yes`.");

extern "C" {
extern int getpid(void);
extern long long read(int __fd, void *__buf, size_t __nbytes);

}  // extern C
namespace {
alignas(arch::PAGE_SIZE_BYTES) static char sigstack[SIGSTKSZ];

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
extern "C" void AwaitAttach(int signum, void *siginfo, void *context) {
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

}  // namespace

// Initialize Granary for debugging. This is geared toward GDB-based debugging,
// where we can either attach GDB on program startup. Alternatively, if
// attaching GDB somehow makes the bug being debugged disappear, then we
// register a signal handler for `SEGFAULT`s that will prompt for GDB to be
// attached.
void InitGDBDebug(void) {
  if (FLAG_debug_gdb_prompt) {
    AwaitAttach(-1, nullptr, nullptr);
  } else {
    const stack_t ss = {&(sigstack[0]), 0, SIGSTKSZ};
    sigaltstack(&ss, nullptr);
    AwaitAttachOnSignal(SIGSEGV);
    AwaitAttachOnSignal(SIGILL);
    AwaitAttachOnSignal(SIGBUS);
    AwaitAttachOnSignal(SIGTRAP);
  }
}
