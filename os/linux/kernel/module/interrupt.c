/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>

#include <asm/syscall.h>
#include <asm/unistd.h>

#include "dependencies/drk/descriptor.h"

enum {
  NUM_INTERRUPT_VECTORS = 256
};

// An interrupt descriptor table.
struct IDT {
  descriptor_t vectors[2 * NUM_INTERRUPT_VECTORS * sizeof(descriptor_t)];
} __attribute__((aligned (4096)));

// Used to share IDTs across multiple CPUs.
static system_table_register_t backup_idt = {0, NULL};
static system_table_register_t inst_idt = {0, NULL};

