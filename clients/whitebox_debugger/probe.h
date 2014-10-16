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
#define __ADD_WATCHPOINT(addr, meta, func, category, kind) \
  __asm__ __volatile__ (\
    "leaq   %[alloc_addr],    %%rdi \n" \
    "movq   %[alloc_meta],    %%rsi\n" \
    "movq   %[callback_func], %%rdx \n" \
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
      [alloc_meta] "g"(meta), \
      [callback_func] "g"(func) \
    : "%rdi", "%rsi", "%rdx", "memory" \
  )

#define ADD_READ_WATCHPOINT(addr, func) \
  __ADD_WATCHPOINT(addr, 0, func, 0, 1)

#define ADD_WRITE_WATCHPOINT(addr, func) \
  __ADD_WATCHPOINT(addr, 0, func, 0, 2)

#define ADD_RW_WATCHPOINT(addr, func) \
  __ADD_WATCHPOINT(addr, 0, func, 0, 3)

#define ADD_META_READ_WATCHPOINT(addr, meta, func) \
  __ADD_WATCHPOINT(addr, meta, func, 0, 1)

#define ADD_META_WRITE_WATCHPOINT(addr, meta, func) \
  __ADD_WATCHPOINT(addr, meta, func, 0, 2)

#define ADD_META_RW_WATCHPOINT(addr, meta, func) \
  __ADD_WATCHPOINT(addr, meta, func, 0, 3)

#define REMOVE_WATCHPOINT(addr, func) \
  __asm__ __volatile__ (\
    "leaq   %[dealloc_addr],    %%rdi \n" \
    "movq   %[remove_func],     %%rsi \n" \
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
    : [dealloc_addr] "m"(addr), \
      [remove_func] "g"(func) \
    : "%rdi", "%rsi", "%rdx", "memory" \
  )

#endif  // CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_
