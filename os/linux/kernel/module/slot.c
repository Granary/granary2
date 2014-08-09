/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/threads.h>

extern uintptr_t * __percpu granary_slots;
extern void __percpu *(*linux___alloc_reserved_percpu)(size_t, size_t);

struct GranaryStack {
  char data[4096 * 4];
};

static unsigned long curr_stack = 0;
static struct GranaryStack *cpu_stacks = NULL;

// Assign the private stack pointer into the CPU-private slots.
//
// Note: We use `cpu_id + 1` instead of `cpu_id` because the stack grows down,
//       not up. Therefore, we want the "entry" stack pointer to point to the
//       end of `data` and not to the beginning.
static void AssignPrivateStack(void *info) {
  unsigned long cpu_id = __sync_fetch_and_add(&curr_stack, 1UL);
  uintptr_t *slots = get_cpu_ptr(granary_slots);
  *slots = (uintptr_t) &(cpu_stacks[cpu_id + 1]);
  put_cpu_ptr(granary_slots);
  (void) info;
}

static void AllocatePrivateStacks(void) {
  cpu_stacks = alloc_pages_exact(
      num_possible_cpus() * sizeof(struct GranaryStack),
      GFP_KERNEL);
}

static void AllocateCPUSlots(void) {
  long slot_ptr;

  // This needs to agree with sizeof(SlotSet) in `os/slot.h`.
  granary_slots = linux___alloc_reserved_percpu(264, 16);

  slot_ptr = (long) granary_slots;
  if (0 > slot_ptr) slot_ptr = -slot_ptr;

  // We depend on the CPU-private pointer being representable by a 32-bit signed
  // value, as this is the limit of an offset from a segment register.
  //
  // TODO(pag): This is x86 specific.
  BUG_ON(slot_ptr >= INT_MAX);
}

void InitSlots(void) {
  AllocateCPUSlots();
  AllocatePrivateStacks();
  on_each_cpu(AssignPrivateStack, NULL, 1 /* wait */);
  BUG_ON(curr_stack != num_possible_cpus());
}
