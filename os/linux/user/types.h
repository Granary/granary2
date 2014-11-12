/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_TYPES_H_
#define OS_LINUX_USER_TYPES_H_

#define __restrict

#include <fcntl.h>

#include <stddef.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdlib.h>

#include <sched.h>

#include <asm/prctl.h>
#include <sys/prctl.h>

#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <ucontext.h>
#include <unistd.h>
#include <asm/unistd.h>

extern int arch_prctl(int option, ...);

struct kernel_sigaction {
  union {
    __sighandler_t handler;
    void (*siginfo_handler)(int, siginfo_t *, void *);
  } handler;
  /* `sa_flags` and `sa_mask` are reversed in the kernel as compared with
   * glibc.
   *
   * http://code.woboq.org/linux/linux/include/linux/signal.h.html#sigaction
   */
  int sa_flags;
  __sigset_t sa_mask;
  void (*sa_restorer)(void);
};

extern int rt_sigaction(int sig, const struct kernel_sigaction *new_act,
                        struct kernel_sigaction *old_act, size_t sigsetsize);

#undef __restrict

#endif  // OS_LINUX_USER_TYPES_H_
