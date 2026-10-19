/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The FPU translation: lazy FPSR condition codes, FBF / FNOP as no code. */

#include "Emu68_ARM_emit.h"
#include <stdint.h>
#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "config.h"
#include "jit_levers.h"

#if !defined(__aarch64__) && defined(EMU68_BCC_INLINE) && JIT_JF_LEV_DEFAULT
#define JF_BUILT 1
#else
#define JF_BUILT 0
#endif

#if JF_BUILT
#define B166_TEXT __attribute__((section(".b166_tail_text"), noinline))
#define B166_DATA __attribute__((section(".b166_tail_data")))
static inline uint32_t jf_lev(void) { return JIT_JF_LEV_DEFAULT; }
enum { JF_INT = 0, JF_OVER, JF_PASS, JF_OTHER };
enum { JF_S_OWE = 0, JF_S_DROP, JF_S_EXIT, JF_S_READ, JF_S_END, JF_S_LAB, JF_S_PLAIN, JF_S_KEEP };
#define JF_W       8u                       /* the words that compute the owed condition codes */
#define JF_LAB_MAX 256u

static struct {
    uint16_t *lab[JF_LAB_MAX];              /* addresses an exit of this unit may join at (lz_note's rule) */
    uint16_t  nlab;                         /* lab[]'s count; JF_LAB_MAX + 1: full, every address may be one */
    uint8_t   pend;                         /* the FPSR condition codes are owed ... */
    uint8_t   dr;                           /* ... of this VFP register's value (d8-d15: FP0-FP7) */
    uint8_t   fl;                           /* a path's end was flushed during the last emission */
} jf B166_DATA;

typedef uint32_t *(*jf_emit_t)(uint32_t *, uint16_t **, uint16_t *);

static inline int jf_lazy(void) { return (jf_lev() & JIT_JF_LEV_LAZYFPSR) != 0; }

/* the FPSR's cached ARM register (0xff: not cached) and whether it is marked modified */
B166_TEXT static uint8_t jf_F(uint8_t *mod)
{
    struct RA_Snapshot s;
    RA_SaveState(&s);
    if (mod)
        *mod = s.mod_fpsr;
    return s.reg_fpsr;
}
int  jf_flags_free(void);
void jf_flags_drain(uint32_t **arm_stream);

/* the owed condition codes of dR into F -- no ARM flag written, r9 the only temporary, no allocation */
B166_TEXT static uint32_t *jf_words(uint32_t *p, uint8_t F, uint8_t dr)
{
    uint32_t *const p0 = p;
    (void)p0;
    *p++ = fcmpzd(dr);                                              /* vcmp.f64 dR, #0                          */
    *p++ = INSN_TO_LE(0xEEF19A10u);                                 /* vmrs r9, fpscr (not APSR_nzcv)          */
    *p++ = _and_val(9, 9, 0x50000000u);                             /* the compare's Z (30) and V (28)         */
    *p++ = _bic_val(F, F, 0x0F000000u);                             /* N Z I NAN                               */
    *p++ = orr_reg(F, F, 9, LSR, 4);                                /* Z -> 26, unordered -> NAN (24)          */
    *p++ = INSN_TO_LE(0xEE300B10u | (9u << 12) | ((uint32_t)dr << 16));   /* vmov.32 r9, dR[1]                 */
    *p++ = _and_val(9, 9, 0x80000000u);                             /* the sign                                */
    *p++ = orr_reg(F, F, 9, LSR, 4);
    return p;
}

B166_TEXT static uint32_t *jf_words_f(uint32_t *p, uint8_t F, uint8_t dr)
{
    uint32_t *const p0 = p;
    (void)p0;
    *p++ = fcmpzd(dr);
    *p++ = fmstat();
    *p++ = _bic_val(F, F, 0x0F000000u);
    *p++ = orr_cc_immed(ARM_CC_EQ, F, F, 0x404);                    /* Z                                       */
    *p++ = orr_cc_immed(ARM_CC_VS, F, F, 0x401);                    /* NAN                                     */
    *p++ = INSN_TO_LE(0xEE300B10u | (9u << 12) | ((uint32_t)dr << 16));   /* vmov.32 r9, dR[1]                 */
    *p++ = _tst_val(9, 0x80000000u);
    *p++ = orr_cc_immed(ARM_CC_MI, F, F, 0x408);                    /* N: the sign                             */
    return p;
}

#define JF_FORM(i) ((void)0)
B166_TEXT static uint32_t *jf_words_x(uint32_t *p, uint8_t F, uint8_t dr, int ff)
{
    if (ff && !(jf_lev() & JIT_JF_LEV_AS3392)) {
        JF_FORM(0);
        return jf_words_f(p, F, dr);
    }
    JF_FORM(1);
    return jf_words(p, F, dr);
}

/* compute them here, in code every path runs (RA_ModifyFPSR may load the FPSR / evict a register) */
B166_TEXT static uint32_t *jf_now(uint32_t *ptr, unsigned why, int ff)
{
    const uint8_t F = RA_ModifyFPSR(&ptr);
    ptr = jf_words_x(ptr, F, jf.dr, ff);
    jf.pend = 0;
    return ptr;
}

/* the words emitted at d moved back: [s, e) over them */
B166_TEXT static uint32_t *jf_back(uint32_t *d, uint32_t *s, uint32_t *e)
{
    uint32_t *const d0 = d;
    for (uint32_t *q = s; q < e; q++)
        *d++ = *q;
    (void)d0;
    return d;
}

B166_TEXT static uint32_t *jf_trailer(uint32_t *s, uint32_t *e)
{
    if (e - s < 5 || e[-1] != INSN_TO_LE(0xFFFFFFFEu) || e[-3] != 1u || e[-4] != (uint32_t)(uintptr_t)(e - 5))
        return 0;
    const uint32_t w = INSN_TO_LE(e[-5]);
    if ((w & 0x0F000000u) != 0x0A000000u || (w >> 28) >= 0xEu)
        return 0;
    return e - 5;
}

#define JF_CLEAN_MAX 2048u
B166_TEXT static int jf_clean(const uint32_t *s, const uint32_t *e)
{
    const unsigned n = (unsigned)(e - s);
    if (n > JF_CLEAN_MAX)
        return 0;
    uint32_t seen[JF_CLEAN_MAX / 32u];
    uint16_t todo[64];
    unsigned nt = 0, lr_set = 0;
    for (unsigned i = 0; i < (n + 31u) / 32u; i++)
        seen[i] = 0;
    if (n)
        todo[nt++] = 0;
    while (nt) {
        unsigned i = todo[--nt];
        while (i < n && !(seen[i >> 5] & (1u << (i & 31u)))) {
            seen[i >> 5] |= 1u << (i & 31u);
            const uint32_t x = INSN_TO_LE(s[i]);
            if ((x >> 28) == 0xFu) {
                if (x == 0xFFFFFFF0u && i == n - 1u)
                    break;
                return 0;                                           /* a marker, the unconditional space */
            }
            if ((x & 0x0E000000u) == 0x0A000000u) {                 /* B / BL */
                if (x & 0x01000000u)
                    return 0;
                const int32_t t = (int32_t)i + 2 + (((int32_t)(x << 8)) >> 8);
                if (t < 0 || t > (int32_t)n)
                    return 0;
                if (t < (int32_t)n) {
                    if (nt == 64u)
                        return 0;
                    todo[nt++] = (uint16_t)t;
                }
                if ((x >> 28) == 0xEu)
                    break;                                          /* (AL: no fall-through) */
                i++;
                continue;
            }
            if ((x & 0x0FFFFFF0u) == 0x012FFF30u) {                 /* BLX Rm: a call, it returns here */
                i++;
                continue;
            }
            if ((x & 0x0FFFFFF0u) == 0x012FFF10u) {                 /* BX Rm */
                if ((x & 15u) == 14u || !lr_set)
                    return 0;
                break;                                              /* a helper call: it returns to lr's target */
            }
            if ((x & 0x0F000000u) == 0x0F000000u || (x & 0x0FF000F0u) == 0x07F000F0u)
                return 0;                                           /* SVC, UDF */
            if ((x & 0x0E108000u) == 0x08108000u)
                return 0;                                           /* LDM with pc */
            if ((x & 0x0FFFF000u) == 0x028FE000u) {                 /* add lr, pc, #k: a helper call's return */
                const uint32_t r = ((x >> 8) & 15u) * 2u, i8 = x & 0xFFu;
                const uint32_t k = r ? ((i8 >> r) | (i8 << (32u - r))) : i8;
                const uint32_t t = i + 2u + k / 4u;
                if ((k & 3u) || t > n)
                    return 0;
                if (t < n) {
                    if (nt == 64u)
                        return 0;
                    todo[nt++] = (uint16_t)t;
                }
                lr_set = 1;
            }
            if ((x & 0x08000000u) == 0 && ((x >> 12) & 15u) == 15u
                && !((x & 0x0C000000u) == 0 && ((x >> 23) & 3u) == 2u && (x & 0x00100000u)))
                return 0;                                           /* pc written (TST / TEQ / CMP / CMN have no Rd) */
            i++;
        }
    }
    return 1;
}

/* the emission [s, e) ends with EMIT_GetFPUFlagsFromD's words on dR (d8-d15) into F: where they start */
B166_TEXT static uint32_t *jf_tail(uint32_t *s, uint32_t *e, uint8_t F, uint8_t *dr)
{
    if (e - s < (int)JF_W || F > 8u)
        return 0;
    uint32_t *const b = e - JF_W;
    const uint8_t d = (INSN_TO_LE(b[0]) >> 12) & 15u;
    if (d < 8u || b[0] != fcmpzd(d) || b[1] != fmstat() || b[2] != _bic_val(F, F, 0x0F000000u)
        || b[3] != orr_cc_immed(ARM_CC_EQ, F, F, 0x404) || b[4] != orr_cc_immed(ARM_CC_VS, F, F, 0x401))
        return 0;
    const uint8_t h = (INSN_TO_LE(b[5]) >> 12) & 15u;
    if (h > 8u || h == F || b[5] != INSN_TO_LE(0xEE300B10u | ((uint32_t)h << 12) | ((uint32_t)d << 16))
        || b[6] != _tst_val(h, 0x80000000u) || b[7] != orr_cc_immed(ARM_CC_MI, F, F, 0x408))
        return 0;
    *dr = d;
    return b;
}

B166_TEXT static uint32_t *jf_ins(uint32_t *s, uint32_t *T, const uint32_t *tr, const uint32_t *w, unsigned n, int excl)
{
    const uint8_t inl = (uint8_t)(INSN_TO_LE(tr[0]) >> 28);
    uint32_t *p = T;
    if (excl) {
        uint32_t *br = 0;
        for (uint32_t *q = T - 1; q >= s && T - q <= 24; q--) {
            const uint32_t x = INSN_TO_LE(*q);
            if ((x & 0x0F000000u) == 0x0A000000u) {
                if ((x >> 28) == inl && q + 2 + (((int32_t)(x << 8)) >> 8) == T)
                    br = q;
                break;                                              /* the last branch before the trailer only */
            }
        }
        if (br)
            *br = b_cc(inl, (int32_t)(T - br) + (int32_t)n);
        else
            *p++ = b_cc(inl, 1 + (int32_t)n);
    }
    uint32_t *const w0 = p;
    for (unsigned i = 0; i < n; i++)
        *p++ = w[i];
    (void)w0;
    uint32_t *const t0 = p;
    for (unsigned i = 0; i < 5u; i++)
        *p++ = tr[i];
    t0[1] = (uint32_t)(uintptr_t)t0;
    return p;
}

B166_TEXT static uint32_t *jf_at_end(uint32_t *s, uint32_t *e, unsigned why)
{
    uint32_t *const T = jf_trailer(s, e);
    const unsigned k = T ? 5u : (e > s && e[-1] == INSN_TO_LE(0xFFFFFFF0u)) ? 1u : 0u;
    uint32_t tr[5];
    for (unsigned i = 0; i < k; i++)
        tr[i] = e[(int)i - (int)k];
    uint32_t *const g = e;
    uint32_t *p = jf_now(e, why, !k && jf_flags_free());           /* (before a trailer / a marker: the flags are its) */
    if (!k)
        return p;
    const unsigned n = (unsigned)(p - g);
    uint32_t *d = e - k;
    for (unsigned i = 0; i < n; i++)                               /* the generated words down by k */
        d[i] = g[i];
    uint32_t *const t0 = d + n;
    for (unsigned i = 0; i < k; i++)
        t0[i] = tr[i];
    if (T)
        t0[1] = (uint32_t)(uintptr_t)t0;
    return t0 + k;
}

B166_TEXT static void jf_note(const uint32_t *start, const uint32_t *end, const uint16_t *pc, const uint16_t *next)
{
    if (end - start < 2 || end[-1] != INSN_TO_LE(0xFFFFFFFEu))
        return;
    const uint16_t op = BE16(pc[0]);
    unsigned len = 0;
    if ((op & 0xF000) == 0x6000 && ((op >> 8) & 15) >= 2)
        len = (op & 0xFF) == 0 ? 2 : (op & 0xFF) == 0xFF ? 3 : 1;
    else if ((op & 0xFF80) == 0xF280)
        len = (op & 0x0040) ? 3 : 2;
    else
        return;
    uint16_t *const tgt = (uint16_t *)(uintptr_t)end[-2], *const ft = (uint16_t *)pc + len;
    uint16_t *const l = (next == tgt && tgt != ft) ? ft : tgt > pc ? tgt : 0;
    if (!l)
        return;
    if (jf.nlab < JF_LAB_MAX)
        jf.lab[jf.nlab] = l;
    if (jf.nlab <= JF_LAB_MAX)
        jf.nlab++;
}

/* can the code at pc be reached before any wrap runs: a label, an odd address, line A */
B166_TEXT static int jf_stop(const uint16_t *pc)
{
    if ((uintptr_t)pc & 1u)
        return 1;
    if (jf.nlab > JF_LAB_MAX)
        return 1;
    for (unsigned i = 0; i < jf.nlab; i++)
        if (jf.lab[i] == pc)
            return 1;
    return (BE16(pc[0]) & 0xF000) == 0xA000;
}

extern int DisableFPU;
#define JF_FBF_OP(op) ((op) == 0xF280u || (op) == 0xF2C0u)
static inline int jf_fbf(uint16_t op) { return JF_FBF_OP(op) && (jf_lev() & JIT_JF_LEV_FNOP) && !DisableFPU; }
B166_TEXT int jf_fbf_nop(uint32_t **pptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    const uint16_t op = BE16((*m68k_ptr)[0]);
    if (!jf_fbf(op))
        return 0;
    const unsigned words = (op & 0x0040u) ? 3u : 2u;              /* the opcode + a 16- / 32-bit displacement */
    *m68k_ptr += words;
    *insn_consumed = 1;
    *pptr = EMIT_AdvancePC(*pptr, (uint8_t)(2u * words));         /* (an add only when the PC offset would overflow) */
    return 1;
}

B166_TEXT static unsigned jf_class(uint16_t op, uint16_t op2)
{
    if (jf_fbf(op))
        return JF_PASS;
    if (op == 0xF200 && (op2 & 0xFC00) == 0x5C00)
        return JF_OVER;                                             /* FMOVECR */
    if ((op & 0xFFC0) != 0xF200)
        return JF_OTHER;
    if ((op2 & 0xA000) == 0) {
        const unsigned m = op2 & 0x7F;
        if ((m & 0x78) == 0x30)
            return JF_OVER;                                         /* FSINCOS */
        switch (m) {
        case 0x18: case 0x58: case 0x5C:
        case 0x22: case 0x62: case 0x66:                            /* FADD                   */
        case 0x38: case 0x3A:                                       /* FCMP FTST              */
        case 0x20: case 0x60: case 0x64: case 0x24:                 /* FDIV FSGLDIV           */
        case 0x01: case 0x03:                                       /* FINT FINTRZ            */
        case 0x14: case 0x06: case 0x15: case 0x16:                 /* FLOGN FLOGNP1 FLOG10 FLOG2 */
        case 0x10: case 0x08: case 0x12: case 0x11:                 /* FETOX FETOXM1 FTENTOX FTWOTOX */
        case 0x02: case 0x19: case 0x09:                            /* FSINH FCOSH FTANH      */
        case 0x0A: case 0x0D: case 0x1C: case 0x0C: case 0x0F:      /* FATAN FATANH FACOS FASIN FTAN */
        case 0x00: case 0x40: case 0x44:                            /* FMOVE <ea>,FPn         */
        case 0x23: case 0x63: case 0x67: case 0x27:                 /* FMUL FSGLMUL           */
        case 0x1A: case 0x5A: case 0x5E:                            /* FNEG                   */
        case 0x1E: case 0x1F: case 0x21: case 0x25: case 0x26:      /* FGETEXP FGETMAN FMOD FREM FSCALE */
        case 0x04: case 0x41: case 0x45:                            /* FSQRT                  */
        case 0x28: case 0x68: case 0x6C:                            /* FSUB                   */
        case 0x0E: case 0x1D:                                       /* FSIN FCOS              */
            return JF_OVER;
        }
        return JF_OTHER;
    }
    if ((op2 & 0xE07F) == 0x6000 && ((op2 >> 10) & 3u) != 3u) {
        return JF_PASS;
    }
    if ((op2 & 0xC3FF) == 0x8000 && !(op2 & 0x0800) && (op2 & 0x1400))
        return JF_PASS;
    return JF_OTHER;
}

#if defined(DE10NANO)
static inline int jm_fast(uint32_t a) { return (a >= 0x00200000u && a < 0x00A00000u) || (a >= 0x40000000u && a < 0x50000000u); }
#else
static inline int jm_fast(uint32_t a) { return (a >= 0x00200000u && a < 0x3F000000u) || a >= 0xFF000000u; }
#endif
#define JM_W 1u                                 /* LDR / STR / LDRB / STRB, immediate offset */
#define JM_X 2u                                 /* LDRH / STRH / LDRSB / LDRSH, immediate offset */
B166_TEXT static unsigned jm_ldst(uint32_t w, unsigned *L, uint8_t *rn, uint8_t *rt, int32_t *off)
{
    const uint32_t x = INSN_TO_LE(w);
    if ((x >> 28) != 0xEu)
        return 0;
    unsigned k = 0;
    int32_t imm = 0;
    if ((x & 0x0F200000u) == 0x05000000u) {
        k = JM_W; imm = (int32_t)(x & 0xFFFu);
    } else if ((x & 0x0F600090u) == 0x01400090u && (x & 0x60u)) {
        k = JM_X; imm = (int32_t)(((x >> 4) & 0xF0u) | (x & 0xFu));
    } else
        return 0;
    *L = (x >> 20) & 1u; *rn = (x >> 16) & 15u; *rt = (x >> 12) & 15u;
    *off = (x & 0x00800000u) ? imm : -imm;
    return k;
}
/* the same transfer post-indexed: [rn], #+sz */
B166_TEXT static uint32_t jm_post(uint32_t w, unsigned k, unsigned sz)
{
    uint32_t x = INSN_TO_LE(w);
    if (k == JM_W)
        x = (x & ~0x01A00FFFu) | 0x00800000u | sz;
    else
        x = (x & ~0x01A00F0Fu) | 0x00800000u | ((sz & 0xF0u) << 4) | (sz & 0xFu);
    return INSN_TO_LE(x);
}
/* ADD / SUB Rd, Rn, #imm (no S), condition AL: Rd, Rn and the value (negative for SUB) */
B166_TEXT static int jm_addimm(uint32_t w, uint8_t *rd, uint8_t *rn, int32_t *v)
{
    const uint32_t x = INSN_TO_LE(w);
    const uint32_t o = x & 0x0FF00000u;
    if ((x >> 28) != 0xEu || (o != 0x02800000u && o != 0x02400000u))
        return 0;
    const uint32_t r = ((x >> 8) & 15u) * 2u, i8 = x & 0xFFu;
    const uint32_t val = r ? ((i8 >> r) | (i8 << (32u - r))) : i8;
    *rd = (x >> 12) & 15u; *rn = (x >> 16) & 15u;
    *v = o == 0x02800000u ? (int32_t)val : -(int32_t)val;
    return 1;
}
B166_TEXT static int jm_movwt(const uint32_t *q, const uint32_t *e, uint8_t rd, uint32_t *val, unsigned *n)
{
    const uint32_t x = INSN_TO_LE(q[0]);
    if ((x & 0xFFF00000u) != 0xE3000000u || ((x >> 12) & 15u) != rd)
        return 0;
    *val = ((x >> 4) & 0xF000u) | (x & 0xFFFu);
    *n = 1;
    if (q + 1 < e) {
        const uint32_t y = INSN_TO_LE(q[1]);
        if ((y & 0xFFF00000u) == 0xE3400000u && ((y >> 12) & 15u) == rd) {
            *val |= (((y >> 4) & 0xF000u) | (y & 0xFFFu)) << 16;
            *n = 2;
        }
    }
    return 1;
}
/* VLDR Dd / Sd, [Rn, #off] (off a multiple of 4, |off| <= 1020) */
B166_TEXT static uint32_t jm_vldr(int dbl, uint8_t v, uint8_t rn, int32_t off)
{
    const uint32_t u = off < 0 ? 0u : 0x00800000u, i = (uint32_t)(off < 0 ? -off : off) >> 2;
    const uint32_t vd = dbl ? VFP_VD(v) : ((((uint32_t)v >> 1) << 12) | (((uint32_t)v & 1u) << 22));
    return INSN_TO_LE(0xED100A00u | (dbl ? 0x100u : 0u) | u | ((uint32_t)rn << 16) | vd | i);
}
B166_TEXT static int jm_reloc_ok(const uint32_t *s, const uint32_t *e, const uint32_t *lo, const uint32_t *hi)
{
    for (const uint32_t *q = s; q < e; q++) {
        if (q >= lo && q < hi)
            continue;
        const uint32_t x = INSN_TO_LE(*q);
        const uint32_t *t = 0;
        if ((x >> 28) == 0xFu)
            continue;
        if ((x & 0x0E000000u) == 0x0A000000u)
            t = q + 2 + (((int32_t)(x << 8)) >> 8);
        else if ((x & 0x0C000000u) == 0x04000000u && ((x >> 16) & 15u) == 15u)
            t = (const uint32_t *)((uintptr_t)(q + 2) + (uintptr_t)((x & 0x00800000u) ? (int32_t)(x & 0xFFFu) : -(int32_t)(x & 0xFFFu)));
        else if ((x & 0x0E000000u) == 0x0C000000u && ((x >> 16) & 15u) == 15u)
            t = q + 2 + ((x & 0x00800000u) ? (int32_t)(x & 0xFFu) : -(int32_t)(x & 0xFFu));
        else if ((x & 0x0FEF0000u) == 0x028F0000u || (x & 0x0FEF0000u) == 0x024F0000u) {
            uint8_t rd, rn;                                         /* adr: add / sub Rd, pc, #imm */
            int32_t v;
            if (!jm_addimm(*q, &rd, &rn, &v) || (v & 3))
                return 0;
            t = q + 2 + v / 4;
        }
        else if ((x & 0x0E000090u) == 0x00000090u && (x & 0x60u) && ((x >> 16) & 15u) == 15u)
            return 0;                                               /* an extra load / store off the pc */
        if (!t)
            continue;
        const int side_q = q < lo ? 0 : 1, side_t = t < lo ? 0 : t >= hi ? 1 : 2;
        if (side_q != side_t)
            return 0;
    }
    return 1;
}
B166_TEXT static uint32_t *jm_put(uint32_t *q, unsigned n0, const uint32_t *w, unsigned n1, uint32_t *e)
{
    uint32_t *const src = q + n0, *const dst = q + n1;
    const unsigned nt = (unsigned)(e - src);
    if (n1 > n0) {
        for (unsigned i = nt; i-- > 0; )
            dst[i] = src[i];
    } else if (n1 < n0) {
        for (unsigned i = 0; i < nt; i++)
            dst[i] = src[i];
    }
    for (unsigned i = 0; i < n1; i++)
        q[i] = w[i];
    return dst + nt;
}
#define JM_CNT(i) ((void)0)

#define JS_CNT(i) ((void)0)
/* eor r9, rb, #K ; tst r9, #M -- Z set when rb is in the window and word-aligned */
B166_TEXT static uint32_t *jm_guard(uint32_t *p, uint8_t rb)
{
#if defined(DE10NANO)
    *p++ = _eor_val(9, rb, 0x40000000u);
    *p++ = _tst_val(9, 0xF0000003u);                 /* the top nibble 4, the low two bits 0 */
#else
    *p++ = _eor_val(9, rb, 0x30000000u);
    *p++ = _tst_val(9, 0x30000003u);                 /* bits 29-28 set, the low two bits 0 */
#endif
    return p;
}
B166_TEXT static uint32_t *jm_store_guard(uint32_t *s, uint32_t *e, uint8_t bA, unsigned fmt)
{
    (void)bA;
    if (fmt != 5 && fmt != 1 && fmt != 0)
        return e;
    uint32_t *const lim = (e - s > 32) ? s + 32 : e;
    for (uint32_t *q = s; q + 3 <= lim; q++) {
        unsigned L1, L2;
        uint8_t r1, r2, t1, t2, v;
        int32_t o1, o2;
        if (fmt == 5) {
            const uint32_t x = INSN_TO_LE(*q);
            if ((x & 0x0FF00FD0u) != 0x0C500B10u || (x >> 28) != 0xEu)
                continue;
            const uint8_t lo = (x >> 12) & 15u, hi = (x >> 16) & 15u;
            v = (x & 15u) | ((x >> 1) & 16u);
            if (*q != fmrrd(hi, lo, v) || jm_ldst(q[1], &L1, &r1, &t1, &o1) != JM_W || L1 || t1 != hi || r1 > 8u
                || (INSN_TO_LE(q[1]) & 0x00400000u) || jm_ldst(q[2], &L2, &r2, &t2, &o2) != JM_W || L2 || t2 != lo
                || r2 != r1 || o2 != o1 + 4 || (INSN_TO_LE(q[2]) & 0x00400000u) || r1 == hi || r1 == lo)
                return e;
        } else {
            const uint32_t xc = INSN_TO_LE(*q);
            const uint8_t sV = (uint8_t)((((xc >> 12) & 15u) << 1) | ((xc >> 22) & 1u)), dX = xc & 15u;
            if (*q != (fmt == 1 ? fcvtsd(sV, dX) : ftosid(sV, dX)))
                continue;
            const uint32_t xm = INSN_TO_LE(q[1]);
            if ((xm & 0x0FF00F7Fu) != 0x0E100A10u || (xm >> 28) != 0xEu
                || (uint8_t)((((xm >> 16) & 15u) << 1) | ((xm >> 7) & 1u)) != sV)
                return e;
            if (jm_ldst(q[2], &L1, &r1, &t1, &o1) != JM_W || L1 || r1 > 8u || t1 != ((xm >> 12) & 15u) || r1 == t1
                || (INSN_TO_LE(q[2]) & 0x00400000u))
                return e;
            v = sV;
        }
        if ((o1 & 3) || o1 > 1020 || o1 < -1020 || !jm_reloc_ok(s, e, q, q + 3)) {
            JS_CNT(1);
            return e;
        }
        uint32_t w[16];
        unsigned n = 0;
        uint32_t old[3] = { q[0], q[1], q[2] };
        if (fmt != 5)
            w[n++] = old[0];                                /* the conversion, on both paths */
        const uint8_t bR = r1;                              /* the store's base: An itself or a copy of it */
        uint8_t rb = bR;
        if (o1) {
            uint16_t f;
            const uint32_t m = (uint32_t)(o1 < 0 ? -o1 : o1);
            if (!arm_encode_imm12(m, &f))
                return e;
            w[n++] = INSN_TO_LE((o1 < 0 ? 0xE2400000u : 0xE2800000u) | ((uint32_t)bR << 16) | (9u << 12) | f);   /* add / sub r9 */
            rb = 9;
        }
        uint32_t *g = jm_guard(&w[n], rb);
        n = (unsigned)(g - w);
        const unsigned nslow = fmt == 5 ? 3u : 2u;
        w[n++] = b_cc(ARM_CC_NE, 3);                       /* -> SLOW (over the vstr and the b) */
        {
            const uint32_t u = o1 < 0 ? 0u : 0x00800000u, i = (uint32_t)(o1 < 0 ? -o1 : o1) >> 2;
            const uint32_t vd = fmt == 5 ? VFP_VD(v) : ((((uint32_t)v >> 1) << 12) | (((uint32_t)v & 1u) << 22));
            w[n++] = INSN_TO_LE(0xED000A00u | (fmt == 5 ? 0x100u : 0u) | u | ((uint32_t)bR << 16) | vd | i);   /* vstr */
        }
        w[n++] = b_cc(ARM_CC_AL, 1 + (int32_t)nslow);      /* -> DONE */
        if (fmt == 5) { w[n++] = old[0]; w[n++] = old[1]; w[n++] = old[2]; }
        else          { w[n++] = old[1]; w[n++] = old[2]; }
        JS_CNT(0);
        return jm_put(q, 3, w, n, e);
    }
    return e;
}

B166_TEXT static uint32_t *jm_line(uint32_t *s, uint32_t *e, const uint16_t *pc, uint16_t op, uint16_t op2)
{
    if ((op & 0xFFC0) != 0xF200)
        return e;
    const unsigned mode = (op >> 3) & 7u, reg = op & 7u, fmt = (op2 >> 10) & 7u;
    const int store = (op2 & 0xE000) == 0x6000, load = (op2 & 0xA000) == 0 && (op2 & 0x4000);
    if ((!store && !load) || (load && !(jf_lev() & JIT_JF_LEV_FPMEM)))
        return e;                                               /* (bit 0x02 the loads and the (An)+ stores, 0x04 the guard) */
    if (fmt == 2 || fmt == 3 || fmt == 7)
        return e;                                               /* X, P: not here */
    const unsigned sz = fmt == 5 ? 8u : fmt == 4 ? 2u : fmt == 6 ? ((reg == 7 && mode == 3) ? 2u : 1u) : 4u;
    uint8_t bA = 0xFF;
    if (mode >= 2 && mode <= 6) {
        const uint8_t m = RA_GetMappedARMRegister(8 + reg);
        if ((m & 0x7Fu) != 0x7Fu)
            bA = m & 0x0Fu;
    }
    /* the static address, when there is one */
    int stat = 0;
    uint32_t S = 0;
    if (load && mode == 7) {
        if (reg == 2) { S = (uint32_t)(uintptr_t)(pc + 2) + (uint32_t)(int32_t)(int16_t)BE16(pc[2]); stat = 1; }
        else if (reg == 4) { S = (uint32_t)(uintptr_t)(pc + 2); stat = 1; }
        else if (reg == 0) { S = (uint32_t)(int32_t)(int16_t)BE16(pc[2]); stat = 1; }
        else if (reg == 1) { S = ((uint32_t)BE16(pc[2]) << 16) | BE16(pc[3]); stat = 1; }
    }
    if (store && (jf_lev() & JIT_JF_LEV_FPSTORE) && mode >= 2 && mode <= 5) {
        uint32_t *const n = jm_store_guard(s, e, bA, fmt);      /* step 2b (above): (An), (An)+, -(An), (d16,An) */
        if (n != e)
            return n;
    }
    if (mode != 3 && !stat)
        return e;
    uint32_t *const lim = (e - s > 24) ? s + 24 : e;
    for (uint32_t *q = s; q < lim; q++) {
        unsigned L, k;
        uint8_t rn, rt;
        int32_t off;
        if (load) {
            if (!(k = jm_ldst(*q, &L, &rn, &rt, &off)) || !L || rn == REG_CTX)
                continue;                                           /* (the context's loads: the allocator's) */
            if (fmt == 5) {                                         /* .D: ldr t1,[b,#o] ; ldr t2,[b,#o+4] ; vmov dX,t2,t1 */
                unsigned L2;
                uint8_t rn2, t2;
                int32_t off2;
                if (q + 3 > e || k != JM_W || (INSN_TO_LE(*q) & 0x00400000u) || jm_ldst(q[1], &L2, &rn2, &t2, &off2) != JM_W
                    || !L2 || (INSN_TO_LE(q[1]) & 0x00400000u) || rn2 != rn || off2 != off + 4 || t2 == rt || rt == rn || t2 == rn)
                    return e;
                const uint8_t dX = (INSN_TO_LE(q[2]) & 15u) | ((INSN_TO_LE(q[2]) >> 1) & 16u);
                if (q[2] != fmdrr(dX, rt, t2))
                    return e;
                if (mode == 3) {
                    if (rn != bA || off != 0 || q + 4 > e)
                        return e;
                    uint8_t rd, r1;
                    int32_t v;
                    if (!jm_addimm(q[3], &rd, &r1, &v) || rd != bA || r1 != bA || v != 8 || !jm_reloc_ok(s, e, q, q + 4))
                        return e;
                    uint32_t w[3] = { jm_post(q[0], JM_W, 4), jm_post(q[1] & ~INSN_TO_LE(0xFFFu), JM_W, 4), q[2] };
                    JM_CNT(1);
                    return jm_put(q, 4, w, 3, e);
                }
                uint32_t *q0 = q;
                uint8_t base = rn;
                int32_t o = off;
                if (reg == 4) {
                    uint8_t rd, r1;
                    int32_t v;
                    if (q == s || !jm_addimm(q[-1], &rd, &r1, &v) || rd != rn || r1 != REG_PC || off != 0)
                        return e;
                    q0 = q - 1; base = REG_PC; o = v;
                } else if (reg == 2) {
                    if (rn != REG_PC)
                        return e;
                } else {
                    uint32_t val = 0;
                    unsigned n = 0;
                    const uint32_t *m = 0;
                    for (const uint32_t *p = s; p < q; p++)
                        if (jm_movwt(p, q, rn, &val, &n))
                            m = p;
                    if (!m || !jm_movwt(m, q, rn, &val, &n) || val != S || off != 0)
                        return e;
                }
                if ((S & 3u) || !jm_fast(S) || !jm_fast(S + 7u) || (o & 3) || o > 1020 || o < -1020) {
                    JM_CNT(3);
                    return e;
                }
                if (!jm_reloc_ok(s, e, q0, q + 3))
                    return e;
                uint32_t w[1] = { jm_vldr(1, dX, base, o) };
                JM_CNT(0);
                return jm_put(q0, (unsigned)(q + 3 - q0), w, 1, e);
            }
            if (q + 3 > e)
                return e;
            const uint32_t xm = INSN_TO_LE(q[1]);
            const uint8_t sN = (uint8_t)((((xm >> 16) & 15u) << 1) | ((xm >> 7) & 1u));
            if (q[1] != fmsr(sN, rt))
                return e;
            const uint32_t xc = INSN_TO_LE(q[2]);
            const uint8_t dX = (xc >> 12) & 15u;
            const int cvt = (fmt == 1) ? q[2] == fcvtds(dX, sN) : q[2] == fsitod(dX, sN);
            if (!cvt || rt == rn)
                return e;
            if (mode == 3) {
                if (rn != bA || off != 0 || q + 4 > e)
                    return e;
                uint8_t rd, r1;
                int32_t v;
                if (!jm_addimm(q[3], &rd, &r1, &v) || rd != bA || r1 != bA || v != (int32_t)sz || !jm_reloc_ok(s, e, q, q + 4))
                    return e;
                const uint32_t w[3] = { jm_post(q[0], k, sz), q[1], q[2] };
                JM_CNT(1);
                return jm_put(q, 4, w, 3, e);
            }
            if (fmt != 0 && fmt != 1)
                return e;
            if (k != JM_W || (INSN_TO_LE(*q) & 0x00400000u))
                return e;
            uint32_t *q0 = q;
            uint8_t base = rn;
            int32_t o = off;
            if (reg == 4) {
                uint8_t rd, r1;
                int32_t v;
                if (q == s || !jm_addimm(q[-1], &rd, &r1, &v) || rd != rn || r1 != REG_PC || off != 0)
                    return e;
                q0 = q - 1; base = REG_PC; o = v;
            } else if (reg == 2) {
                if (rn != REG_PC)
                    return e;
            } else {
                uint32_t val = 0;
                unsigned n = 0;
                const uint32_t *m = 0;
                for (const uint32_t *p = s; p < q; p++)
                    if (jm_movwt(p, q, rn, &val, &n))
                        m = p;
                if (!m || !jm_movwt(m, q, rn, &val, &n) || val != S || off != 0)
                    return e;
            }
            if ((S & 3u) || !jm_fast(S) || !jm_fast(S + 3u) || (o & 3) || o > 1020 || o < -1020) {
                JM_CNT(3);
                return e;
            }
            if (!jm_reloc_ok(s, e, q0, q + 2))
                return e;
            const uint32_t w[2] = { jm_vldr(0, sN, base, o), q[2] };
            JM_CNT(0);
            return jm_put(q0, (unsigned)(q + 3 - q0), w, 2, e);
        } else {
            /* stores, (An)+ only */
            if (mode != 3 || bA == 0xFF || !(jf_lev() & JIT_JF_LEV_FPMEM))
                return e;
            if (fmt == 5) {                                         /* vmov lo,hi,dX ; str hi,[b] ; str lo,[b,#4] ; add b,b,#8 */
                const uint32_t x = INSN_TO_LE(*q);
                if ((x & 0x0FF00FD0u) != 0x0C500B10u || (x >> 28) != 0xEu)
                    continue;
                const uint8_t lo = (x >> 12) & 15u, hi = (x >> 16) & 15u, dX = (x & 15u) | ((x >> 1) & 16u);
                unsigned L1, L2;
                uint8_t r1, r2, t1, t2;
                int32_t o1, o2;
                if (*q != fmrrd(hi, lo, dX) || q + 4 > e || jm_ldst(q[1], &L1, &r1, &t1, &o1) != JM_W || L1 || t1 != hi
                    || r1 != bA || o1 != 0 || (INSN_TO_LE(q[1]) & 0x00400000u) || jm_ldst(q[2], &L2, &r2, &t2, &o2) != JM_W
                    || L2 || t2 != lo || r2 != bA || o2 != 4 || (INSN_TO_LE(q[2]) & 0x00400000u))
                    return e;
                uint8_t rd, rr;
                int32_t v;
                if (!jm_addimm(q[3], &rd, &rr, &v) || rd != bA || rr != bA || v != 8 || !jm_reloc_ok(s, e, q, q + 4))
                    return e;
                const uint32_t w[3] = { q[0], jm_post(q[1], JM_W, 4), jm_post(q[2] & ~INSN_TO_LE(0xFFFu), JM_W, 4) };
                JM_CNT(2);
                return jm_put(q, 4, w, 3, e);
            }
            if (!(k = jm_ldst(*q, &L, &rn, &rt, &off)) || L || rn != bA)
                continue;
            if (q == s || q + 2 > e || off != 0)
                return e;
            const uint32_t xm = INSN_TO_LE(q[-1]);
            if ((xm & 0x0FF00F7Fu) != 0x0E100A10u || ((xm >> 12) & 15u) != rt)
                return e;                                           /* vmov t, sN just before it */
            uint8_t rd, rr;
            int32_t v;
            if (!jm_addimm(q[1], &rd, &rr, &v) || rd != bA || rr != bA || v != (int32_t)sz || !jm_reloc_ok(s, e, q, q + 2))
                return e;
            const uint32_t w[1] = { jm_post(q[0], k, sz) };
            JM_CNT(2);
            return jm_put(q, 2, w, 1, e);
        }
    }
    return e;
}

B166_TEXT static int jf_drops_ahead(uint16_t *p)
{
    for (unsigned n = 0; n < 16u; n++) {
        if (jf_stop(p))
            return 0;
        const uint16_t op = BE16(p[0]);
        if ((op & 0xF000) == 0xF000) {
            const unsigned c = jf_class(op, BE16(p[1]));
            if (c == JF_OVER)
                return 1;
            if (c != JF_PASS)
                return 0;
        } else if (M68K_IsBranch(p))
            return 0;
        const int len = M68K_GetINSNLength(p);
        if (len <= 0)
            return 0;
        p += len;
    }
    return 0;
}

/* every line wrap (src/jit_b162.c) runs the instruction through here while 'JF' has a bit on */
B166_TEXT uint32_t *jf_line(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed, jf_emit_t inner)
{
    if (!jf_lazy() && !jf.pend && !(jf_lev() & (JIT_JF_LEV_FPMEM | JIT_JF_LEV_FPSTORE)))
        return inner(ptr, m68k_ptr, insn_consumed);
    uint16_t *const pc = *m68k_ptr;
    const uint16_t op = BE16(pc[0]);
    const unsigned cls = (op & 0xF000) == 0xF000 ? jf_class(op, BE16(pc[1])) : JF_INT;
    uint32_t *spec = 0;
    const int ff = jf_flags_free();
    if (jf.pend) {
        uint8_t mod = 0;
        const uint8_t F = jf_F(&mod);
        if (cls == JF_OTHER || F > 8u) {
            ptr = jf_now(ptr, cls == JF_OTHER ? JF_S_READ : JF_S_PLAIN, ff);
        } else {
            if (!mod)
                (void)RA_ModifyFPSR(&ptr);                          /* cached: marks it, emits nothing */
            spec = ptr;
            ptr = jf_words_x(ptr, F, jf.dr, ff);
        }
    }
    uint32_t *start = ptr;
    jf.fl = 0;
    ptr = inner(ptr, m68k_ptr, insn_consumed);
    if (spec) {
        uint32_t *const T = jf_trailer(start, ptr);
        const int clean = !jf.fl && jf_clean(start, T ? T : ptr);
        if (clean && !T && (cls == JF_OVER || cls == JF_PASS || cls == JF_INT)) {
            ptr = jf_back(spec, start, ptr);                        /* crossed: still owed (or overwritten) */
            start = spec;
            if (cls == JF_OVER) {
                jf.pend = 0;
            }
        } else if (clean && T && cls == JF_INT && jf_F(0) <= 8u && !jf_stop(*m68k_ptr)
                   && ((jf_lev() & JIT_JF_LEV_AS3392) || jf_drops_ahead(*m68k_ptr))) {
            uint32_t tr[5], w[JF_W];
            for (unsigned i = 0; i < 5u; i++)
                tr[i] = T[i];
            uint8_t mod = 0;
            const uint8_t F = jf_F(&mod);
            if (!mod)
                (void)RA_ModifyFPSR(&ptr);                          /* (cached: no word) */
            jf_words_x(w, F, jf.dr, 0);                             /* (the trailer's skip reads the flags after them) */
            uint32_t *const b = jf_back(spec, start, T);            /* the code back over the words ... */
            start = spec;
            ptr = jf_ins(start, b, tr, w, JF_W, 1);                 /* ... and the words on its exit path only */
        } else {
            jf.pend = 0;                                            /* the words stay before it */
        }
    }
    if ((jf_lev() & (JIT_JF_LEV_FPMEM | JIT_JF_LEV_FPSTORE)) && (cls == JF_OVER || cls == JF_PASS) && jf_clean(start, ptr))
        ptr = jm_line(start, ptr, pc, op, BE16(pc[1]));
    if (jf_lazy() && cls == JF_OVER && !jf.pend) {
        uint8_t mod = 0, d = 0;
        const uint8_t F = jf_F(&mod);
        uint32_t *t;
        if (mod && (t = jf_tail(start, ptr, F, &d)) != 0 && jf_clean(start, t)) {
            ptr = t;
            jf.pend = 1;
            jf.dr = d;
        }
    }
    jf_note(start, ptr, pc, *m68k_ptr);
    if (jf.pend && jf_stop(*m68k_ptr))
        ptr = jf_at_end(start, ptr, JF_S_LAB);
    return ptr;
}

/* a path ends (RA_FlushFPURegs): what is owed is computed */
B166_TEXT void jf_flush(uint32_t **arm_stream)
{
    jf.fl = 1;
    if (jf.pend) {
        if (!(jf_lev() & JIT_JF_LEV_AS3392))
            jf_flags_drain(arm_stream);
        *arm_stream = jf_now(*arm_stream, JF_S_END, jf_flags_free());
    }
}

/* a unit starts (RA_ResetM68kAllocator) */
B166_TEXT void jf_reset(void)
{
    jf.pend = 0;
    jf.nlab = 0;
}

#endif /* JF_BUILT */

#if !defined(__aarch64__) && JIT_JU_LEV_DEFAULT
#define B167_TEXT __attribute__((section(".b166_tail_text"), noinline))
#define B167_DATA __attribute__((section(".b166_tail_data")))
static inline uint32_t ju_lev(void) { return JIT_JU_LEV_DEFAULT; }
B167_TEXT uint32_t ju_unroll_cap(uint32_t n, uintptr_t first_copy_bytes)
{
    const uint32_t c = ju_lev() & JIT_JU_UNROLL_MASK;
    if (n <= 1u)
        return n;
    if (!c || !first_copy_bytes)
        return n;
    uint32_t m = (uint32_t)(((uintptr_t)32u << c) / first_copy_bytes);
    if (m < 1u)
        m = 1u;
    if (m >= n)
        return n;
    return m;
}

#endif
