/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/kallsyms.h>
#include <linux/list.h>

#include <asm/syscall.h>

#include "os/linux/kernel/module.h"

struct mutex;

struct SymbolResolver {
  const char *name;
  unsigned long *addr;
};

#define RESOLVE_SYM(name) \
    {#name, (unsigned long *) (void *) &linux_ ## name}

void *(*linux_module_alloc)(unsigned long) = NULL;
sys_call_ptr_t *linux_sys_call_table = NULL;
struct mutex *linux_module_mutex = NULL;
struct list_head *linux_modules = NULL;
void __percpu *(*linux___alloc_reserved_percpu)(size_t, size_t) = NULL;
struct ExceptionTableEntry *linux___start___ex_table = NULL;
struct ExceptionTableEntry *linux___stop___ex_table = NULL;

static struct SymbolResolver symbols[] = {
  RESOLVE_SYM(module_alloc),
  RESOLVE_SYM(sys_call_table),
  RESOLVE_SYM(module_mutex),
  RESOLVE_SYM(modules),
  RESOLVE_SYM(__alloc_reserved_percpu),
  RESOLVE_SYM(__start___ex_table),
  RESOLVE_SYM(__stop___ex_table)
};

// Resolves needed symbols.
void ResolveSymbols(void) {
  size_t i = 0;
  for (; i < (sizeof(symbols) / sizeof(symbols[0])); ++i) {
    *(symbols[i].addr) = kallsyms_lookup_name(symbols[i].name);
  }
}
