/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_SOFTDEP("pre: granary");

int init_module(void) {

  return 0;
}

void exit_module(void) { }
