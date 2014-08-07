/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/printk.h>
#include <linux/string.h>

#include <asm/uaccess.h>

enum {
  COMMAND_BUFF_SIZE = 4095
};

// Buffer for storing commands issued from user space. For example, if one does
//    `echo "init --tools=follow_jumps,print_bbs" > /dev/granary`
// Then `command_buff` will contain `init --tools=follow_jumps,print_bbs`.
static char command_buff[COMMAND_BUFF_SIZE + 1] = {'\0'};

static int seen_init = 0;
static int seen_attach = 0;

static int MatchCommand(const char *command, const char *key) {
  return strstr(command, key) == command;
}

// Initialize Granary.
extern void _ZN7granary4InitEv(void);
extern void _ZN7granary11InitOptionsEPKc(const char *);
static void ProcessInit(const char *options) {
  _ZN7granary11InitOptionsEPKc(options);  // `granary::InitOptions`.
  _ZN7granary4InitEv();  // `granary::Init`.
  printk("[granary] Initialized.\n");
}

// Attach Granary to the kernel.
extern void TakeoverSyscallTable(void);
static void ProcessAttach(void) {
  TakeoverSyscallTable();
  printk("[granary] Attached.\n");
}

// Detach Granary.
extern void RestoreNativeSyscallTable(void);
static void ProcessDetach(void) {
  RestoreNativeSyscallTable();
  printk("[granary] Detached.\n");
}

// Process commands. Commands are written to `/dev/granary`.
static void ProcessCommand(const char *command) {
  if (MatchCommand(command, "init")) {
    if (!seen_init) {
      seen_init = 1;
      ProcessInit(command + 4);
    }
  } else if (MatchCommand(command, "attach")) {
    if (seen_init && !seen_attach) {
      seen_attach = 1;
      ProcessAttach();
    }
  } else if (MatchCommand(command, "detach")) {
    if (seen_attach) {
      seen_attach = 0;
      ProcessDetach();
    }
  }
}

// A user space program wrote a command to Granary. We will assume that we can
// only process one command at a time.
static ssize_t ParseCommand(struct file *file, const char __user *str,
                            size_t size, loff_t *offset) {
  memset(&(command_buff), 0, COMMAND_BUFF_SIZE);
  copy_from_user(
      &(command_buff[0]),
      str,
      size > COMMAND_BUFF_SIZE ? COMMAND_BUFF_SIZE : size);
  command_buff[COMMAND_BUFF_SIZE] = '\0';

  ProcessCommand(&(command_buff[0]));

  (void) file; (void) offset;
  return (ssize_t) size;
}

static ssize_t DumpLog(struct file *file, char __user *str, size_t size,
                       loff_t *offset) {
  (void) file; (void) str; (void) size; (void) offset;
  return 0;
}

// File operations on `/dev/granary`.
static struct file_operations operations = {
    .owner      = THIS_MODULE,
    .write      = ParseCommand,
    .read       = DumpLog
};

// Simple character-like device for Granary and user space to communicate.
static struct miscdevice device = {
    .minor      = 0,
    .name       = "granary",
    .fops       = &operations,
    .mode       = 0666
};

void InitCommandListener(void) {
  if(0 != misc_register(&device)) {
    printk("[granary] Unable to register `/dev/granary`.\n");
  } else {
    printk("[granary] Listening to commands on `/dev/granary`.\n");
  }
}
