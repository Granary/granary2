/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE

// Note: This is Linux-specific, and assumes the first quadword of memory
//       pointed to by the base address of the segment descriptor for `FS`
//       is a pointer to said base address (i.e. self-reference).
#define EDGE_SLOTS GRANARY_IF_USER_ELSE(fs, gs)

// See `struct EdgeSlotSet` in
// `granary/arch/x86-64/assemble/2_build_fragment_list.cc` for how these
// fields work.
#define EDGE_SLOT_OFFSET 0
#define EDGE_SLOT_ENTRY_TARGET 8
#define EDGE_SLOT_ARG1 16
#define EDGE_SLOT_STACK_PTR 24

DECLARE_FUNC(granary_enter_direct_edge)

// Create the non-profiled version of the direct edge entrypoint code.
#define EDGE_FUNC_NAME granary_arch_enter_direct_edge
#include "granary/arch/x86-64/asm/edge_direct.asm"
#undef EDGE_FUNC_NAME

// Create the profiled version of the direct edge entrypoint code.
#define EDGE_FUNC_NAME granary_arch_enter_direct_edge_profiled
#define EDGE_PROFILED
#include "granary/arch/x86-64/asm/edge_direct.asm"
#undef EDGE_PROFILED
#undef EDGE_FUNC_NAME

END_FILE
