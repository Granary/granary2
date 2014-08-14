/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_KERNEL_MODULE_H_
#define OS_LINUX_KERNEL_MODULE_H_

#ifdef __cplusplus
namespace granary {
extern "C" {
#endif  // __cplusplus

// Mirror's the Linux kernel's exception table entry structure.
struct ExceptionTableEntry {
  int32_t fault_addr_rel32;
  int32_t fixup_addr_rel32;
};

struct ExceptionTableBounds {
  const struct ExceptionTableEntry *start;
  const struct ExceptionTableEntry *stop;
};

struct LinuxKernelModule {
  const char *name;

  enum {
    GRANARY_MODULE,
    KERNEL_MODULE,
    KERNEL
  } kind;

  void *module;

  unsigned long core_text_begin;
  unsigned long core_text_end;

  unsigned long init_text_begin;
  unsigned long init_text_end;

  struct LinuxKernelModule *next;

  struct ExceptionTableBounds exception_tables;
};

#ifdef __cplusplus
}  // extern C
}  // namespace granary
#endif  // __cplusplus

#endif  // OS_LINUX_KERNEL_MODULE_H_
