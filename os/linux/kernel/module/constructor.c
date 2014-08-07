/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/printk.h>

typedef void (*FuncPtr)(void);

// Defined by the linker script `os/linux/kernel/constructor.lds`.
extern FuncPtr granary_begin_init_array[];
extern FuncPtr granary_end_init_array[];

void RunConstructors(void) {
#ifndef CONFIG_CONSTRUCTORS
  FuncPtr *init_func = granary_begin_init_array;
  for (; init_func < granary_end_init_array; ++init_func) {
    (*init_func)();
  }
#endif
}
