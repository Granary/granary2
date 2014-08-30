/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

    .file "nmi.asm"
START_FILE_INTEL

#ifdef GRANARY_WHERE_kernel

DEFINE_UINT64(granary_direct_edge_return_rip)
DEFINE_UINT64(granary_indirect_edge_return_rip)
DEFINE_UINT64(granary_nmi_handler)

// Granary uses an NMI (non-maskable interrupt) handler for context switches
// in and out of Granary. This is because once an NMI is issues, the hardware
// queues all further interrupts. This isn't the case if we just disabled
// interrupts by modifying the Interrupt Flag (RFLAGS.IF).
DEFINE_FUNC(granary_nmi_edge_handler)
    pushfq
    push rax
    push rdx

    //          +48:    SS of interrupted task.
    //          +40:    RSP of interrupted task.
    //          +32:    RFLAGS of interrupted task.
    //          +24:    Zero-extended CS of interrupted task.
    //          +16:    RIP of interrupted task.
    //          +8:     Error code.
    //          +0:     RFLAGS on entry to NMI.
    //          +
    //          +0:     Saved RDX

    mov rax, [rsp

L(native_nmi):
    popfq
    jmp qword ptr [granary_nmi_handler]

L(return_to_code_cache):
    bt qword ptr [rsp], 13  // Test bit 13, the nested task (NT) flag.
    jb L(emulate_iret)

L(return_iret):
    lea rsp, [rsp + 16]  // Pop flags and error code.
    iretq

L(emulate_iret):
    // Spill RAX and RDX.
    mov [rsp + 8], rax
    mov [rsp], rdx

    //          +48:    Zero-extended SS of interrupted task.
    //          +40:    RSP of interrupted task.
    //          +32:    RFLAGS of interrupted task.
    //          +24:    Zero-extended CS of interrupted task.
    //          +16:    RIP of interrupted task.
    //          +8:     Error code.                 (clobber with RAX)
    // RSP ---> +0:     RFLAGS on entry to NMI.     (clobber with RDX)

    // Note: We will assumes that the CS of the interrupted task matches that
    //       of the NMI handler.

    // Overwrite the interrupted task's stack, shifting it down and making
    // space for a return address.
    mov rax, qword ptr [rsp + 16]   // RAX <- task RIP
    mov rdx, qword ptr [rsp + 40]   // RDX <- task RSP
    lea rdx, [rdx - 8]              // Shift task RSP down.
    mov [rdx], rax                  // Put task RIP as a return address.
    mov [rsp + 40], rdx             // Overwrite saved RSP of task.

    pop rdx                         // Restore RDX.
    pop rax                         // Restore RAX.
    lea rsp, [rsp + 16]             // Shift off most of the ISF.
    popfq                           // Restore interrupted task's RFLAGS.

    //          +8:     SS of interrupted task.
    // RSP ---> +0:     Modified RSP of interrupted task.


    mov ss, [rsp + 8]
    mov rsp, [ds:rsp]
    ret

END_FUNC(granary_nmi_edge_handler)

#endif  // GRANARY_WHERE_kernel

END_FILE
