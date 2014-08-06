/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/printk.h>

// Defined by the linker script `os/linux/kernel/constructor.lds`.
extern void (**granary_begin_init_array)(void);
extern void (**granary_end_init_array)(void);

void RunConstructors(void) {
#ifndef CONFIG_CONSTRUCTORS
  void (**init_func)(void) = granary_begin_init_array;
  for (; init_func < granary_end_init_array; ++init_func) {
    (*init_func)();
  }
#endif
}
