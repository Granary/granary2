/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_KERNEL_ATTACH_CC_
#define OS_LINUX_KERNEL_ATTACH_CC_

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/context.h"
#include "granary/metadata.h"
#include "granary/tool.h"
#include "granary/translate.h"

#include "os/logging.h"

namespace granary {
extern "C" {

// Hook to attach granary to a system call function pointer.
void granary_attach_to_syscall(AppPC *func_pc, int syscall_number) {
  if (auto context = GlobalContext()) {
    *func_pc = TranslateEntryPoint(context, *func_pc, kEntryPointKernelSyscall,
                                   syscall_number, kTargetStackValid);
  }
}
}  // extern C

}  // namespace granary


#endif  // OS_LINUX_KERNEL_ATTACH_CC_
