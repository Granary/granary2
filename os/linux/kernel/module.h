/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_KERNEL_MODULE_H_
#define OS_LINUX_KERNEL_MODULE_H_

#ifdef __cplusplus
namespace granary {
extern "C" {
#endif  // __cplusplus

struct LinuxKernelModule {
  const char *name;

  enum {
    GRANARY_MODULE,
    KERNEL_MODULE,
    KERNEL
  } kind;

  int seen_by_granary;

  unsigned long core_text_begin;
  unsigned long core_text_end;

  unsigned long init_text_begin;
  unsigned long init_text_end;

  struct LinuxKernelModule *next;
};

#ifdef __cplusplus
}  // extern C
}  // namespace granary
#endif  // __cplusplus

#endif  // OS_LINUX_KERNEL_MODULE_H_
