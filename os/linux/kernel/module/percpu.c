/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/smp.h>

#include "dependencies/drk/descriptor.h"

struct CPUState {
  system_table_register_t instrumented_idtr;
  system_table_register_t gdtr;
  system_table_register_t idtr;
};

struct CPUState *granary_cpu_states = NULL;

static void InitEachCPUState(void *null) {
  int cpu_id = raw_smp_processor_id();
  struct CPUState *cpu_state = &(granary_cpu_states[cpu_id]);

  get_idtr(&(cpu_state->idtr));
  get_gdtr(&(cpu_state->gdtr));
  (void) null;
}

void InitPerCPUState(void) {
  int num_cpus = num_possible_cpus();
  granary_cpu_states = alloc_pages_exact(num_cpus * sizeof(struct CPUState),
                                         GFP_KERNEL);
  on_each_cpu(InitEachCPUState, NULL, 1);
}
#endif
