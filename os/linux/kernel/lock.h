/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_KERNEL_LOCK_H_
#define OS_LINUX_KERNEL_LOCK_H_

#include "granary/base/lock.h"

namespace granary {
namespace os {

// TODO(pag): Re-implement using `HLT`.
typedef SpinLock Lock;

}  // namespace os
}  // namespace granary

#endif  // OS_LINUX_KERNEL_LOCK_H_
