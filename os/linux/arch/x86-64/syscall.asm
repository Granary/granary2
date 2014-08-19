/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE

    .intel_syntax noprefix

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
    mov    eax,0x9
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_mmap_error
    ret
.Lgranary_mmap_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(mmap)

DEFINE_FUNC(munmap)
    mov    eax,0xb
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_munmap_error
    ret
.Lgranary_munmap_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(munmap)

DEFINE_FUNC(mprotect)
    mov    eax,0xa
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_mprotect_error
    ret
.Lgranary_mprotect_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(mprotect)

DEFINE_FUNC(mlock)
    mov    eax,0x95
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_mlock_error
    ret
.Lgranary_mlock_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(mlock)

DEFINE_FUNC(open)
    mov    eax,0x2
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_open_error
    ret
.Lgranary_open_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(open)

DEFINE_FUNC(close)
    mov    eax,0x3
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_close_error
    ret
.Lgranary_close_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(close)

DEFINE_FUNC(read)
    mov    eax,0x0
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_read_error
    ret
.Lgranary_read_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(read)

DEFINE_FUNC(write)
    mov    eax,0x1
    syscall
    cmp    rax,0xfffffffffffff001
    jae    .Lgranary_write_error
    ret
.Lgranary_write_error:
    or     rax,0xffffffffffffffff
    ret
END_FUNC(write)

DEFINE_FUNC(getpid)
    mov    eax,0x27
    syscall
    ret
END_FUNC(getpid)

// `exit_group` system call.
.section .text.inst_exports
.global exit_group
.type exit_group, @function
exit_group:
    .cfi_startproc
    mov     eax,0xE7
    xor     rdi, rdi
    syscall
    ud2 /* Should not be reached */
    .cfi_endproc

#endif

END_FILE
