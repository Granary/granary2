/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_TYPES_H_
#define OS_LINUX_USER_TYPES_H_

#define __restrict

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

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
#include <sys/ptrace.h>
#include <sys/reg.h>
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
#include <sys/user.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <ucontext.h>
#include <unistd.h>
#include <asm/unistd.h>

#include <semaphore.h>
#include <time.h>

#include <linux/futex.h>

#include <aio.h>
#include <arpa/inet.h>
#include <assert.h>
#include <complex.h>
#include <cpio.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <float.h>
#include <fmtmsg.h>
#include <fnmatch.h>
#include <ftw.h>
#include <glob.h>
#include <inttypes.h>
#include <iso646.h>
#include <langinfo.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <monetary.h>
//#include <ndbm.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <nl_types.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <sched.h>
#include <search.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
#include <syslog.h>
#include <tar.h>
#include <termios.h>
#include <tgmath.h>
#include <time.h>
#include <ulimit.h>
#include <unistd.h>
#include <utime.h>
#include <utmpx.h>
#include <wchar.h>
#include <wctype.h>
#include <wordexp.h>


#include <ucontext.h>

#ifdef __linux
#   include <grp.h>
#   include <crypt.h>
#   include <iconv.h>
#   include <sys/socket.h>
#endif

#ifndef HAVE_SA_RESTORER
# define HAVE_SA_RESTORER
#endif

#ifndef SA_RESTORER
# define SA_RESTORER 0x04000000
#endif

// This is the sigaction structure from the Linux 2.1.20 kernel.
struct old_kernel_sigaction {
  __sighandler_t k_sa_handler;
  unsigned long sa_mask;
  unsigned long sa_flags;
  void (*sa_restorer) (void);
};

// This is the sigaction structure from the Linux 2.1.68 kernel.
struct kernel_sigaction {
  __sighandler_t k_sa_handler;
  unsigned long sa_flags;
  void (*sa_restorer) (void);
  sigset_t sa_mask;
};

extern int rt_sigaction(int sig, const struct kernel_sigaction *new_act,
                        struct kernel_sigaction *old_act, size_t sigsetsize);

extern void rt_sigreturn(void);

// Raw clone system call, plus an extra parameter :-D
extern long sys_clone(unsigned long clone_flags, char *newsp,
                      int *parent_tidptr, int *child_tidptr, int tls_val,
                      void (*func)(void));

extern int arch_prctl(int option, ...);

#undef __restrict

#endif  // OS_LINUX_USER_TYPES_H_
