/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ENTRY_H_
#define GRANARY_ENTRY_H_

namespace granary {

enum EntryPointKind {
  ENTRYPOINT_KERNEL_SYSCALL,
  ENTRYPOINT_KERNEL_INTERRUPT,
  ENTRYPOINT_MODULE_INIT,
  ENTRYPOINT_MODULE_EXIT,
  ENTRYPOINT_MODULE_CALLBACK,
  ENTRYPOINT_USER_SIGNAL,
  ENTRYPOINT_USER_LOAD
};

}  // namespace granary

#endif  // GRANARY_ENTRY_H_
