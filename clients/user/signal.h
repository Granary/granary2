/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_SIGNAL_H_
#define OS_LINUX_USER_SIGNAL_H_

#define SA_SIGINFO    0x4
#define SA_RESETHAND  0x80000000  // Reset to SIG_DFL on entry to handler.
#define SA_ONSTACK    0x08000000  // Use signal stack by using `sa_restorer'.

// Illegal instruction (ANSI). In Granary, these would come up because of
// failed assertions.
#define SIGILL        4

// Trap instruction (POSIX).
#define SIGTRAP       5

// BUS error (4.2 BSD). E.g. trying to execute some bad memory.
#define SIGBUS        7

// Segmentation violation (ANSI). This is really just a page fault or a
// general protection fault.
#define SIGSEGV       11

#define SIGUNUSED     31

// Biggest signal number + 1 (including real-time signals).
#define _NSIG         65

// System default stack size.
#define SIGSTKSZ      8192

extern "C" {

}  // extern C

#endif  // OS_LINUX_USER_SIGNAL_H_
