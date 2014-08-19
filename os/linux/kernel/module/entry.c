/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

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

#ifndef CONFIG_SMP
# error "Kernel must be compiled with `CONFIG_SMP`. Note: This is because " \
        "the slots mechanism currently uses `GS` for accessing CPU-private " \
        "memory. If `CONFIG_SMP` is disabled, then implement SlotMemOp " \
        "using global memory."
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Peter Goodman <pag@cs.toronto.edu>");
MODULE_DESCRIPTION("Granary is a Linux kernel dynamic binary translator.");

extern void ResolveSymbols(void);
extern void _ZN7granary7PreInitEv(void);  // `granary::PreInit()`.
extern void CopyNativeSyscallTable(void);
extern void InitCommandListener(void);

// Initialize the Granary kernel module.
static int granary_enter(void) {
  ResolveSymbols();
  _ZN7granary7PreInitEv();
  CopyNativeSyscallTable();
  InitCommandListener();
  printk("[granary] Loaded Granary.\n");
  return 0;
}

module_init(granary_enter);
