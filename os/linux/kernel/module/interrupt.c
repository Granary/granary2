/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#ifndef MODULE
# define MODULE
#endif

#include <linux/kernel.h>
#include <linux/smp.h>

#include "dependencies/drk/descriptor.h"

enum {
  NUM_INTERRUPT_VECTORS = 256
};

// Interrupt vector numbers.
enum interrupt_vector {
  VECTOR_DIVIDE_ERROR = 0,
  VECTOR_START = VECTOR_DIVIDE_ERROR,
  VECTOR_EXCEPTION_START = VECTOR_DIVIDE_ERROR,
  VECTOR_DEBUG = 1,
  VECTOR_NMI = 2,
  VECTOR_BREAKPOINT = 3,
  VECTOR_OVERFLOW = 4,
  VECTOR_BOUND_RANGE_EXCEEDED = 5,
  VECTOR_INVALID_OPCODE = 6,
  VECTOR_DEVICE_NOT_AVAILABLE = 7,
  VECTOR_DOUBLE_FAULT = 8,
  VECTOR_COPROCESSOR_SEGMENT_OVERRUN = 9,
  VECTOR_INVALID_TSS = 10,
  VECTOR_SEGMENT_NOT_PRESENT = 11,
  VECTOR_STACK_FAULT = 12,
  VECTOR_GENERAL_PROTECTION = 13,
  VECTOR_PAGE_FAULT = 14,
  /* no 15 */
  VECTOR_X87_FPU_FLOATING_POINT_ERROR = 16,
  VECTOR_ALIGNMENT_CHECK = 17,
  VECTOR_MACHINE_CHECK = 18,
  VECTOR_SIMD_FLOATING_POINT = 19,
  VECTOR_SECURITY_EXCEPTION = 20, // Virtualization exception.

  VECTOR_EXCEPTION_END = VECTOR_SECURITY_EXCEPTION,
  VECTOR_INTERRUPT_START = 32,
  VECTOR_SYSCALL = 0x80, // Linux-specific.

  // Linux-specific.
  // See: arch/x86/include/asm/irq_vectors.h
  //      arch/ia64/include/asm/hw_irq.h
  VECTOR_X86_KVM_IPI = 0xf2,
  VECTOR_X86_IPI = 0xf7,
  VECTOR_IA64_IPI = 0xfe,

  VECTOR_INTERRUPT_END = 255,
  VECTOR_END = VECTOR_INTERRUPT_END
};

// An interrupt descriptor table.
struct IDT {
  descriptor_t vectors[2 * NUM_INTERRUPT_VECTORS];
} __attribute__((aligned (4096)));

static struct IDT idt;

// Used to share IDTs across multiple CPUs.
static system_table_register_t backup_idtr = {0, NULL};
system_table_register_t inst_idtr = {4095, &(idt.vectors[0])};

// Used by Granary's NMI handler (which is used for block translation).
extern uint8_t *granary_os_nmi_handler;
extern uint8_t granary_nmi_edge_handler;  // Code.

// Used to enable/disable interrupts and page protection.
extern int _ZN7granary4arch20TryDisableInterruptsEv(void);
extern void _ZN7granary4arch16EnableInterruptsEv(void);
extern int _ZN7granary4arch24TryDisablePageProtectionEv(void);
extern void _ZN7granary4arch20EnablePageProtectionEv(void);

// Copies the native IDTR.
void CopyNativeIDTR(void) {
  descriptor_t *nmi_desc;
  get_idtr(&backup_idtr);

  // Copies the native NMI target. This global variable is used by
  // `granary_nmi_edge_handler` to defer handling of "real" NMIs to the OS.
  nmi_desc = &(backup_idtr.base[VECTOR_NMI * 2]);
  granary_os_nmi_handler = get_gate_target_offset(&(nmi_desc->gate));
}

// Copy native IDT into `copied_idt`.
static void CopyNativeIDT(struct IDT *copied_idt) {
  int enable_interrupts = _ZN7granary4arch20TryDisableInterruptsEv();
  int enable_prot = _ZN7granary4arch24TryDisablePageProtectionEv();
  memcpy(&(idt.vectors[0]), backup_idtr.base, sizeof(struct IDT));
  if (enable_prot) _ZN7granary4arch20EnablePageProtectionEv();
  if (enable_interrupts) _ZN7granary4arch16EnableInterruptsEv();
}

// Creates an instrumented version of the IDT.
static void InstrumentIDT(void) {
  descriptor_t *desc;
  int i;
  CopyNativeIDT(&idt);
  for (i = 0; i < NUM_INTERRUPT_VECTORS; ++i) {
    desc = &(idt.vectors[i * 2]);
    if (GATE_DESCRIPTOR != get_descriptor_kind(desc)) continue;
    if (VECTOR_NMI == i) {
      set_gate_target_offset(&(desc->gate), &granary_nmi_edge_handler);
    }
  }
}

// Takes over the IDT.
void TakeoverIDT(void) {
  InstrumentIDT();
  on_each_cpu((void (*)(void *)) set_idtr, &inst_idtr, 1 /* wait */);
}
#endif
