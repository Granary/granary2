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

#include "granary/kernel/module.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Peter Goodman");
MODULE_DESCRIPTION("Granary is a Linux kernel dynamic binary translator.");

// Initialize a new `KernelModule` from a `struct module`.
static struct KernelModule *init_kernel_module(struct KernelModule *kmod,
                                               const struct module *mod) {
  kmod->name = mod->name;
  kmod->kind = KERNEL_MODULE;
  kmod->seen_by_granary = 0;
  kmod->core_text_begin = (uintptr_t) mod->module_core;
  kmod->core_text_end = kmod->core_text_begin + mod->core_text_size;
  kmod->init_text_begin = 0;
  kmod->init_text_end = 0;
  if (MODULE_STATE_LIVE != mod->state && MODULE_STATE_GOING != mod->state) {
    kmod->init_text_begin = (uintptr_t) mod->module_init;
    kmod->init_text_begin = kmod->init_text_begin + mod->init_text_size;
  }
  if (mod == THIS_MODULE) {
    kmod->kind = KERNEL_MODULE_GRANARY;
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
  .init_text_end = 0
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

// Pointer to `__cxx_global_var_init`, if it exists and if `CONFIG_CONSTRUCTORS`
// is disabled.
static void (*granary_global_var_init)(void) = NULL;

// Find some internal kernel symbols.
static int find_symbols(void *data, const char *name,
                                struct module *mod, unsigned long addr) {
#ifndef CONFIG_CONSTRUCTORS
  if (NULL == granary_global_var_init &&
      THIS_MODULE == mod &&
      0 == strncmp("__cxx_global_var_init", name, 22)) {
    granary_global_var_init = (typeof(granary_global_var_init)) addr;
  }
#endif

  if (NULL != mod) {
    return 0;  // Only care about kernel symbols.
  }

  if (NULL == kernel_modules && 0 == strncmp("modules", name, 8)) {
    kernel_modules = (typeof(kernel_modules)) addr;
    return 0;
  }

  return 0;
}

// granary::LoadTools(char const*).
void _ZN7granary9LoadToolsEPKc(const char *tool_names) {
  if (!granary_global_var_init) {
    return;
  }
}

// granary::InitOptions(char const*)
extern void _ZN7granary11InitOptionsEPKc(const char *);

// granary::Init(granary::InitKind, char const*)
extern void _ZN7granary4InitENS_8InitKindEPKc(int, const char *);

enum {
  COMMAND_BUFF_SIZE = 4095
};

static int initialized = 0;
static int started = 0;
static char command_buff[COMMAND_BUFF_SIZE + 1] = {'\0'};

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

  // Start granary. Once all of the requested tools are loaded, Granary is
  // ready to start :-D
  } else if(initialized && !started && match_command("start")) {
    started = 1;
    printk("[granary] %s\n", command_buff);
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

static ssize_t write_output(struct file *file, char __user *str,
                            size_t size, loff_t *offset) {
  printk("[granary] Writing output.\n");
  (void) file; (void) str; (void) size; (void) offset;
  return 0;
}

static struct file_operations operations = {
    .owner      = THIS_MODULE,
    .write      = read_command,
    .read       = write_output
};

static struct miscdevice device = {  // Granary takes input from here.
    .minor      = 0,
    .name       = "granary",
    .fops       = &operations,
    .mode       = 0666
};

// Initialize Granary.
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

  if (NULL != granary_global_var_init) {
    //printk("[granary] Invoking global constructors.\n");
    //granary_global_var_init();
  }

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
