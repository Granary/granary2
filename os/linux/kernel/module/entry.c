/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>

#ifndef CONFIG_MODULES
# error "Module auto-loading must be supported (`CONFIG_MODULES`)."
#endif

#ifndef CONFIG_KALLSYMS_ALL
# error "All symbols must be included in kallsyms (`CONFIG_KALLSYMS_ALL`)."
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Peter Goodman <pag@cs.toronto.edu>");
MODULE_DESCRIPTION("Granary is a Linux kernel dynamic binary translator.");

extern void ResolveSymbols(void);
extern void RunConstructors(void);
extern void InitCommandListener(void);
extern void CopyNativeSyscallTable(void);

// Initialize the Granary kernel module.
static int granary_enter(void) {
  ResolveSymbols();
  RunConstructors();
  InitCommandListener();
  CopyNativeSyscallTable();
  printk("[granary] Loaded Granary.\n");
  return 0;
}

module_init(granary_enter);
