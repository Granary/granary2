/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_SIGNAL_H_
#define OS_LINUX_USER_SIGNAL_H_

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

extern int rt_sigaction(int signum, const struct sigaction *act,
                        struct sigaction *oldact);

#define sa_sigaction __sigaction_handler.sa_sigaction

}  // extern C

#endif  // OS_LINUX_USER_SIGNAL_H_
