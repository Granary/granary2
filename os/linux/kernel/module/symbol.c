/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "os/linux/kernel/module/symbol.h"

#include <linux/kallsyms.h>

struct SymbolResolver {
  const char *name;
  unsigned long *addr;
};

#define RESOLVE_SYM(name) \
    {#name, (unsigned long *) (void *) &linux_ ## name}

void *(*linux_module_alloc)(unsigned long) = NULL;
sys_call_ptr_t *linux_sys_call_table = NULL;
struct mutex *linux_module_mutex = NULL;

static struct SymbolResolver symbols[] = {
  RESOLVE_SYM(module_alloc),
  RESOLVE_SYM(sys_call_table),
  RESOLVE_SYM(module_mutex)
};

// Resolves needed symbols.
void ResolveSymbols(void) {
  size_t i = 0;
  for (; i < (sizeof(symbols) / sizeof(symbols[0])); ++i) {
    *(symbols[i].addr) = kallsyms_lookup_name(symbols[i].name);
  }
}
