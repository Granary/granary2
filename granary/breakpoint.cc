/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"

extern "C" {

#ifdef GRANARY_WHERE_user
extern ssize_t write(int, const char *, size_t);
extern ssize_t read (int, void *, size_t);
extern int getpid (void);
#endif

void granary_unreachable(const char *error) {
  GRANARY_IF_VALGRIND(VALGRIND_PRINTF_BACKTRACE("Assertion failed:\n"));

  // Try to do a reasonable job of reporting the problem before we abort with
  // a `SIGILL`.
  if (error) {
#ifdef GRANARY_WHERE_user
    char buff[1024];
    auto num_bytes = granary::Format(buff, sizeof buff,
        "Assertion failed: %s.\n"
        "Process ID for attaching GDB: %d\n"
        "Press enter to continue.\n",
        error, getpid());
    write(1, buff, num_bytes);
    read(0, buff, 1);
#endif  // GRANARY_WHERE_user
  }

  __builtin_trap();
}

void granary_curiosity(void) {
  GRANARY_INLINE_ASSEMBLY("" ::: "memory");
}

void granary_interrupts_enabled(void) {
  GRANARY_INLINE_ASSEMBLY("" ::: "memory");
}

}  // extern C
