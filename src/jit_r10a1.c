/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* Units ending in JSR (An) / JMP (An) chained straight into their target's unit. */

#include "Emu68_ARM_emit.h"
#define _GNU_SOURCE 1

#include <stdint.h>
#include <stddef.h>
#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "lists.h"
#include "tlsf.h"
#include "config.h"
#include "jit_levers.h"

#if !defined(__aarch64__)
#ifdef DE10NANO
#include "amiga_memmap.h"
extern volatile uint32_t g_block_spin;           /* start_de10nano.c: a per-block pace (set at boot, before any unit) */
#endif

volatile uint32_t g_a1_gen;                      /* bumped by every free of a translation unit */
volatile uint32_t g_a1_islands;                  /* translate time: islands appended */
volatile uint32_t g_a1_isl_skip;                 /* translate time: anchors left without an island (an unexpected epilogue) */
#define A1_ISL_MAX    8u                         /* paths one unit can have: its own + R10C_PATHS_MAX (6) continuations */
#define A1_ISL_WORDS  12u
static uint32_t *s_a1_anchor[A1_ISL_MAX];        /* where each JSR / JMP (An) path's `str REG_PC, [ctx, #PC]` went */
static unsigned s_a1_n;
static int s_a1_pending;                         /* the instruction just translated was such a JSR / JMP (An) */

/* M68k_Translator.c's translator statics, by address (the same block jit_r10c.c reads) */
extern const struct r10c_statics {
    uint32_t **tac; uintptr_t *nominal; struct M68KLocalState **ls; uint32_t **aerr; unsigned *aerr_cnt;
} g_r10c_statics;
typedef uintptr_t (*r10c_xlat_t)(uint16_t *);
extern const r10c_xlat_t g_a1_xlat_r10c;        /* jit_r10c.c: its translator copy */
typedef uint32_t *(*a1_emit_t)(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr, uint16_t *insn_consumed);
extern a1_emit_t const g_a1_emit_jmp;            /* M68k_LINE4.c: the original EMIT_JMP */
extern a1_emit_t const g_r10_emit_jsr;            /* M68k_LINE4.c: EMIT_JSR */
extern struct List *ICache;
void a1_chain(void);                             /* the asm below */

void a1_tlsf_free(void *handle, void *ptr)
{
    g_a1_gen++;
    tlsf_free(handle, ptr);
}

static int a1_site_ok(const uint16_t *site)
{
    if (g_trace_step || !g_guest_smc_announce || !JITMODE_IS_CACHED(g_jit_mode))
        return 0;
#ifdef DE10NANO
    if (g_block_spin)
        return 0;
#endif
    return EMU68_INLINE_ALLOWED(site);
}
uint32_t *a1_EMIT_JSR(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    const uint16_t *site = *m68k_ptr - 1;        /* (the opcode is consumed) */
    ptr = g_r10_emit_jsr(ptr, opcode, m68k_ptr, insn_consumed);
    if (a1_site_ok(site) && ptr[-1] == 0xffffffffu)
        s_a1_pending = 1;
    return ptr;
}
uint32_t *a1_EMIT_JMP(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    const uint16_t *site = *m68k_ptr - 1;
    ptr = g_a1_emit_jmp(ptr, opcode, m68k_ptr, insn_consumed);
    if (a1_site_ok(site) && ptr[-1] == 0xffffffffu)
        s_a1_pending = 1;
    return ptr;
}
void a1_FlushFPSR(uint32_t **arm_stream)
{
    RA_FlushFPSR(arm_stream);
    if (s_a1_pending)
    {
        s_a1_pending = 0;
        if (s_a1_n < A1_ISL_MAX)
            s_a1_anchor[s_a1_n++] = *arm_stream;
    }
}
static uintptr_t a1_post(uintptr_t len)
{
    uint32_t *const base = *g_r10c_statics.tac;
    uint32_t *mk = base + len / 4u - 1u;          /* the unit's end marker */
    if (len < 8u || (len & 3u) || *mk != 0xffffffffu
        || (uintptr_t)(mk + A1_ISL_WORDS * s_a1_n + 1u) - (uintptr_t)base > *g_r10c_statics.nominal)
    {
        g_a1_isl_skip += s_a1_n;
        return len;
    }
    const uint32_t str_pc = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
    const uint32_t ch = (uint32_t)(uintptr_t)&a1_chain;
    uint32_t gen_init = g_a1_gen ^ 0x80000000u;   /* never the current generation (and never the end marker) */
    if (gen_init == 0xffffffffu)
        gen_init = 0x7ffffffeu;
    for (unsigned i = 0; i < s_a1_n; i++)
    {
        uint32_t *const a = s_a1_anchor[i], *p = 0;
        if (a < base || a >= mk || *a != str_pc) { g_a1_isl_skip++; continue; }
        for (unsigned k = 1; k < 24u && a + k < mk; k++)
        {
            const uint32_t w = INSN_TO_LE(a[k]);
            if (w == 0xE12FFF1Eu || (w & 0xFFFF0000u) == 0xE8BD0000u) { p = a + k; break; }
        }
        if (!p) { g_a1_isl_skip++; continue; }
        const uint32_t w = INSN_TO_LE(*p);
        uint32_t frame;
        if (w == 0xE12FFF1Eu)        frame = 0xE320F000u;                    /* bx lr: the unit pushed nothing -- nop */
        else if (w & 0x6000u)        { g_a1_isl_skip++; continue; }         /* lr or sp in the list: never emitted */
        else if (w & 0x8000u)        frame = (w & ~0x8000u) | 0x4000u;
        else                         frame = w;                              /* pop {...}: lr was never pushed */
        uint32_t *const I = mk;
        I[0]  = INSN_TO_LE(frame);
        I[1]  = INSN_TO_LE(0xE1A0300Eu);          /* mov   r3, lr */
        I[2]  = INSN_TO_LE(0xE28F0018u);          /* add   r0, pc, #24      (pc = I + 4 words: the slot at I + 10) */
        I[3]  = movw_immed_u16(2, ch & 0xffffu);  /* movw  r2, a1_chain */
        I[4]  = movt_immed_u16(2, ch >> 16);      /* movt  r2, a1_chain */
        I[5]  = INSN_TO_LE(0xE12FFF32u);          /* blx   r2 */
        I[6]  = INSN_TO_LE(0xE3500000u);          /* cmp   r0, #0 */
        I[7]  = INSN_TO_LE(0x012FFF13u);          /* bxeq  r3 */
        I[8]  = INSN_TO_LE(0xE1A0E003u);          /* mov   lr, r3 */
        I[9]  = INSN_TO_LE(0xE12FFF10u);          /* bx    r0 */
        I[10] = 0u;                               /* the slot: the unit (data) */
        I[11] = gen_init;                         /* the slot: its generation (data) */
        *p = b_cc(ARM_CC_AL, (int32_t)(I - p));
        mk += A1_ISL_WORDS;
        g_a1_islands++;
    }
    *mk = 0xffffffffu;
    return (uintptr_t)(mk + 1) - (uintptr_t)base;
}
/* jit_r10c.c's selector calls this in place of its translator copy */
uintptr_t a1_translate_r10c(uint16_t *m68kcodeptr)
{
    s_a1_pending = 0;
    s_a1_n = 0;
    uintptr_t len = g_a1_xlat_r10c(m68kcodeptr);
    if (s_a1_n)
        len = a1_post(len);
    s_a1_n = 0;
    s_a1_pending = 0;
    return len;
}

struct M68KTranslationUnit *a1_fill(uint32_t *slot, uint32_t pc)
{
    if ((pc & 1u) || !EMU68_INLINE_ALLOWED(pc))
        return 0;
    struct M68KTranslationUnit *n;
    ForeachNode(&ICache[(pc ^ (pc >> 16)) & 0xffffu], n)
    {
        if ((uint32_t)(uintptr_t)n->mt_M68kAddress == pc)
        {
            slot[0] = (uint32_t)(uintptr_t)n;
            slot[1] = g_a1_gen;
            return n;
        }
    }
    return 0;
}

_Static_assert(__builtin_offsetof(struct M68KState, PC) == 76,           "a1_chain: PC offset");
_Static_assert(__builtin_offsetof(struct M68KState, MOVE2SR_REQ) == 240, "a1_chain: MOVE2SR_REQ offset");
_Static_assert(__builtin_offsetof(struct M68KState, STOP_REQ) == 244,    "a1_chain: STOP_REQ offset");
_Static_assert(__builtin_offsetof(struct M68KState, CACR_REQ) == 252,    "a1_chain: CACR_REQ offset");
_Static_assert(__builtin_offsetof(struct M68KTranslationUnit, mt_M68kAddress) == 16,  "a1_chain: mt_M68kAddress");
_Static_assert(__builtin_offsetof(struct M68KTranslationUnit, mt_ARMEntryPoint) == 64, "a1_chain: mt_ARMEntryPoint");
_Static_assert(REG_CTX == 11 && REG_PC == 12, "a1_chain: fp is the context");

#define A1_INC(k, ra, rb) ""
asm(
"   .section .r10a1_tail_text,\"ax\",%progbits     \n"
"   .align  2                               \n"
"   .global a1_chain                        \n"
"   .type   a1_chain, %function             \n"
"a1_chain:                                  \n"
A1_INC(0, "r1", "r2")                             /* [0] an island entered */
"   ldr     ip, [fp, #76]                   \n"   /* the target PC (the unit's epilogue stored it) */
"   ldr     r1, [r0, #4]                    \n"   /* the slot's generation */
"   movw    r2, #:lower16:g_a1_gen          \n"
"   movt    r2, #:upper16:g_a1_gen          \n"
"   ldr     r2, [r2]                        \n"
"   cmp     r1, r2                          \n"
"   ldreq   r1, [r0]                        \n"   /* the slot's unit: no unit freed since it was filled */
"   ldreq   r2, [r1, #16]                   \n"   /* its key (mt_M68kAddress; a soft-flush mark is bit 0) */
"   cmpeq   r2, ip                          \n"
"   bne     .La1_miss                       \n"
".La1_hit:                                  \n"
"   ldr     r0, [fp, #240]                  \n"   /* MOVE2SR_REQ */
"   ldr     r2, [fp, #244]                  \n"   /* STOP_REQ */
"   orr     r0, r0, r2                      \n"
"   ldr     r2, [fp, #252]                  \n"   /* CACR_REQ */
"   orrs    r0, r0, r2                      \n"
"   bne     .La1_no                         \n"   /* a request: C's */
"   movw    r2, #:lower16:ipl_poll_countdown \n"
"   movt    r2, #:upper16:ipl_poll_countdown \n"
"   ldr     r0, [r2]                        \n"
"   subs    r0, r0, #1                      \n"
"   ble     .La1_no                         \n"   /* the IPL poll is due: back (the trampoline decrements, then C) */
"   str     r0, [r2]                        \n"
A1_INC(4, "r0", "r2")                             /* [1] chained */
"   ldr     r0, [r1, #64]                   \n"   /* mt_ARMEntryPoint */
"   bx      lr                              \n"
".La1_no:                                   \n"
"   mov     r0, #0                          \n"
"   bx      lr                              \n"
".La1_miss:                                 \n"
"   push    {r0, r3, ip, lr}                \n"   /* the slot, the caller's return, the PC, the island */
"   mov     r1, ip                          \n"
"   bl      a1_fill                         \n"
"   mov     r1, r0                          \n"
"   pop     {r0, r3, ip, lr}                \n"
"   cmp     r1, #0                          \n"
"   bne     .La1_hit                        \n"
"   mov     r0, #0                          \n"
"   bx      lr                              \n"
"   .size   a1_chain, .-a1_chain            \n"
"   .text                                   \n"
);
#endif /* !__aarch64__ */
