/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/kallsyms.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>

#include "granary/kernel/linux/module.h"

#ifndef CONFIG_MODULES
# error "Module auto-loading must be supported (`CONFIG_MODULES`)."
#endif

#ifndef CONFIG_KALLSYMS_ALL
# error "All symbols must be included in kallsyms (`CONFIG_KALLSYMS_ALL`)."
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Peter Goodman <peter.goodman@gmail.com>");
MODULE_DESCRIPTION("Granary is a Linux kernel dynamic binary translator.");

// Initialize a new `KernelModule` from a `struct module`. A `KernelModule`
// is a stripped down `struct module` that contains enough information for
// Granary to create its own `Module` structure from.
static struct KernelModule *init_kernel_module(struct KernelModule *kmod,
                                               const struct module *mod) {
  kmod->name = mod->name;
  kmod->kind = KERNEL_MODULE;
  kmod->seen_by_granary = 0;
  kmod->core_text_begin = (uintptr_t) mod->module_core;
  kmod->core_text_end = kmod->core_text_begin + mod->core_text_size;
  kmod->init_text_begin = 0;
  kmod->init_text_end = 0;
  kmod->next = NULL;
  if (MODULE_STATE_LIVE != mod->state && MODULE_STATE_GOING != mod->state) {
    kmod->init_text_begin = (uintptr_t) mod->module_init;
    kmod->init_text_begin = kmod->init_text_begin + mod->init_text_size;
  }
  if (mod == THIS_MODULE) {
    kmod->kind = GRANARY_MODULE;
  }
  return kmod;
}

// Treat the kernel as one large module.
static struct KernelModule GRANARY_KERNEL = {
  .name = "kernel",
  .kind = KERNEL,
  .seen_by_granary = 0,
  .core_text_begin = 0xffffffff80000000ULL,
  .core_text_end = 0xffffffffa0000000ULL,
  .init_text_begin = 0,
  .init_text_end = 0,
  .next = NULL
};

// Global variable, shared with granary.
struct KernelModule *GRANARY_KERNEL_MODULES = &GRANARY_KERNEL;

// The kernel's internal module list. Guarded by `modules_lock`.
static struct list_head *kernel_modules = NULL;

// Wrapper around iterating over all modules. Granary's InitModules function
// uses this function to determine the current set of modules
static void init_module_list(void) {
  struct module *mod = NULL;
  struct KernelModule **next_ptr = &(GRANARY_KERNEL.next);
  struct KernelModule *kmod = NULL;
  int num_modules = 0;
  int i = 0;

  mutex_lock(&module_mutex);
  list_for_each_entry_rcu(mod, kernel_modules, list) {
    ++num_modules;
  }

  for (i = 0; i < num_modules; ++i) {
    *next_ptr = kmalloc(sizeof(struct KernelModule), GFP_NOWAIT);
    next_ptr = &((*next_ptr)->next);
  }

  kmod = GRANARY_KERNEL.next;  // The first module.
  list_for_each_entry_rcu(mod, kernel_modules, list) {
    --num_modules;
    if (NULL == kmod) {
      break;
    }
    init_kernel_module(kmod, mod);
    kmod = kmod->next;
  }
  mutex_unlock(&module_mutex);

  // It would be unusual if the number of modules changed while holding
  // the `module_mutex`.
  BUG_ON(0 > num_modules);
}

// Find some internal kernel symbols.
static int find_symbols(void *data, const char *name,
                        struct module *mod, unsigned long addr) {
  if (THIS_MODULE == mod) {

    // If we find a constructor for some global Granary data, then invoke it.
    // This is pretty ugly. A minor hack is used to handle command-line
    // options, which is to add `_GLOBAL__I_` into the constructor name of those
    // options to ensure that this method catches those calls.
    if (NULL != strstr(name, "_GLOBAL__I_")) {
      ((void (*)(void)) addr)();
    }

  // We don't care (for now?) about other modules.
  } else if (NULL != mod) {
    return 0;

  // Get a pointer the kernel's `struct list_head modules` so that we can later
  // iterate over the list of all modules. Luckily `modules_lock` is exported.
  } else if (NULL == kernel_modules && 0 == strncmp("modules", name, 8)) {
    kernel_modules = (typeof(kernel_modules)) addr;
    return 0;
  }

  return 0;
}

// granary::Log(granary::LogLevel, char const*, ...)
int _ZN7granary3LogENS_8LogLevelEPKcz(int log_level, const char *format, ...) {
  (void) log_level;
  (void) format;
  return 0;
}

// granary::InitOptions(char const*)
extern void _ZN7granary11InitOptionsEPKc(const char *);

// granary::Init(granary::InitKind, char const*)
extern void _ZN7granary4InitENS_8InitKindEPKc(int, const char *);

enum {
  COMMAND_BUFF_SIZE = 4095
};

// Has Granary been initialized?
static int initialized = 0;

// Buffer for storing commands issued from user space. For example, if one does
//    `echo "init --tools=follow_jumps,print_bbs" > /dev/granary`
// Then `command_buff` will contain `init --tools=follow_jumps,print_bbs`.
static char command_buff[COMMAND_BUFF_SIZE + 1] = {'\0'};

// Try to match a command in the command buffer.
static int match_command(const char *command) {
  return command_buff == strstr(command_buff, command);
}

// Process a Granary command. Commands are written to `/dev/granary_in`.
static void process_command(void) {

  // Initialize granary. This is used to set the initial options of Granary so
  // that it can go and load in some tools.
  if (!initialized && match_command("init")) {
    initialized = 1;
    printk("[granary] %s\n", command_buff);
    _ZN7granary11InitOptionsEPKc(&(command_buff[4]));
    init_module_list();
    _ZN7granary4InitENS_8InitKindEPKc(0, "");
  }
}

// A user space program wrote a command to Granary. We will assume that we can
// only process one command at a time.
static ssize_t read_command(struct file *file, const char __user *str,
                            size_t size, loff_t *offset) {
  printk("[granary] Reading command.\n");
  memset(&(command_buff), 0, COMMAND_BUFF_SIZE);
  copy_from_user(
      &(command_buff[0]),
      str,
      size > COMMAND_BUFF_SIZE ? COMMAND_BUFF_SIZE : size);
  command_buff[COMMAND_BUFF_SIZE] = '\0';

  process_command();

  (void) file; (void) offset;
  return size;
}

// TODO(pag): Output granary::Log to here.
static ssize_t write_output(struct file *file, char __user *str,
                            size_t size, loff_t *offset) {
  printk("[granary] Writing output.\n");
  (void) file; (void) str; (void) size; (void) offset;
  return 0;
}

// File operations on `/dev/granary`.
static struct file_operations operations = {
    .owner      = THIS_MODULE,
    .write      = read_command,
    .read       = write_output
};

// Simple character-like device for Granary and user space to communicate.
static struct miscdevice device = {
    .minor      = 0,
    .name       = "granary",
    .fops       = &operations,
    .mode       = 0666
};

// Initialize the Granary kernel module.
static int granary_init(void) {
  int ret = 0;

  printk("[granary] Initializing.\n");
  printk("[granary] Finding internal kernel symbols.\n");

  // Try to find some internal symbols.
  ret = kallsyms_on_each_symbol(&find_symbols, NULL);
  if (ret) {
    printk("[granary] Something broke :-/\n");
    return ret;
  }

  BUG_ON(NULL == kernel_modules);

  ret = misc_register(&device);
  if(0 != ret) {
    printk("[granary] Unable to register `/dev/granary_in`.\n");
    return ret;
  }

  printk("[granary] Initialized.\n");
  return ret;
}

// Exit Granary.
static void granary_exit(void) {
  misc_deregister(&device);
  printk("[granary] Exiting Granary.\n");
}

module_init(granary_init);
module_exit(granary_exit);
