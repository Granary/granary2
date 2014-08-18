/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE

    .intel_syntax noprefix

#ifdef GRANARY_WHERE_user

DEFINE_FUNC(granary_mmap)
    mov    r10,rcx
    mov    eax,0x9
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_mmap_error
    ret
.Lgranary_mmap_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_mmap)

DEFINE_FUNC(granary_munmap)
    mov    eax,0xb
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_munmap_error
    ret
.Lgranary_munmap_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_munmap)

DEFINE_FUNC(granary_mprotect)
    mov    eax,0xa
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_mprotect_error
    ret
.Lgranary_mprotect_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_mprotect)

DEFINE_FUNC(granary_mlock)
    mov    eax,0x95
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_mlock_error
    ret
.Lgranary_mlock_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_mlock)

DEFINE_FUNC(granary_open)
    mov    eax,0x2
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_open_error
    ret
.Lgranary_open_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_open)

DEFINE_FUNC(granary_close)
    mov    eax,0x3
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_close_error
    ret
.Lgranary_close_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_close)

DEFINE_FUNC(granary_read)
    mov    eax,0x0
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_read_error
    ret
.Lgranary_read_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_read)

DEFINE_FUNC(granary_write)
    mov    eax,0x1
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_write_error
    ret
.Lgranary_write_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(granary_write)

DEFINE_FUNC(granary_getpid)
    mov    eax,0x27
    syscall
    ret
END_FUNC(granary_getpid)

// `exit_group` system call.
.section .text.inst_exports
.global granary_exit_group
.type granary_exit_group, @function
granary_exit_group:
    .cfi_startproc
    mov     eax,0xE7
    xor     rdi, rdi
    syscall
    ud2 /* Should not be reached */
    .cfi_endproc

#endif

END_FILE
