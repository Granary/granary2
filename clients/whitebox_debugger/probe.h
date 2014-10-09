/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_
#define CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_

#ifdef __cplusplus
# include <cstdint>
# include <cstddef>
#else
# include <stdint.h>
# include <stddef.h>
#endif  //  __cplusplus

// Adds a watchpoint probe to the code.
#define __ADD_WATCHPOINT(addr, size, func, category, kind) \
  __asm__ __volatile__ (\
    "leaq   %[alloc_addr],    %%r15 \n" \
    "movq   %[alloc_size],    %%r14 \n" \
    "movq   %[callback_func], %%r13 \n" \
    "1: \n" \
    ".pushsection .granary_probes,\"a\" \n" \
    ".balign 8 \n" \
    ".long " #category " \n" \
    ".long " #kind " \n" \
    ".quad 1b \n" \
    ".popsection \n" \
    "2: \n" \
    "movq   %[alloc_addr],    %[new_alloc_addr] \n" \
    : [new_alloc_addr] "=r"(addr) \
    : [alloc_addr] "m"(addr), \
      [alloc_size] "g"(size), \
      [callback_func] "g"(func) \
    : "%r15", "%r14", "%r13", "memory" \
  )

#define ADD_READ_WATCHPOINT(addr, size, func) \
  __ADD_WATCHPOINT(addr, size, func, 0, 1)

#define ADD_WRITE_WATCHPOINT(addr, size, func) \
  __ADD_WATCHPOINT(addr, size, func, 0, 2)

#define ADD_RW_WATCHPOINT(addr, size, func) \
  __ADD_WATCHPOINT(addr, size, func, 0, 3)

#endif  // CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_
