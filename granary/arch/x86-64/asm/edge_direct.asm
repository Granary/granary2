
#ifdef EDGE_FUNC_NAME

// Context switch into granary. This is used by `edge.asm` to generate a
// profiled and an unprofiled version of the edge entrypoint code. The profiled
// version will increment edge counters, whereas the unprofiled version will
// not.
DEFINE_FUNC(EDGE_FUNC_NAME)
    // Save the flags.
    pushfq

    // Disable interrupts. This still leaves two opportunities for interruption.
    //      1) In the edge code, after the stack switch, and in this function
    //         before the `CLI`.
    //      2) In this code after the `POPF`, and in the edge code before the
    //         stack switch back to native.
    GRANARY_IF_KERNEL( cli )

    // Double-check to see if we've actually already resolved this target. If
    // so, then don't go all the way into Granary.
    //
    // Note: `ARG1` was stolen by the direct edge code and saved into
    //       `%EDGE_SLOTS:EDGE_SLOT_ARG1(%ARG2)`. The value of `ARG1` at this
    //       point is a pointer to a `DirectEdge` data structure, whose first
    //       8 bytes are the value of the cached target location in memory.
    //       Initially those 8 bytes are 0, meaning we have not computed the
    //       target.
    testq   $0xFFFFFFFFFFFFFFFF, (%ARG1)
#ifdef EDGE_PROFILED
    jz      .Ltranslate_block ## EDGE_FUNC_NAME

    // If we've already translated the target block, then increment the
    // execution counter. This is a saturating counter, where one counter
    // counts up to 32 bits, and the other counts the number of overflows.
    lock incl   8(%ARG1)
    jno         .Lback_to_code_cache ## EDGE_FUNC_NAME

    // An unlikely event, so no need to lock the cache line.
    incl    12(%ARG1)
    jmp     .Lback_to_code_cache ## EDGE_FUNC_NAME
#else
    jnz     .Lback_to_code_cache ## EDGE_FUNC_NAME
#endif  // EDGE_PROFILED

  .Ltranslate_block ## EDGE_FUNC_NAME:
    // Save all regs, except `RDI`.
    push    %rsi
    push    %rax
    push    %rcx
    push    %rdx
    push    %rbx
    push    %rbp
    push    %r8
    push    %r9
    push    %r10
    push    %r11
    push    %r12
    push    %r13
    push    %r14
    push    %r15

    // Align the stack to a 16-byte boundary.
    push    %rsp
    push    (%rsp)
    and     $-16, %rsp

    call    granary_enter_direct_edge

    // Restore the old stack alignment.
    pop     %rsp

    // Restore the regs (except `RDI`)
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rbp
    pop     %rbx
    pop     %rdx
    pop     %rcx
    pop     %rax
    pop     %rsi

  .Lback_to_code_cache ## EDGE_FUNC_NAME:
    // Restore the flags. In kernel space this will enable interrupts.
    popfq

    // Restore the native `ARG1`. We stole this to store the pointer to the
    // `DirectEdge` structure.
    mov     %EDGE_SLOTS:EDGE_SLOT_ARG1(%ARG2), %ARG1

    // Restore the native `ARG2`. We stole this to store the offset from the
    // `EDGE_SLOTS` segment at which the `EdgeSlotSet` data structure is
    // located. This is sort of a hack that allows this entrypoint code to
    // not have to be dynamically generated.
    xchg    %EDGE_SLOTS:EDGE_SLOT_OFFSET(%ARG2), %ARG2  // Restore `RSI`.
    ret
END_FUNC(EDGE_FUNC_NAME)

#endif  // EDGE_FUNC_NAME
