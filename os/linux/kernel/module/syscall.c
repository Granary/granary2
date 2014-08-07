/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>

#include <asm/syscall.h>
#include <asm/unistd.h>

extern sys_call_ptr_t *linux_sys_call_table;
static sys_call_ptr_t backup_sys_call_table[__NR_syscall_max];
static sys_call_ptr_t inst_sys_call_table[__NR_syscall_max];

extern void granary_attach_to_syscall(sys_call_ptr_t *, int);

extern int _ZN7granary4arch20TryDisableInterruptsEv(void);
extern void _ZN7granary4arch16EnableInterruptsEv(void);

extern int _ZN7granary4arch24TryDisablePageProtectionEv(void);
extern void _ZN7granary4arch20EnablePageProtectionEv(void);

void CopyNativeSyscallTable(void) {
  int i = 0;
  for (; i < __NR_syscall_max; ++i) {
    backup_sys_call_table[i] = linux_sys_call_table[i];
  }
}

void TakeoverSyscallTable(void) {
  int i;
  int enable_interrupts;
  int enable_prot;

  // Create an instrumented version of the syscall table based on the native
  // syscall table.
  for (i = 0; i < __NR_syscall_max; ++i) {
    inst_sys_call_table[i] = backup_sys_call_table[i];
    granary_attach_to_syscall(&(inst_sys_call_table[i]), i);
  }

  // Take over the kernel's system call table.
  enable_interrupts = _ZN7granary4arch20TryDisableInterruptsEv();
  enable_prot = _ZN7granary4arch24TryDisablePageProtectionEv();
  for (i = 0; i < __NR_syscall_max; ++i) {
    linux_sys_call_table[i] = inst_sys_call_table[i];
  }
  if (enable_prot) _ZN7granary4arch20EnablePageProtectionEv();
  if (enable_interrupts) _ZN7granary4arch16EnableInterruptsEv();
}

void RestoreNativeSyscallTable(void) {
  int i;
  int enable_interrupts = _ZN7granary4arch20TryDisableInterruptsEv();
  int enable_prot = _ZN7granary4arch24TryDisablePageProtectionEv();
  for (i = 0; i < __NR_syscall_max; ++i) {
    linux_sys_call_table[i] = backup_sys_call_table[i];
  }
  if (enable_prot) _ZN7granary4arch20EnablePageProtectionEv();
  if (enable_interrupts) _ZN7granary4arch16EnableInterruptsEv();
}
