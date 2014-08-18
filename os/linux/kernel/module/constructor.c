/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

typedef void (*FuncPtr)(void);

// Defined by the linker script `linker.lds`.
extern FuncPtr granary_begin_init_array[];
extern FuncPtr granary_end_init_array[];

void RunConstructors(void) {
  FuncPtr *init_func = granary_begin_init_array;
  for (; init_func < granary_end_init_array; ++init_func) {
    (*init_func)();
  }
}
