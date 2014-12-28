/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_KERNEL_TYPES_H_
#define OS_LINUX_KERNEL_TYPES_H_

#define __restrict
#define new new_
#define true true_
#define false false_
#define private private_
#define namespace namespace_
#define template template_
#define class class_
#define delete delete_
#define export export_
#define typeof decltype
#define this this_
#define typename typename_

#define bool K_bool
#define _Bool K_Bool

// Big hack: clang complains when a (named) struct is declared inside of an
// anonymous union. There is one such case: __raw_tickets, and it's not
// referenced by other types, so we will clobber it.
#define __raw_tickets

#define __KERNEL__

#if 0
#include <linux/version.h>
#include <linux/kconfig.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/tick.h>
#include <linux/pci.h>
#include <linux/sched.h>
#endif

// TODO(pag): Add more as needed.

// Remove these as they aren't compatible with C++.
#undef min
#undef max

#undef __restrict
#undef new
#undef true
#undef false
#undef private
#undef namespace
#undef template
#undef class
#undef delete
#undef export
#undef typeof
#undef this
#undef typename
#undef bool
#undef K_Bool
#undef __raw_tickets

#endif  // OS_LINUX_KERNEL_TYPES_H_
