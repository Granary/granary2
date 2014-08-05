/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_KERNEL_MODULE_SYMBOL_H_
#define OS_LINUX_KERNEL_MODULE_SYMBOL_H_

#ifndef MODULE
# define MODULE
#endif

#include <asm/syscall.h>

#include <linux/mutex.h>

extern void *(*linux_module_alloc)(unsigned long);
extern sys_call_ptr_t *linux_sys_call_table;
extern struct mutex *linux_module_mutex;

// Resolves needed symbols.
void ResolveSymbols(void);

#endif  // OS_LINUX_KERNEL_MODULE_SYMBOL_H_
