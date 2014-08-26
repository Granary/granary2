/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_SIGNAL_H_
#define OS_LINUX_USER_SIGNAL_H_

#define SA_SIGINFO    0x4
#define SA_RESTORER   0x04000000
#define SA_RESETHAND  0x80000000  // Reset to SIG_DFL on entry to handler.
#define SA_ONSTACK    0x08000000  // Use signal stack by using `sa_restorer'.

#define SS_ONSTACK    1

// Illegal instruction (ANSI). In Granary, these would come up because of
// failed assertions.
#define SIGILL        4

// Segmentation violation (ANSI). This is really just a page fault or a
// general protection fault.
#define SIGSEGV       11

// Biggest signal number + 1 (including real-time signals).
#define _NSIG         65

// System default stack size.
#define SIGSTKSZ      8192

extern "C" {

typedef struct {
  unsigned long int __val[(1024 / (8 * sizeof (unsigned long int)))];
} __sigset_t;

// Get the kernel `sigaction` structure.
typedef void (*__sighandler_t) (int);

struct sigaction {
  union {
    __sighandler_t sa_handler;
    void (*sa_sigaction)(int, /* siginfo_t */ void *, void *);
  } __sigaction_handler;
  __sigset_t sa_mask;
  int sa_flags;
  void (*sa_restorer)(void);
};

#define sa_handler __sigaction_handler.sa_handler
#define sa_sigaction __sigaction_handler.sa_sigaction

// Alternate, preferred interface.
struct sigaltstack {
  void *ss_sp;
  int ss_flags;
  size_t ss_size;
};

extern int rt_sigaction(int signum, const struct sigaction *act,
                        struct sigaction *oldact, size_t sigsetsize);

extern int sigaltstack(const struct sigaltstack *__restrict __ss,
                       struct sigaltstack *__restrict __oss);

extern void rt_sigreturn(void);

}  // extern C

#endif  // OS_LINUX_USER_SIGNAL_H_
