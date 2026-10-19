/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The translator variant whose conditional exits stay in the unit (joins and continuations). */

#include "Emu68_ARM_emit.h"
#define _GNU_SOURCE 1

#include "support.h"
#include "EmuFeatures.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "lists.h"
#include "tlsf.h"
#include "config.h"
#include "disasm.h"
#include "jit_levers.h"
#include "OMMX.h"

#if !defined(__aarch64__)
extern void *tlsf;
/* M68k_Translator.c's globals the translator uses */
extern uint32_t insn_count;
extern int32_t _pc_rel;
#define RTSTACK_SIZE 32                         /* = M68k_Translator.c's */
extern uint16_t *ReturnStack[RTSTACK_SIZE];
extern uint16_t ReturnStackDepth;
extern uint16_t *m68k_low, *m68k_high;
#if defined(EMU68_INNER_LOOP)
extern uint16_t *g_xlat_unit_start;
extern int g_xlat_backedge_ok;
#endif
extern int JitStatsOff;
extern uint32_t prologue_size, epilogue_size, conditionals_count;
extern uint32_t insn_seen;
extern int ChipSlowdown;
extern int debug, disasm;
extern const int debug_cnt;
extern uintptr_t g_xlat_max_insn;
extern uint16_t g_xlat_max_insn_op;
extern uintptr_t g_xlat_reserve;                /* M68k_Translator.c's EMU68_XLAT_WORST_UNIT, published */
#define EMU68_XLAT_WORST_UNIT g_xlat_reserve
extern struct M68KState *__m68k_state;
void M68K_PrintContext(void *);
/* M68k_Translator.c's file statics, by address (its assembler block g_r10c_statics) */
extern const struct r10c_statics {
    uint32_t **tac; uintptr_t *nominal; struct M68KLocalState **ls; uint32_t **aerr; unsigned *aerr_cnt;
} g_r10c_statics;
#define temporary_arm_code          (*g_r10c_statics.tac)
#define temporary_arm_code_nominal  (*g_r10c_statics.nominal)
#define local_state                 (*g_r10c_statics.ls)
#define aerr_pop_fixup              (g_r10c_statics.aerr)
#define aerr_pop_fixup_cnt          (*g_r10c_statics.aerr_cnt)
/* the opcode groups' emitters (M68k_Translator.c's line_array, the same functions) */
uint32_t *EMIT_lineA(uint32_t *arm_ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed);   /* (M68k_Translator.c) */
static uint32_t *r10d_lineF(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed);
static uint32_t * (*const r10c_line_array[16])(uint32_t *arm_ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed) = {
    EMIT_line0, EMIT_move, EMIT_move, EMIT_move, EMIT_line4, EMIT_line5, EMIT_line6, EMIT_moveq,
    EMIT_line8, EMIT_line9, EMIT_lineA, EMIT_lineB, EMIT_lineC, EMIT_lineD, EMIT_lineE, r10d_lineF
};

#define R10C_TEXT   __attribute__((noinline))
#if defined(__arm__) && !defined(__APPLE__)
#define R10C_BSS    __attribute__((section(".bss.~r10")))
#else
#define R10C_BSS
#endif
volatile uint32_t g_r10d_fbcc R10C_BSS;     /* (d): FBcc trailers rewritten (translate time) */
R10C_TEXT static uint32_t *r10d_lineF(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    const uint16_t opcode = BE16((*m68k_ptr)[0]);
    uint32_t *const start = ptr;
    ptr = EMIT_lineF(ptr, m68k_ptr, insn_consumed);
    if ((opcode & 0xff80) != 0xf280)
        return ptr;
    if (ptr - start < 5 || ptr[-1] != INSN_TO_LE(0xfffffffe) || ptr[-3] != 0u)
        return ptr;
    const uint32_t w = INSN_TO_LE(ptr[-4]);
    uint8_t not_taken = (uint8_t)(w >> 28);
    if (not_taken >= 0xEu)
        return ptr;
    uint8_t taken = not_taken ^ 1u;
    const uint32_t target = ptr[-2];
    ptr -= 3;
    uint32_t *const skip = ptr;
    *ptr++ = b_cc(*m68k_ptr == (uint16_t *)(uintptr_t)target ? taken : not_taken, 1);
    *ptr++ = (uint32_t)(uintptr_t)skip;
    *ptr++ = 1;
    *ptr++ = target;
    *ptr++ = INSN_TO_LE(0xfffffffe);
    g_r10d_fbcc++;
    return ptr;
}
#define R10C_LAB_MAX     16u     /* exits one unit may keep as labels (the rest leave inline, as before)            */
#define R10C_PATHS_MAX   6u      /* continuations one unit may translate                                             */
#define R10C_CONT_MAX    64u     /* m68k instructions one continuation may translate                                 */
#define R10C_MIN_LEFT    8u      /* no continuation starts with fewer instructions than this left in the unit depth  */
#define R10C_STUB_WORST  320u    /* bytes: the largest join stub or late exit (every store, every load, 2 more)      */

enum { R10C_PENDING = 0, R10C_BOUND, R10C_CONT, R10C_TOCONT, R10C_LATE };
struct r10c_lab {
    uint16_t *pc;                /* where the exit goes (the m68k address) */
    uint32_t *br;
    uint32_t *to;                /* BOUND / TOCONT / CONT: the ARM code it goes on at */
    uint32_t  cnt;               /* insn_count at the exit: what a late exit adds to INSN_COUNT */
    int32_t   to_pcrel;          /* BOUND: the translator's pending PC offset at `to` */
    uint8_t   cond, kind, state, xp;   /* the exit's ARM condition; 0 forward / 1 a loop's exit; R10C_*; its exit kind */
    uint16_t  rsd;               /* the translator's return stack at the exit */
    uint16_t *rs[RTSTACK_SIZE];
    struct RA_Snapshot m;        /* the allocator at the exit */
    struct RA_Snapshot mt;       /* BOUND / TOCONT: the allocator at `to` */
};
struct r10c_mem {
    struct r10c_lab lab[R10C_LAB_MAX];
    struct RA_Snapshot flushed;  /* the allocator after the main path's flush: what the next unit starts from */
};
static struct r10c_mem *r10c_m R10C_BSS;    /* from the heap at the first unit translated with (c) on */
static unsigned r10c_nlab R10C_BSS;
volatile uint32_t g_r10c_join R10C_BSS;     /* (c): exits joined to their own unit's later code      */
volatile uint32_t g_r10c_cont R10C_BSS;     /* (c): continuations translated                          */
volatile uint32_t g_r10c_tocont R10C_BSS;   /* (c): exits joined to a continuation's start            */
volatile uint32_t g_r10c_late R10C_BSS;     /* (c): exits left as exits (an out-of-line epilogue)     */

R10C_TEXT static int r10c_rs_eq(uint16_t d, uint16_t *const *rs, uint16_t d2, uint16_t *const *rs2)
{
    if (d != d2)
        return 0;
    for (unsigned i = 0; i < d; i++)
        if (rs[i] != rs2[i])
            return 0;
    return 1;
}

R10C_TEXT static void r10c_exit(uint16_t *in_code, uint16_t *next, uint16_t *tgt, uint32_t *br, unsigned lev, int *done,
                                unsigned xp)
{
    uint16_t op = BE16(in_code[0]);
    const int fbcc = (op & 0xff80) == 0xf280;
    if ((!fbcc && ((op & 0xf000) != 0x6000 || ((op >> 8) & 15) < 2)) || !r10c_m || r10c_nlab >= R10C_LAB_MAX || _pc_rel != 0)
        return;                                                 /* a Bcc's (not BRA / BSR, not DBcc, ...) or an FBcc's */
    uint32_t w = INSN_TO_LE(*br);
    if ((w & 0x0f000000u) != 0x0a000000u || (w >> 28) >= 14u)  /* the skip branch: a conditional B */
        return;
    uint16_t *ft = fbcc ? in_code + ((op & 0x0040) ? 3 : 2)    /* FBcc: the opcode + a 16- / 32-bit displacement */
                        : in_code + 1 + ((op & 0xff) == 0 ? 1 : (op & 0xff) == 0xff ? 2 : 0);
    struct r10c_lab *L = &r10c_m->lab[r10c_nlab];
    if (next == tgt && tgt != ft) {                             /* the unit went on with the TAKEN path: a loop's exit */
        L->pc = ft; L->kind = 1;
    } else {                                                    /* the unit went on at the fall-through */
        (void)lev;
        if (tgt <= in_code)
            return;
        L->pc = tgt; L->kind = 0;
    }
    L->br = br; L->cond = (uint8_t)((w >> 28) ^ 1u); L->state = R10C_PENDING; L->xp = (uint8_t)xp;
    L->cnt = insn_count; L->to = 0; L->to_pcrel = 0;
    RA_SaveState(&L->m);
    L->rsd = ReturnStackDepth;
    for (unsigned i = 0; i < ReturnStackDepth; i++)
        L->rs[i] = ReturnStack[i];
    r10c_nlab++;
    *done = 1;
}

R10C_TEXT static void r10c_bind(uint32_t *end, uint16_t *pc)
{
    for (unsigned i = 0; i < r10c_nlab; i++) {
        struct r10c_lab *L = &r10c_m->lab[i];
        if (L->state != R10C_PENDING || L->pc != pc || !r10c_rs_eq(L->rsd, L->rs, ReturnStackDepth, ReturnStack))
            continue;
        L->state = R10C_BOUND; L->to = end; L->to_pcrel = _pc_rel;
        RA_SaveState(&L->mt);
        g_r10c_join++;
        OMMX_UnitStart();       /* the joining exit may have skipped this unit's first OMMX instruction (M68k_OMMX.c) */
    }
}

R10C_TEXT static int r10c_next(uint16_t *unit_start, uintptr_t used, unsigned paths, uint32_t depth)
{
    for (unsigned i = 0; i < r10c_nlab; i++) {
        struct r10c_lab *L = &r10c_m->lab[i];
        if (L->state != R10C_PENDING)
            continue;
        for (unsigned j = 0; j < r10c_nlab; j++) {
            struct r10c_lab *C = &r10c_m->lab[j];
            if (C->state == R10C_CONT && C->pc == L->pc && r10c_rs_eq(C->rsd, C->rs, L->rsd, L->rs)) {
                L->state = R10C_TOCONT; L->to = C->to; L->to_pcrel = 0; L->mt = C->m;
                g_r10c_tocont++;
                break;
            }
        }
        if (L->state != R10C_PENDING)
            continue;
        intptr_t d = (intptr_t)L->pc - (intptr_t)unit_start;
        if (paths >= R10C_PATHS_MAX || insn_count + R10C_MIN_LEFT > depth || !EMU68_INLINE_ALLOWED(L->pc)
            || d > (intptr_t)Options.M68K_INLINE_RANGE || d < -(intptr_t)Options.M68K_INLINE_RANGE
            || used + EMU68_XLAT_WORST_UNIT + (r10c_nlab + 1u) * R10C_STUB_WORST > temporary_arm_code_nominal)
            continue;
        return (int)i;
    }
    return -1;
}

R10C_TEXT static void r10c_enter(uint32_t *end, unsigned i)
{
    struct r10c_lab *L = &r10c_m->lab[i];
    *L->br = b_cc(L->cond, (int32_t)(end - L->br));
    L->state = R10C_CONT; L->to = end;
    RA_RestoreState(&L->m);
    ReturnStackDepth = L->rsd;
    for (unsigned k = 0; k < L->rsd; k++)
        ReturnStack[k] = L->rs[k];
    _pc_rel = 0;
    OMMX_UnitStart();
    g_r10c_cont++;
}

#if EMU68_INSN_COUNTER
R10C_TEXT static uint32_t *r10c_count(uint32_t *end, uint32_t n)
{
    *end++ = ldr_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, INSN_COUNT));
    while (n) {
        uint32_t c = n > 255 ? 255 : n;
        *end++ = add_immed(9, 9, c);
        n -= c;
    }
    *end++ = str_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, INSN_COUNT));
    return end;
}
#endif

/* after every path: the joins' stubs and the late exits, each label's branch pointed at its code */
R10C_TEXT static uint32_t *r10c_finish(uint32_t *end, uint32_t **pop_update_loc, uint32_t *pop_cnt, int lr_is_saved)
{
    for (unsigned i = 0; i < r10c_nlab; i++) {
        struct r10c_lab *L = &r10c_m->lab[i];
        if (L->state == R10C_CONT)
            continue;
        *L->br = b_cc(L->cond, (int32_t)(end - L->br));
        RA_RestoreState(&L->m);
        RA_StoreDirtyFPURegs(&end);
        RA_StoreDirtyOMMXRegs(&end);
        RA_StoreDirtyM68kRegs(&end);
        RA_StoreCC(&end);
        RA_StoreFPCR(&end);
        RA_StoreFPSR(&end);
        if (L->state == R10C_PENDING) {         /* a late exit: the handler's own epilogue, out of line */
            *end++ = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
            *end++ = setend_le();
#endif
#if EMU68_INSN_COUNTER
            if (!JitStatsOff)
                end = r10c_count(end, L->cnt);
#endif
            pop_update_loc[(*pop_cnt)++] = end;
            *end++ = pop((1 << REG_SR));
            if (!lr_is_saved)
                *end++ = bx_lr();
            L->state = R10C_LATE;
            g_r10c_late++;
            continue;
        }
        /* a join: REG_PC holds the target; the code there adds its pending offset */
        if (L->to_pcrel > 0)
            *end++ = sub_immed(REG_PC, REG_PC, (uint16_t)L->to_pcrel);
        else if (L->to_pcrel < 0)
            *end++ = add_immed(REG_PC, REG_PC, (uint16_t)(-L->to_pcrel));
        for (unsigned r = 0; r < 16; r++)
            if (L->mt.arm[r] != 0xff)
                *end++ = ldr_offset(REG_CTX, L->mt.arm[r] & 15, r < 8 ? __builtin_offsetof(struct M68KState, D[r])
                                                                      : __builtin_offsetof(struct M68KState, A[r - 8]));
        if (L->mt.got_cc)
            *end++ = ldrh_offset(REG_CTX, REG_SR, __builtin_offsetof(struct M68KState, SR));
        if (L->mt.reg_fpcr != 0xff)
            *end++ = ldrh_offset(REG_CTX, L->mt.reg_fpcr, __builtin_offsetof(struct M68KState, FPCR));
        if (L->mt.reg_fpsr != 0xff)
            *end++ = ldr_offset(REG_CTX, L->mt.reg_fpsr, __builtin_offsetof(struct M68KState, FPSR));
        for (unsigned f = 0; f < 8; f++)
            if (L->mt.fpu_state[f] & 1u)
                *end++ = fldd(f + 8, REG_CTX, __builtin_offsetof(struct M68KState, FP[f]) / 4);
#ifdef EMU68_OMMX
        for (unsigned e = 0; e < 16; e++)
            if (L->mt.ommx_state[e] & 1u)                    /* OMMX_LOADED */
                *end++ = fldd(16 + e, REG_CTX, __builtin_offsetof(struct M68KState, E[8 + e]) / 4);
#endif
        *end = b_cc(ARM_CC_AL, (int32_t)(L->to - end));
        end++;
    }
    return end;
}

R10C_TEXT static uint32_t *EmitINSN_r10c(uint32_t *arm_ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    uint32_t *ptr = arm_ptr;
    uint16_t opcode = BE16((*m68k_ptr)[0]);
    uint8_t group = opcode >> 12;

#ifdef __aarch64__
    if (debug > 2)
    {
        *ptr++ = hint(0);
        *ptr++ = movw_immed_u16(31, opcode);
        *ptr++ = movk_immed_u16(31, ((uintptr_t)*(m68k_ptr)) >> 16, 1);
        *ptr++ = movk_immed_u16(31, ((uintptr_t)*(m68k_ptr)), 0);
    }
    if (debug > 1)
        *ptr++ = hint(1);
    if (debug_cnt & 1)
    {
        uint8_t reg = RA_AllocARMRegister(&ptr);
        *ptr++ = mov_immed_u16(reg, 1, 0);
        *ptr++ = msr(reg, 3, 3, 9, 12, 4);
        RA_FreeARMRegister(&ptr, reg);
    }
#endif

    if (ChipSlowdown && (uintptr_t)*m68k_ptr < EMU68_SLOWDOWN_ADDR_MAX)
    {
        if ((insn_seen % (uint32_t)ChipSlowdown) == 0)
        {
            uint32_t *slow_start = ptr;
            uint8_t reg = RA_AllocARMRegister(&ptr);
            int8_t off = 0;
            ptr = EMIT_GetOffsetPC(ptr, &off);
            *ptr++ = ldrh_offset(REG_PC, reg, off);      /* reg := [REG_PC + off]  (BASE, TARGET) */
            RA_FreeARMRegister(&ptr, reg);
            (void)slow_start;
        }
    }
    insn_seen++;

    ptr = r10c_line_array[group](ptr, m68k_ptr, insn_consumed);

    return ptr;
}

#if defined(EMU68_INNER_LOOP)
R10C_TEXT static uint32_t *EMIT_LoopBackEdge_r10c(uint32_t *end, uint32_t *loop_entry, uint32_t n_insn, uint32_t copies)
{
#if EMU68_INSN_COUNTER
    if (!JitStatsOff)
    {
        end = r10c_count(end, n_insn);   /* (the same words: r9 := [fp+INSN_COUNT], adds, store) */
    }
#else
    (void)n_insn;
#endif
    *end++ = ldr_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, LOOP_BUDGET));      /* r9 := [fp+LOOP_BUDGET] */
    *end++ = subs_immed(9, 9, copies);
    *end++ = str_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, LOOP_BUDGET));      /* [fp+LOOP_BUDGET] := r9 */
    *end = b_cc(ARM_CC_GT, (int32_t)(loop_entry - end));                                      /* budget left: next pass */
    end++;
    {
        /* exhausted: make the dispatcher poll the IPL on this very exit (r9 <= 0 here) */
        uint32_t a = (uint32_t)(uintptr_t)&ipl_poll_countdown;
        *end++ = movw_immed_u16(REG_PC, a & 0xffffu);
        *end++ = movt_immed_u16(REG_PC, a >> 16);
        *end++ = str_offset(REG_PC, 9, 0);                                                    /* [ip] := r9 */
    }
    return end;
}

R10C_TEXT static unsigned b104_unroll_want_r10c(uint32_t body_insns, uintptr_t used_bytes, uintptr_t prologue_bytes, int may_close,
                                 int back_inside)
{
    if (!may_close || !g_xlat_backedge_ok || body_insns == 0u)
        return 1u;
    uint32_t n = Options.M68K_LOOP_COUNT;          /* m68k_do_jitctl and the feats2 apply keep it in 1..15 */
    if (n < 1u)  n = 1u;
    if (n > 15u) n = 15u;
    {   /* upstream's capacity rule: every copy must fit the unit depth ... */
        uint32_t cap = Options.M68K_TRANSLATION_DEPTH / body_insns;
        if (back_inside)
            cap = (Options.M68K_TRANSLATION_DEPTH + 1u > body_insns)
                  ? (Options.M68K_TRANSLATION_DEPTH + 1u - body_insns) / body_insns : 0u;
        if (n > cap) n = cap;
    }
#if JIT_JU_LEV_DEFAULT
    n = ju_unroll_cap(n, used_bytes - prologue_bytes);
#endif
    if (n > 1u) {
        uintptr_t per  = used_bytes - prologue_bytes;
        per += per / 4u + 64u;
        uintptr_t room = (used_bytes + EMU68_XLAT_WORST_UNIT < temporary_arm_code_nominal)
                         ? temporary_arm_code_nominal - EMU68_XLAT_WORST_UNIT - used_bytes : 0u;
        uintptr_t fit  = 1u + room / per;
        if ((uintptr_t)n > fit) n = (uint32_t)fit;
    }
    return n ? n : 1u;
}
#endif

R10C_TEXT static uintptr_t M68K_Translate_r10c(uint16_t *m68kcodeptr)
{
    uint16_t *orig_m68kcodeptr = m68kcodeptr;
    uintptr_t hash = (uintptr_t)m68kcodeptr;
#define POP_UPDATE_MAX  (4 * EMU68_M68K_INSN_DEPTH + 4)
    uint32_t *pop_update_loc[POP_UPDATE_MAX];
    uint32_t pop_cnt=0;

    uint16_t *last_rev_jump = (uint16_t *)0xffffffff;

    if (RA_GetTempAllocMask())
        kprintf("[R10C] temp mask %x\n", RA_GetTempAllocMask());

    if (disasm) {
        disasm_open_b();
    }

    for (int i=0; i < POP_UPDATE_MAX; i++)
        pop_update_loc[i] = (uint32_t *)0;

    M68K_ResetReturnStack();
    RA_ResetM68kAllocator();
    RA_ResetOMMXAllocator();   /* same for the OMMX fast bank - d16-d31 are caller-saved */
    OMMX_UnitStart();
#if !defined(__aarch64__)
    aerr_pop_fixup_cnt = 0;    /* no carry-over from an abandoned translation */
    M68K_ActiveInsn    = NULL;
#endif

    int lr_is_saved = 0;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    uint32_t *loop_entry = NULL;
#endif
    prologue_size = 0;
    epilogue_size = 0;
    conditionals_count = 0;

    insn_count = 0;
    uint32_t *arm_code = temporary_arm_code;
    uint32_t *end = arm_code;

    (void)prologue_size;
    (void)lr_is_saved;

    RA_ClearChangedMask();

    uint32_t *tmpptr = end;
    pop_update_loc[pop_cnt++] = end;

#ifndef __aarch64__
    *end++ = push(0); // Space for register saving, aarch32 only
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    loop_entry = end;
#endif

#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
    *end++ = setend_be();
#endif

    *end++ = ldr_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
#else
    if (debug_cnt & 2)
    {
        uint8_t reg = RA_AllocARMRegister(&end);
        *end++ = mov_immed_u16(reg, 4, 0);
        *end++ = msr(reg, 3, 3, 9, 12, 4);
        RA_FreeARMRegister(&end, reg);
    }
#endif

    prologue_size = end - tmpptr;

#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    g_xlat_unit_start  = orig_m68kcodeptr;
    g_xlat_backedge_ok = !g_trace_step && jit_backedge_allowed((uint32_t)(uintptr_t)orig_m68kcodeptr);
    int       b104_entry_clean = RA_AllStateFlushed();
    uintptr_t b104_lo = (uintptr_t)orig_m68kcodeptr;
    unsigned  b104_copies = 0, b104_want = 1;
    int       b104_tail_dead = 0;
    int       b104_back_inside = 0;   /* the first copy jumped backward to a target other than the unit start */
    (void)b104_want; (void)b104_tail_dead; (void)b104_back_inside;
#endif

    int break_loop = FALSE;
    int inner_loop = FALSE;
    int soft_break = FALSE;
    unsigned r10c_lev = 1;                          /* the exits' labels: off only without their memory */
    unsigned r10c_path = 0;
    uint32_t r10c_cnt_adj = 0;                      /* a continuation's exits count the instructions run to them */
    uint32_t r10c_depth0 = Options.M68K_TRANSLATION_DEPTH;
    r10c_nlab = 0;
    if (r10c_lev && !r10c_m)
        r10c_m = tlsf_malloc(tlsf, sizeof(struct r10c_mem));
    if (!r10c_m)
        r10c_lev = 0;
    int max_rev_jumps = 0;
    insn_seen = 0;

r10c_again:
    while (break_loop == FALSE && soft_break == FALSE && insn_count < Options.M68K_TRANSLATION_DEPTH
           && (uintptr_t)((uintptr_t)end - (uintptr_t)arm_code) + EMU68_XLAT_WORST_UNIT
              + (r10c_lev ? (r10c_nlab + 1u) * R10C_STUB_WORST : 0u) <= temporary_arm_code_nominal)
    {
        uint16_t insn_consumed;
        uint16_t *in_code = m68kcodeptr;
        uint32_t *out_code = end;
        if (r10c_path && m68kcodeptr == orig_m68kcodeptr)
            break;

#if !defined(__aarch64__)
        if ((uintptr_t)m68kcodeptr & 1)
        {
            uint8_t areg;
            uint32_t odd_pc = (uint32_t)(uintptr_t)m68kcodeptr;

            end = EMIT_ResetOffsetPC(end);   /* REG_PC is overwritten below: no need to emit the add */
            areg = RA_AllocARMRegister(&end);
            *end++ = movw_immed_u16(areg, odd_pc & 0xffff);
            *end++ = movt_immed_u16(areg, odd_pc >> 16);
            *end++ = mov_reg(REG_PC, areg);
            end = EMIT_AddressError(end, areg,
                                    AERR_SSW_RW | AERR_SSW_FC_PROG,  /* read, program space */
                                    0 /* IR: the word at an odd address was never fetched */);
            RA_FreeARMRegister(&end, areg);
            break_loop = TRUE;
            break;
        }
        M68K_ActiveInsn = m68kcodeptr;
#endif

        if (insn_count && ((uintptr_t)m68kcodeptr < (uintptr_t)local_state[insn_count-1].mls_M68kPtr))
        {
            int found = -1;
            for (int i=insn_count - 1; i >= 0; --i)
            {
                if (local_state[i].mls_M68kPtr == m68kcodeptr)
                {
                    found = i;
                    break;
                }
            }

            if (found > 0)
            {
                if ((insn_count - found - 1) > (Options.M68K_TRANSLATION_DEPTH - insn_count))
                {
                    break;
                }
            }
        }

        if (m68kcodeptr < m68k_low)
            m68k_low = m68kcodeptr;
        if (m68kcodeptr + 16 > m68k_high)
            m68k_high = m68kcodeptr + 16;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
        if ((uintptr_t)m68kcodeptr < b104_lo)
            b104_lo = (uintptr_t)m68kcodeptr;
#endif

        if (r10c_nlab)
            r10c_bind(end, m68kcodeptr);
        local_state[insn_count].mls_ARMOffset = end - arm_code;
        local_state[insn_count].mls_M68kPtr = m68kcodeptr;
        local_state[insn_count].mls_PCRel = _pc_rel;
#ifndef __aarch64__
        for (int r=0; r < 16; r++)
            local_state[insn_count].mls_RegMap[r] = RA_GetMappedARMRegister(r);
#endif
        end = EmitINSN_r10c(end, &m68kcodeptr, &insn_consumed);
        insn_count+=insn_consumed;
#if !defined(__aarch64__)
        while (aerr_pop_fixup_cnt) {
            uint32_t *loc = aerr_pop_fixup[--aerr_pop_fixup_cnt];
            if (pop_cnt < POP_UPDATE_MAX - 1)
                pop_update_loc[pop_cnt++] = loc;
            else
                kprintf("[R10C] pop list full\n");
        }
        M68K_ActiveInsn = NULL;
#endif
        if (end[-1] == INSN_TO_LE(0xfffffff0))
        {
            lr_is_saved = 1;
            end--;
        }
#ifndef __aarch64__
        while (end[-1] == INSN_TO_LE(0xfffffff2))
        {
            end--;
            pop_update_loc[pop_cnt++] = (uint32_t *)(uintptr_t)*--end;
        }
#endif
        if (end[-1] == INSN_TO_LE(0xffffffff))
        {
            end--;
            break_loop = TRUE;
        }
        if (end[-1] == INSN_TO_LE(0xfffffff1))
        {
            end--;
            soft_break = TRUE;
        }
        if (end[-1] == INSN_TO_LE(0xfffffffe))
        {
            uint32_t *tmpptr;
            uint32_t *branch_mod[10];
            uint32_t branch_cnt;
            int local_branch_done = 0;
            uint32_t r10c_tgt = end[-2];
            end--;
            end--;  /* Remove branch target (unused!) */
            branch_cnt = *--end;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
            if (branch_cnt == 0u)
                b104_tail_dead = 1;
#endif

            for (unsigned i=0; i < branch_cnt; i++)
            {
                uintptr_t ptr = *(uint32_t *)--end;
#ifdef __aarch64__
                ptr |= (uintptr_t)end & 0xffffffff00000000;
#endif
                branch_mod[i] = (uint32_t *)ptr;
            }

            tmpptr = end;

            conditionals_count++;

            if (branch_cnt == 1u && r10c_lev)
            {
                unsigned r10c_xp = 0;
                r10c_exit(in_code, m68kcodeptr, (uint16_t *)(uintptr_t)r10c_tgt, branch_mod[0], r10c_lev,
                          &local_branch_done, r10c_xp);
            }
            if (!local_branch_done)
            {
                RA_StoreDirtyFPURegs(&end);
                RA_StoreDirtyOMMXRegs(&end);     /* OMMX fast bank, same rule as the FPU */
                RA_StoreDirtyM68kRegs(&end);
                end = EMIT_FlushPC(end);
#ifndef __aarch64__
                RA_StoreCC(&end);
                RA_StoreFPCR(&end);
                RA_StoreFPSR(&end);

                *end++ = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
                *end++ = setend_le();
#endif

#else
                RA_StoreCC(&end);
                RA_StoreFPCR(&end);
                RA_StoreFPSR(&end);
#endif
#ifndef __aarch64__
#if EMU68_INSN_COUNTER
                if (!JitStatsOff)
                {
#if defined(EMU68_BCC_INLINE)
                    end = r10c_count(end, insn_count - r10c_cnt_adj);
#else
#if defined(EMU68_BCC_INLINE)
                    uint8_t cnt_tmp = 9;
#else
                    uint8_t cnt_tmp = RA_AllocARMRegister(&end);
#endif
                    uint8_t cnt_ctx = RA_GetCTX(&end);
                    *end++ = ldr_offset(cnt_ctx, cnt_tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
                    {
                        uint32_t cnt_left = insn_count - r10c_cnt_adj;
                        while (cnt_left) {
                            uint32_t c = cnt_left > 255 ? 255 : cnt_left;
                            *end++ = add_immed(cnt_tmp, cnt_tmp, c);
                            cnt_left -= c;
                        }
                    }
                    *end++ = str_offset(cnt_ctx, cnt_tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
#if !defined(EMU68_BCC_INLINE)
                    RA_FreeARMRegister(&end, cnt_tmp);
#endif
#endif
                }
#endif
                pop_update_loc[pop_cnt++] = end;
                *end++ = pop((1 << REG_SR));
                if (!lr_is_saved)
                    *end++ = bx_lr();
#else

#if EMU68_INSN_COUNTER
                if (!JitStatsOff) {
                uint8_t ctx_free = 0;
                uint8_t ctx = RA_TryCTX(&end);
                uint8_t tmp = RA_AllocARMRegister(&end);
                if (ctx == 0xff)
                {
                    ctx = RA_AllocARMRegister(&end);
                    *end++ = mrs(ctx, 3, 3, 13, 0, 3);
                    ctx_free = 1;
                }
                EMIT_ldr64_offset(&end, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
                EMIT_add64_immed(&end, tmp, tmp, insn_count & 0xfff);
                if (insn_count & 0xfff000)
                    *end++ = adds64_immed_lsl12(tmp, tmp, insn_count >> 12);
                EMIT_str64_offset(&end, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));

                RA_FreeARMRegister(&end, tmp);
                if (ctx_free)
                    RA_FreeARMRegister(&end, ctx);
                }
#endif
                pop_update_loc[pop_cnt++] = end;
                *end++ = bx_lr();
#endif
            }
            int distance = end - tmpptr;

            for (unsigned i=0; i < branch_cnt; i++) {
#ifdef __aarch64__
                *(branch_mod[i]) = INSN_TO_LE((INSN_TO_LE(*(branch_mod[i])) + (distance << 5)));
#elif defined(EMU68_BCC_INLINE)
                {
                    uint32_t w   = INSN_TO_LE(*(branch_mod[i]));
                    int32_t  imm = ((int32_t)(w << 8)) >> 8;        /* sign-extend imm24 */
                    imm += distance;
                    if (imm >= (1 << 23) || imm < -(1 << 23))
                        kprintf("[R10C] skip %d at %p\n", imm, (void *)branch_mod[i]);
                    *(branch_mod[i]) = INSN_TO_LE((w & 0xff000000u) | ((uint32_t)imm & 0x00ffffffu));
                }
#else
                *(branch_mod[i]) = INSN_TO_LE((INSN_TO_LE(*(branch_mod[i])) + distance));
#endif
            }
            epilogue_size += distance;
        }

        {
            uintptr_t insn_bytes = (uintptr_t)end - (uintptr_t)out_code;
            if (insn_bytes > g_xlat_max_insn) {
                g_xlat_max_insn    = insn_bytes;
                g_xlat_max_insn_op = BE16(in_code[0]);
            }
        }

        if (disasm)
            disasm_print_b(in_code, insn_consumed, out_code, 4*(end - out_code), temporary_arm_code);

#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
        if (!r10c_path && !break_loop && !g_trace_step && orig_m68kcodeptr == m68kcodeptr)
            b104_copies++;
        else if (in_code > m68kcodeptr && b104_copies == 0u)
            b104_back_inside = 1;        /* an interior backward jump in the first copy (see b104_unroll_want) */
#endif
        if (in_code > m68kcodeptr)
        {
            if (last_rev_jump == m68kcodeptr) {
                if (--max_rev_jumps <= 0) {
                    break;
                }
            }
            else {
                last_rev_jump = m68kcodeptr;
                max_rev_jumps = (int)Options.M68K_LOOP_COUNT - 1;
            }
            if (max_rev_jumps <= 0 && !(!break_loop && !g_trace_step && orig_m68kcodeptr == m68kcodeptr)) {
                break;
            }
        }

        if (!r10c_path && !break_loop && !g_trace_step && (orig_m68kcodeptr == m68kcodeptr))
        {
            inner_loop = TRUE;
#if defined(EMU68_INNER_LOOP) && defined(EMU68_BCC_INLINE) && !defined(__aarch64__)
            if (b104_copies == 1u)
                b104_want = b104_unroll_want_r10c(insn_count, (uintptr_t)end - (uintptr_t)arm_code, 4u * prologue_size,
                                             b104_entry_clean && !b104_tail_dead
                                             && jit_backedge_allowed((uint32_t)b104_lo), b104_back_inside);
            if (b104_copies < b104_want && !soft_break) {
                last_rev_jump = (uint16_t *)0xffffffff;
                max_rev_jumps = 0;
                continue;
            }
#endif
            break;
        }
    }
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    if (!r10c_path && inner_loop && m68kcodeptr != orig_m68kcodeptr) {
        inner_loop = FALSE;
        g_b104_unroll_cut++;
    }
#endif
    uint32_t *out_code = end;
    tmpptr = end;
    RA_FlushFPURegs(&end);
    RA_FlushOMMXRegs(&end);              /* OMMX fast bank: store + forget, like the FPU */
    RA_FlushM68kRegs(&end);
    end = EMIT_FlushPC(end);
    RA_FlushCC(&end);
    RA_FlushFPCR(&end);
    a1_FlushFPSR(&end);
    *end++ = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
    if (r10c_path == 0 && r10c_lev)
        RA_SaveState(&r10c_m->flushed);
#if EMU68_INSN_COUNTER
    if (r10c_path && !JitStatsOff)
        end = r10c_count(end, insn_count - r10c_cnt_adj);
#endif
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    if (!r10c_path && inner_loop && !break_loop && !soft_break)
    {
        if (!g_xlat_backedge_ok || !jit_backedge_allowed((uint32_t)b104_lo))
            g_b104_refused_gate++;
        else if (!b104_entry_clean || !RA_AllStateFlushed())
            g_b104_refused_state++;
        else {
            end = EMIT_LoopBackEdge_r10c(end, loop_entry, insn_count, b104_copies ? b104_copies : 1u);
            g_b104_units++;
            if (b104_copies > 1u)
                g_b104_unrolled++;
        }
    }
#endif
#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
    *end++ = setend_le();
#endif
    pop_update_loc[pop_cnt++] = end;
    *end++ = pop((1 << REG_SR));         /* the final mask below */
    if (!lr_is_saved)
        *end++ = bx_lr();
    epilogue_size += end - tmpptr;
    if (disasm)
        disasm_print_b((uint16_t *)0, 0, out_code, 4*(end - out_code), temporary_arm_code);
    if (r10c_lev)
    {
        int nx = r10c_next(orig_m68kcodeptr, (uintptr_t)end - (uintptr_t)arm_code, r10c_path, r10c_depth0);
        if (nx >= 0)
        {
            r10c_enter(end, (unsigned)nx);
            m68kcodeptr = r10c_m->lab[nx].pc;
            r10c_path++;
            r10c_cnt_adj = insn_count - r10c_m->lab[nx].cnt;
            Options.M68K_TRANSLATION_DEPTH = (insn_count + R10C_CONT_MAX < r10c_depth0) ? insn_count + R10C_CONT_MAX
                                                                                     : r10c_depth0;
            break_loop = FALSE; soft_break = FALSE; inner_loop = FALSE;
            last_rev_jump = (uint16_t *)0xffffffff; max_rev_jumps = 0;
#if defined(EMU68_INNER_LOOP)
            g_xlat_backedge_ok = 0;
#endif
            goto r10c_again;
        }
        /* every label left: a join's stub or a late exit */
        tmpptr = end;
        end = r10c_finish(end, pop_update_loc, &pop_cnt, lr_is_saved);
        epilogue_size += end - tmpptr;
        if (disasm && end != tmpptr)
            disasm_print_b((uint16_t *)0, 0, tmpptr, 4*(end - tmpptr), temporary_arm_code);
        RA_RestoreState(&r10c_m->flushed);  /* the next unit starts from a flushed allocator */
        _pc_rel = 0;
        Options.M68K_TRANSLATION_DEPTH = r10c_depth0;
    }
    {
        uint16_t mask = RA_GetChangedMask() & 0xfff0;
        if (RA_IsCCLoaded())
            mask |= 1 << REG_SR;
        int i = 1;
        if (!mask && !lr_is_saved)
        {
            /* the push(0) placeholder becomes an ARMv7 NOP hint (see M68K_Translate), every pop a return */
            *pop_update_loc[0] = INSN_TO_LE(0xE320F000);
            while (pop_update_loc[i])
                *pop_update_loc[i++] = bx_lr();
        }
        else
        {
            if (lr_is_saved)
                *pop_update_loc[0] = push(mask | (1 << 14));
            else
                *pop_update_loc[0] = push(mask);
            while (pop_update_loc[i]) {
                if (lr_is_saved)
                    *pop_update_loc[i++] = pop(mask | (1 << 15));
                else
                    *pop_update_loc[i++] = pop(mask);
            }
        }
    }
    if (disasm)
        disasm_close();

    // Put a marker at the end of translation unit
    *end++ = 0xffffffff;

    return (uintptr_t)end - (uintptr_t)arm_code;
}

#if defined(__arm__) && !defined(__aarch64__) && !defined(__APPLE__)
#define R10E_TEXT   __attribute__((section(".r10e_tail_text"), noinline))
#else
#define R10E_TEXT   __attribute__((noinline))
#endif
R10E_TEXT void r10e_promote(uint16_t *pc)
{
    extern struct List *ICache;
    uintptr_t h = (uintptr_t)pc;
    h = (h ^ (h >> 16)) & 0xffff;
    struct List *l = &ICache[h];
    struct M68KTranslationUnit *n;
    ForeachNode(l, n)
    {
        if (n->mt_M68kAddress == pc)                  /* unmarked only: a soft-flush-marked key (bit 0) is C's to verify */
        {
            if (l->lh_Head != &n->mt_HashNode)
            {
                REMOVE(&n->mt_HashNode);
                ADDHEAD(l, &n->mt_HashNode);
            }
            return;
        }
    }
}
#ifndef DE10NANO
R10C_TEXT struct M68KTranslationUnit *M68K_GetTranslationUnit_e(uint16_t *m68kcodeptr)
{
    r10e_promote(m68kcodeptr);
    return M68K_GetTranslationUnit(m68kcodeptr);
}
#endif

typedef uintptr_t (*r10c_xlat_t)(uint16_t *);
R10C_TEXT uintptr_t M68K_Translate_sel(uint16_t *m68kcodeptr)
{
    return a1_translate_r10c(m68kcodeptr);
}
#if defined(__arm__) && !defined(__aarch64__) && !defined(__APPLE__)
__attribute__((section(".r10a1_tail_rodata")))
#endif
const r10c_xlat_t g_a1_xlat_r10c = M68K_Translate_r10c;
#endif /* !__aarch64__ */
