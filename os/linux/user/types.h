/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_TYPES_H_
#define OS_LINUX_USER_TYPES_H_

#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE
#endif

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

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

#undef __restrict

#endif  // OS_LINUX_USER_TYPES_H_
