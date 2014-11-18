/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ENTRY_H_
#define GRANARY_ENTRY_H_

namespace granary {

enum EntryPointKind : unsigned {
  kEntryPointKernelSyscall = 0,
  kEntryPointKernelInterrupt,
  kEntryPointKernelModuleInit,
  kEntryPointKernelModuleExit,
  kEntryPointKernelModuleCallback,
  kEntryPointUserSignalHandler,
  kEntryPointUserAttach,
  kEntryPointTestCase
};

}  // namespace granary

#endif  // GRANARY_ENTRY_H_
