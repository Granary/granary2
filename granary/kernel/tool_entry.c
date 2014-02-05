/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

#include "granary.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_SOFTDEP("pre: granary");

// Find some internal kernel symbols.
static int init_globals(void *data, const char *name,
                        struct module *mod, unsigned long addr) {
  if (THIS_MODULE == mod) {
    if (NULL != strstr(name, "_GLOBAL__I_")) {
      ((void (*)(void)) addr)();
    }
  }
  return 0;
}

int init_module(void) {
  kallsyms_on_each_symbol(&init_globals, NULL);
  return 0;
}

void exit_module(void) { }
