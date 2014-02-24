/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_KERNEL_LINUX_MODULE_H_
#define GRANARY_KERNEL_LINUX_MODULE_H_

#ifdef __cplusplus
namespace granary {
extern "C" {
#endif  // __cplusplus

struct KernelModule {
  const char *name;

  enum {
    KERNEL_MODULE_GRANARY,
    KERNEL_MODULE_TOOL,
    KERNEL_MODULE,
    KERNEL
  } kind;

  int seen_by_granary;

  unsigned long core_text_begin;
  unsigned long core_text_end;

  unsigned long init_text_begin;
  unsigned long init_text_end;

  struct KernelModule *next;
};

#ifdef __cplusplus
}  // extern C
}  // namespace granary
#endif  // __cplusplus

#endif  // GRANARY_KERNEL_LINUX_MODULE_H_
