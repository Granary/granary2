/* ******************************************************************************
 * Copyright (c) 2010-2014 Google, Inc.  All rights reserved.
 * Copyright (c) 2010 Massachusetts Institute of Technology  All rights reserved.
 * Copyright (c) 2000-2010 VMware, Inc.  All rights reserved.
 * ******************************************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * 
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Copyright (c) 2003-2007 Determina Corp. */
/* Copyright (c) 2001-2003 Massachusetts Institute of Technology */
/* Copyright (c) 2000-2001 Hewlett-Packard Company */

/* file "mangle.c" */

#include "dependencies/dynamorio/globals.h"
#include "dependencies/dynamorio/x86/arch.h"
#include "dependencies/dynamorio/link.h"
#include "dependencies/dynamorio/x86/instr.h"
//#include "instr_create.h"
#include "dependencies/dynamorio/x86/decode.h"
#include "dependencies/dynamorio/x86/decode_fast.h"
#ifdef STEAL_REGISTER
#include "steal_reg.h"
#endif

#ifdef RCT_IND_BRANCH
# include "dependencies/dynamorio/rct.h" /* rct_add_rip_rel_addr */
#endif

#ifndef GRANARY
#ifdef UNIX
#include <sys/syscall.h>
#endif
#endif

#include "granary/base/string.h" /* for memset */

/* make code more readable by shortening long lines
 * we mark everything we add as a meta-instr to avoid hitting
 * client asserts on setting translation fields
 */
#define POST instrlist_meta_postinsert
#define PRE  instrlist_meta_preinsert

#ifndef STANDALONE_DECODER
/****************************************************************************
 * clean call callee info table for i#42 and i#43
 */

/* Describes usage of a scratch slot. */
enum {
    SLOT_NONE = 0,
    SLOT_REG,
    SLOT_LOCAL,
    SLOT_FLAGS,
};
typedef byte slot_kind_t;

/* If kind is:
 * SLOT_REG: value is a reg_id_t
 * SLOT_LOCAL: value is meaningless, may change to support multiple locals
 * SLOT_FLAGS: value is meaningless
 */
typedef struct _slot_t {
    slot_kind_t kind;
    byte value;
} slot_t;

/* data structure of clean call callee information. */
typedef struct _callee_info_t {
    bool bailout;             /* if we bail out on function analysis */
    uint num_args;            /* number of args that will passed in */
    int num_instrs;           /* total number of instructions of a function */
    app_pc start;             /* entry point of a function  */
    app_pc bwd_tgt;           /* earliest backward branch target */
    app_pc fwd_tgt;           /* last forward branch target */
    int num_xmms_used;        /* number of xmms used by callee */
    bool xmm_used[NUM_XMM_REGS];  /* xmm/ymm registers usage */
    bool reg_used[NUM_GP_REGS];   /* general purpose registers usage */
    int num_callee_save_regs; /* number of regs callee saved */
    bool callee_save_regs[NUM_GP_REGS]; /* callee-save registers */
    bool has_locals;          /* if reference local via stack */
    bool xbp_is_fp;           /* if xbp is used as frame pointer */
    bool opt_inline;          /* can be inlined or not */
    bool write_aflags;        /* if the function changes aflags */
    bool read_aflags;         /* if the function reads aflags from caller */
    bool tls_used;            /* application accesses TLS (errno, etc.) */
    reg_id_t spill_reg;       /* base register for spill slots */
    uint slots_used;          /* scratch slots needed after analysis */
    slot_t scratch_slots[CLEANCALL_NUM_INLINE_SLOTS];  /* scratch slot allocation */
    instrlist_t *ilist;       /* instruction list of function for inline. */
} callee_info_t;
static callee_info_t     default_callee_info;
static clean_call_info_t default_clean_call_info;

#ifdef CLIENT_INTERFACE
/* hashtable for storing analyzed callee info */
static generic_table_t  *callee_info_table;
/* we only free callee info at exit, when callee_info_table_exit is true. */
static bool callee_info_table_exit = false;
#define INIT_HTABLE_SIZE_CALLEE 6 /* should remain small */

static void
callee_info_init(callee_info_t *ci)
{
    uint i;
    memset(ci, 0, sizeof(*ci));
    ci->bailout = true;
    /* to be conservative */
    ci->has_locals   = true;
    ci->write_aflags = true;
    ci->read_aflags  = true;
    ci->tls_used   = true;
    /* We use loop here and memset in analyze_callee_regs_usage later.
     * We could reverse the logic and use memset to set the value below,
     * but then later in analyze_callee_regs_usage, we have to use the loop.
     */
    /* assuming all xmm registers are used */
    ci->num_xmms_used = NUM_XMM_REGS;
    for (i = 0; i < NUM_XMM_REGS; i++)
        ci->xmm_used[i] = true;
    for (i = 0; i < NUM_GP_REGS; i++)
        ci->reg_used[i] = true;
    ci->spill_reg = DR_REG_INVALID;
}

static void
callee_info_free(callee_info_t *ci)
{
    ASSERT(callee_info_table_exit);
    if (ci->ilist != NULL) {
        ASSERT(ci->opt_inline);
        instrlist_clear_and_destroy(GLOBAL_DCONTEXT, ci->ilist);
    }
    HEAP_TYPE_FREE(GLOBAL_DCONTEXT, ci, callee_info_t,
                   ACCT_CLEANCALL, PROTECTED);
}

static callee_info_t *
callee_info_create(app_pc start, uint num_args)
{
    callee_info_t *info;
    
    info = HEAP_TYPE_ALLOC(GLOBAL_DCONTEXT, callee_info_t,
                           ACCT_CLEANCALL, PROTECTED);
    callee_info_init(info);
    info->start = start;
    info->num_args = num_args;
    return info;
}

static void
callee_info_reserve_slot(callee_info_t *ci, slot_kind_t kind, byte value)
{
    if (ci->slots_used < BUFFER_SIZE_ELEMENTS(ci->scratch_slots)) {
        if (kind == SLOT_REG)
            value = dr_reg_fixer[value];
        ci->scratch_slots[ci->slots_used].kind = kind;
        ci->scratch_slots[ci->slots_used].value = value;
    } else {
        LOG(THREAD_GET, LOG_CLEANCALL, 2,
            "CLEANCALL: unable to fulfill callee_info_reserve_slot for "
            "kind %d value %d\n", kind, value);
    }
    /* We check if slots_used > CLEANCALL_NUM_INLINE_SLOTS to detect failure. */
    ci->slots_used++;
}

static opnd_t
callee_info_slot_opnd(callee_info_t *ci, slot_kind_t kind, byte value)
{
    uint i;
    if (kind == SLOT_REG)
        value = dr_reg_fixer[value];
    for (i = 0; i < BUFFER_SIZE_ELEMENTS(ci->scratch_slots); i++) {
        if (ci->scratch_slots[i].kind  == kind &&
            ci->scratch_slots[i].value == value) {
            int disp = (int)offsetof(unprotected_context_t,
                                     inline_spill_slots[i]);
            return opnd_create_base_disp(ci->spill_reg, DR_REG_NULL, 0, disp,
                                         OPSZ_PTR);
        }
    }
    ASSERT_MESSAGE(CHKLVL_ASSERTS, "Tried to find scratch slot for value "
                   "without calling callee_info_reserve_slot for it", false);
    return opnd_create_null();
}

static void
callee_info_table_init(void)
{
    callee_info_table = 
        generic_hash_create(GLOBAL_DCONTEXT,
                            INIT_HTABLE_SIZE_CALLEE,
                            80 /* load factor: not perf-critical */,
                            HASHTABLE_SHARED | HASHTABLE_PERSISTENT,
                            (void(*)(void*)) callee_info_free
                            _IF_DEBUG("callee-info table"));
}

static void
callee_info_table_destroy(void)
{
    callee_info_table_exit = true;
    generic_hash_destroy(GLOBAL_DCONTEXT, callee_info_table);
}

static callee_info_t *
callee_info_table_lookup(void *callee)
{
    callee_info_t *ci;
    TABLE_RWLOCK(callee_info_table, read, lock);
    ci = generic_hash_lookup(GLOBAL_DCONTEXT, callee_info_table,
                             (ptr_uint_t)callee);
    TABLE_RWLOCK(callee_info_table, read, unlock);
    /* We only delete the callee_info from the callee_info_table 
     * when destroy the table on exit, so we can keep the ci 
     * without holding the lock.
     */
    return ci;
}

static callee_info_t *
callee_info_table_add(callee_info_t *ci)
{
    callee_info_t *info;
    TABLE_RWLOCK(callee_info_table, write, lock);
    info = generic_hash_lookup(GLOBAL_DCONTEXT, callee_info_table,
                               (ptr_uint_t)ci->start);
    if (info == NULL) {
        info = ci;
        generic_hash_add(GLOBAL_DCONTEXT, callee_info_table,
                         (ptr_uint_t)ci->start, (void *)ci);
    } else {
        /* Have one in the table, free the new one and use existing one. 
         * We cannot free the existing one in the table as it might be used by 
         * other thread without holding the lock.
         * Since we assume callee should never be changed, they should have
         * the same content of ci.
         */
        callee_info_free(ci);
    }
    TABLE_RWLOCK(callee_info_table, write, unlock);
    return info;
}
#endif /* CLIENT_INTERFACE */

static void
clean_call_info_init(clean_call_info_t *cci, void *callee,
                     bool save_fpstate, uint num_args)
{
    memset(cci, 0, sizeof(*cci));
    cci->callee        = callee;
    cci->num_args      = num_args;
    cci->save_fpstate  = save_fpstate;
    cci->save_all_regs = true;
    cci->should_align  = true;
    cci->callee_info   = &default_callee_info;
}
#endif /* !STANDALONE_DECODER */
/***************************************************************************/

/* Convert a short-format CTI into an equivalent one using
 * near-rel-format.
 * Remember, the target is kept in the 0th src array position,
 * and has already been converted from an 8-bit offset to an
 * absolute PC, so we can just pretend instructions are longer
 * than they really are.
 */
static instr_t *
convert_to_near_rel_common(dcontext_t *dcontext, instrlist_t *ilist, instr_t *instr)
{
    int opcode = instr_get_opcode(instr);
    DEBUG_DECLARE(const instr_info_t * info = instr_get_instr_info(instr);)
    app_pc target = NULL;

    if (opcode == OP_jmp_short) {
        instr_set_opcode(instr, OP_jmp);
        return instr;
    }

    if (OP_jo_short <= opcode && opcode <= OP_jnle_short) {
        /* WARNING! following is OP_ enum order specific */
        instr_set_opcode(instr, opcode - OP_jo_short + OP_jo);
        return instr;
    }
#ifndef GRANARY
    if (OP_loopne <= opcode && opcode <= OP_jecxz) {
        uint mangled_sz;
        uint offs;
        /*
         * from "info as" on GNU/linux system:
       Note that the `jcxz', `jecxz', `loop', `loopz', `loope', `loopnz'
       and `loopne' instructions only come in byte displacements, so that if
       you use these instructions (`gcc' does not use them) you may get an
       error message (and incorrect code).  The AT&T 80386 assembler tries to
       get around this problem by expanding `jcxz foo' to
                     jcxz cx_zero
                     jmp cx_nonzero
            cx_zero: jmp foo
            cx_nonzero:
        *
        * We use that same expansion, but we want to treat the entire
        * three-instruction sequence as a single conditional branch.
        * Thus we use a special instruction that stores the entire
        * instruction sequence as mangled bytes, yet w/ a valid target operand
        * (xref PR 251646).
        * patch_branch and instr_invert_cbr
        * know how to find the target pc (final 4 of 9 bytes).
        * When decoding anything we've written we know the only jcxz or
        * loop* instructions are part of these rewritten packages, and
        * we use remangle_short_rewrite to read back in the instr.
        * (have to do this everywhere call decode() except original
        * interp, plus in input_trace())
        *
        * An alternative is to change 'jcxz foo' to:
                    <save eflags>
                    cmpb %cx,$0
                    je   foo_restore
                    <restore eflags>
                    ...
       foo_restore: <restore eflags>
               foo:
        * However the added complications of restoring the eflags on
        * the taken-branch path made me choose the former solution.
        */

        /* SUMMARY: 
         * expand 'shortjump foo' to:
                          shortjump taken
                          jmp-short nottaken
                   taken: jmp foo
                nottaken:
        */
        if (ilist != NULL) {
            /* PR 266292: for meta instrs, insert separate instrs */
            /* reverse order */
            opnd_t tgt = instr_get_target(instr);
            instr_t *nottaken = INSTR_CREATE_label(dcontext);
            instr_t *taken = INSTR_CREATE_jmp(dcontext, tgt);
            ASSERT(!instr_ok_to_mangle(instr));
            instrlist_meta_postinsert(ilist, instr, nottaken);
            instrlist_meta_postinsert(ilist, instr, taken);
            instrlist_meta_postinsert(ilist, instr, INSTR_CREATE_jmp_short
                                      (dcontext, opnd_create_instr(nottaken)));
            instr_set_target(instr, opnd_create_instr(taken));
            return taken;
        }

        if (opnd_is_near_pc(instr_get_target(instr)))
            target = opnd_get_pc(instr_get_target(instr));
        else if (opnd_is_near_instr(instr_get_target(instr))) {
            instr_t *tgt = opnd_get_instr(instr_get_target(instr));
            /* assumption: target's translation or raw bits are set properly */
            target = instr_get_translation(tgt);
            if (target == NULL && instr_raw_bits_valid(tgt))
                target = instr_get_raw_bits(tgt);
            ASSERT(target != NULL);
        } else
            ASSERT_NOT_REACHED();

        /* PR 251646: cti_short_rewrite: target is in src0, so operands are
         * valid, but raw bits must also be valid, since they hide the multiple
         * instrs.  For x64, it is marked for re-relativization, but it's
         * special since the target must be obtained from src0 and not
         * from the raw bits (since that might not reach).
         */
        /* need 9 bytes + possible addr prefix */
        mangled_sz = CTI_SHORT_REWRITE_LENGTH;
        if (!reg_is_pointer_sized(opnd_get_reg(instr_get_src(instr, 1))))
            mangled_sz++; /* need addr prefix */
        instr_allocate_raw_bits(dcontext, instr, mangled_sz);
        offs = 0;
        if (mangled_sz > CTI_SHORT_REWRITE_LENGTH) {
            instr_set_raw_byte(instr, offs, ADDR_PREFIX_OPCODE);
            offs++;
        }
        /* first 2 bytes: jecxz 8-bit-offset */
        instr_set_raw_byte(instr, offs, decode_first_opcode_byte(opcode));
        offs++;
        /* remember pc-relative offsets are from start of next instr */
        instr_set_raw_byte(instr, offs, (byte)2);
        offs++;
        /* next 2 bytes: jmp-short 8-bit-offset */
        instr_set_raw_byte(instr, offs, decode_first_opcode_byte(OP_jmp_short));
        offs++;
        instr_set_raw_byte(instr, offs, (byte)5);
        offs++;
        /* next 5 bytes: jmp 32-bit-offset */
        instr_set_raw_byte(instr, offs, decode_first_opcode_byte(OP_jmp));
        offs++;
        /* for x64 we may not reach, but we go ahead and try */
        instr_set_raw_word(instr, offs, (int)
                           (target - (instr->bytes + mangled_sz)));
        offs += sizeof(int);
        ASSERT(offs == mangled_sz);
        LOG(THREAD, LOG_INTERP, 2, "convert_to_near_rel: jecxz/loop* opcode\n");
        /* original target operand is still valid */
        instr_set_operands_valid(instr, true);
        return instr;
    }
#endif  /* GRANARY */
    LOG(THREAD, LOG_INTERP, 1, "convert_to_near_rel: unknown opcode: %d %s\n",
        opcode, info->name);
    ASSERT_NOT_REACHED();      /* conversion not possible OR not a short-form cti */
    return instr;
}

instr_t *
convert_to_near_rel_meta(dcontext_t *dcontext, instrlist_t *ilist, instr_t *instr)
{
    return convert_to_near_rel_common(dcontext, ilist, instr);
}

void
convert_to_near_rel(dcontext_t *dcontext, instr_t *instr)
{
    convert_to_near_rel_common(dcontext, NULL, instr);
}

/* For jecxz and loop*, we create 3 instructions in a single
 * instr that we treat like a single conditional branch.
 * On re-decoding our own output we need to recreate that instr.
 * This routine assumes that the instructions encoded at pc
 * are indeed a mangled cti short.
 * Assumes that the first instr has already been decoded into instr,
 * that pc points to the start of that instr.
 * Converts instr into a new 3-raw-byte-instr with a private copy of the
 * original raw bits.
 * Optionally modifies the target to "target" if "target" is non-null.
 * Returns the pc of the instruction after the remangled sequence.
 */
byte *
remangle_short_rewrite(dcontext_t *dcontext,
                       instr_t *instr, byte *pc, app_pc target)
{
    uint mangled_sz = CTI_SHORT_REWRITE_LENGTH;
    ASSERT(instr_is_cti_short_rewrite(instr, pc));
    if (*pc == ADDR_PREFIX_OPCODE)
        mangled_sz++;

    /* first set the target in the actual operand src0 */
    if (target == NULL) {
        /* acquire existing absolute target */
        int rel_target = *((int *)(pc + mangled_sz - 4));
        target = pc + mangled_sz + rel_target;
    }
    instr_set_target(instr, opnd_create_pc(target));
    /* now set up the bundle of raw instructions
     * we've already read the first 2-byte instruction, jecxz/loop*
     * they all take up mangled_sz bytes
     */
    instr_allocate_raw_bits(dcontext, instr, mangled_sz);
    instr_set_raw_bytes(instr, pc, mangled_sz);
    /* for x64 we may not reach, but we go ahead and try */
    instr_set_raw_word(instr, mangled_sz - 4, (int)(target - (pc + mangled_sz)));
    /* now make operands valid */
    instr_set_operands_valid(instr, true);
    return (pc+mangled_sz);
}

/***************************************************************************/

/***************************************************************************/

