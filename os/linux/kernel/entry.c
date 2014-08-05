/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/uaccess.h>

#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/kallsyms.h>
#include <linux/miscdevice.h>

#include <linux/percpu.h>
#include <linux/percpu-defs.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/slab.h>

#include "os/linux/kernel/module.h"

#ifndef CONFIG_MODULES
# error "Module auto-loading must be supported (`CONFIG_MODULES`)."
#endif

#ifndef CONFIG_KALLSYMS_ALL
# error "All symbols must be included in kallsyms (`CONFIG_KALLSYMS_ALL`)."
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Peter Goodman <pag@cs.toronto.edu>");
MODULE_DESCRIPTION("Granary is a Linux kernel dynamic binary translator.");

extern struct LinuxKernelModule *granary_kernel_modules;

// Initialize the Granary kernel module.
static int granary_enter(void) {
  printk("[granary] Entering Granary.\n");
  return 0;
}

// Exit Granary.
static void granary_exit(void) {
  printk("[granary] Exiting Granary.\n");
}

module_init(granary_enter);
module_exit(granary_exit);
