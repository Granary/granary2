/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

    .file "nmi.asm"
START_FILE_INTEL

#ifdef GRANARY_WHERE_kernel

DEFINE_UINT64(granary_direct_edge_return_rip)
DEFINE_UINT64(granary_indirect_edge_return_rip)
DEFINE_UINT64(granary_os_nmi_handler)

DECLARE_FUNC(granary_arch_enter_direct_edge)
DECLARE_FUNC(granary_arch_enter_indirect_edge)

// Granary uses an NMI (non-maskable interrupt) handler for context switches
// in and out of Granary. This is because once an NMI is issues, the hardware
// queues all further interrupts. This isn't the case if we just disabled
// interrupts by modifying the Interrupt Flag (RFLAGS.IF).
DEFINE_FUNC(granary_nmi_edge_handler)
    pushfq
    push rax
    push rdx

    //          +56:    Zero-extended SS of interrupted task.
    //          +48:    RSP of interrupted task.
    //          +40:    RFLAGS of interrupted task.
    //          +32:    Zero-extended CS of interrupted task.
    //          +24:    RIP of interrupted task.
    //          +16:    Saved RFLAGS on entry to NMI.
    //          +8:     Saved RAX.
    //          +0:     Saved RDX.

    // Check that the low 3 bits of the interrupted task's SS and CS are both
    // from ring0.
    mov rax, [rsp + 56]
    or rax, [rsp + 32]
    and rax, 0x3
    jnz L(nmi_not_in_ring0)

    // Check to see if we're trying to do a direct edge.
    mov rax, [rsp + 24]
    cmp rax, [granary_direct_edge_return_rip]
    jz L(nmi_from_direct_edge)

    // Check to see if we're trying to do an indirect edge.
    cmp rax, [granary_indirect_edge_return_rip]
    jz L(nmi_from_indirect_edge)
    // Fall-through.

L(nmi_not_in_ring0):
    pop rdx
    pop rax
    // Fall-through.

L(defer_to_OS):
    popfq
    jmp qword ptr [granary_os_nmi_handler]

    // Handle a direct edge.
L(nmi_from_direct_edge):
    pop rdx
    pop rax
    call granary_arch_enter_direct_edge
    jmp L(return_to_code_cache)

    // Handle an indirect edge.
L(nmi_from_indirect_edge):
    pop rdx
    pop rax
    call granary_arch_enter_direct_edge
    // Fall-through.

L(return_to_code_cache):
    bt qword ptr [rsp], 13  // Test bit 13, the nested task (NT) flag.
    jc L(emulate_iret)      // NT = 1.
    // Fall-through.

L(return_iret):
    lea rsp, qword ptr [rsp + 8]  // Ignore saved RFLAGS.
    iret

L(emulate_iret):
    // Spill RAX and RDX once again; this clobbers our previously saved flags.
    mov [rsp], rax
    push rdx

    //          +48:    Zero-extended SS of interrupted task.
    //          +40:    RSP of interrupted task.
    //          +32:    RFLAGS of interrupted task.
    //          +24:    Zero-extended CS of interrupted task.
    //          +16:    RIP of interrupted task.
    //          +8:     Saved RFLAGS on entry to NMI.     (clobbered by RAX)
    // RSP ---> +0:     Saved RDX.

    // Note: We will assumes that the CS of the interrupted task matches that
    //       of the NMI handler.

    // Overwrite the interrupted task's stack, shifting it down and making
    // space for a return address.
    mov rax, qword ptr [rsp + 16]   // RAX <- task RIP
    mov rdx, qword ptr [rsp + 40]   // RDX <- task RSP
    lea rdx, [rdx - 8]              // Adjust task RSP for return address.
    mov [rdx], rax                  // Unsafe: Put task RIP as a return address.
    mov [rsp + 40], rdx             // Overwrite saved RSP w/ adjusted RSP.

    pop rdx                         // Restore RDX.
    pop rax                         // Restore RAX.
    lea rsp, qword ptr [rsp + 16]   // Shift off ISF's RIP and CS.
    popfq                           // Restore interrupted task's RFLAGS.

    //          +8:     SS of interrupted task.
    // RSP ---> +0:     Modified RSP of interrupted task.

    mov ss, word ptr [rsp + 8]      // TODO(pag): Probably wrong; consider LSS.
    mov rsp, qword ptr [rsp]
    ret

END_FUNC(granary_nmi_edge_handler)

#endif  // GRANARY_WHERE_kernel

END_FILE
