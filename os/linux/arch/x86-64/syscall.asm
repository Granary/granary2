/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE_INTEL

#ifdef GRANARY_WHERE_user

// A curious person might wonder: Why did I not prefix each of these functions
// with `granary_`, and instead went with the symbol versioning approach (by
// using `symver.map`). The answer is that
//
//      1)  I can achieve roughly the same effect with symbol versioning.
//      2)  I hope that applying static analysis tools to Granary's source code
//          will explicitly recognize the names of these `libc` system calls
//          and do specific checks based on their usage.
//
// A drawback of this approach is that the prefix approach with `granary_`
// makes it explicit that Granary has its own versions of each of these `libc`
// functions. Without this explicit prefix, a new developer of Granary might
// get confused into thinking that *any* `libc` function can be used.

DEFINE_FUNC(mmap)
    mov    r10,rcx
    mov    eax, 9  // `__NR_mmap`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_mmap_error)
    ret
L(granary_mmap_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(mmap)

DEFINE_FUNC(munmap)
    mov    eax, 11  // `__NR_munmap`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_munmap_error)
    ret
L(granary_munmap_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(munmap)

DEFINE_FUNC(mprotect)
    mov    eax, 10  // `__NR_mprotect`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_mprotect_error)
    ret
L(granary_mprotect_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(mprotect)

DEFINE_FUNC(mlock)
    mov    eax, 149  // `__NR_mlock`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_mlock_error)
    ret
L(granary_mlock_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(mlock)

DEFINE_FUNC(open)
    mov    eax, 2  // `__NR_open`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_open_error)
    ret
L(granary_open_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(open)

DEFINE_FUNC(close)
    mov    eax, 3  // `__NR_close`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_close_error)
    ret
L(granary_close_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(close)

DEFINE_FUNC(read)
    mov    eax, 0  // `__NR_read`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_read_error)
    ret
L(granary_read_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(read)

DEFINE_FUNC(write)
    mov    eax,1  // `__NR_write`.
    syscall
    cmp    rax,0xfffffffffffff001
    jae    L(granary_write_error)
    ret
L(granary_write_error):
    or     rax,0xffffffffffffffff
    ret
END_FUNC(write)

DEFINE_FUNC(getpid)
    mov    eax, 39  // `__NR_getpid`.
    syscall
    ret
END_FUNC(getpid)

DEFINE_FUNC(alarm)
    mov    eax, 37  // `__NR_alarm`.
    syscall
    ret
END_FUNC(alarm)

DEFINE_FUNC(setitimer)
    mov    eax, 38  // `__NR_setitimer`.
    syscall
    ret
END_FUNC(setitimer)

DEFINE_FUNC(rt_sigaction)
    mov     r10, rcx  // arg4, `sigsetsize`.
    jmp generic_sigaction
END_FUNC(rt_sigaction)

DEFINE_FUNC(generic_sigaction)
    mov     eax, 13  // `__NR_rt_sigaction`.
    syscall
    ret
END_FUNC(generic_sigaction)

DEFINE_FUNC(sigaltstack)
    mov     eax, 131  // `__NR_sigaltstack`.
    syscall
    ret
END_FUNC(sigaltstack)

DEFINE_FUNC(arch_prctl)
    mov     eax, 158  // `__NR_arch_prctl`.
    syscall
    ret
END_FUNC(arch_prctl)

DEFINE_FUNC(sys_clone)
    mov     r10, rcx  // arg4, `child_tidptr`.
    mov     eax, 56  // `__NR_clone`.
    syscall
    ret
END_FUNC(sys_clone)

DEFINE_FUNC(nanosleep)
    mov     eax, 35  // `__NR_nanosleep`.
    syscall
    ret
END_FUNC(nanosleep)

DECLARE_FUNC(granary_exit)
DEFINE_INST_FUNC(exit_group_ok)  // Can be called by instrumentation code.
    xor     rdi, rdi
    jmp     exit_group
END_FUNC(exit_group_ok)

DEFINE_FUNC(exit)
    jmp exit_group
END_FUNC(exit)

DEFINE_FUNC(_exit)
    jmp exit_group
END_FUNC(_exit)

DEFINE_FUNC(_Exit)
    jmp exit_group
END_FUNC(_Exit)

DEFINE_FUNC(exit_group)
    push    rdi
    xor     rdi, rdi  // `ExitReason::EXIT_PROGRAM`.
    call    granary_exit
    pop rdi
    mov     eax, 231  // `__NR_exit_group`.
    syscall
END_FUNC(exit_group)

DEFINE_INST_FUNC(rt_sigreturn)
    mov     eax, 15  // `__NR_rt_sigreturn`.
    syscall
END_FUNC(rt_sigreturn)

#endif  // GRANARY_WHERE_user

END_FILE
