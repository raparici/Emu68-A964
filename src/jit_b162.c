/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* Conditional branches on their producer's ARM flags, lazy condition codes and FPU loops closed in the unit. */

#include "Emu68_ARM_emit.h"
#include <stdint.h>
#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "config.h"
#include "jit_levers.h"

#define B162_TEXT __attribute__((section(".b162_tail_text"), noinline))
#define B162_DATA __attribute__((section(".b162_tail_data")))

#define B162_DECL(n) uint32_t *__real_##n(uint32_t *, uint16_t **, uint16_t *); \
                     uint32_t *__wrap_##n(uint32_t *, uint16_t **, uint16_t *);
B162_DECL(EMIT_line0) B162_DECL(EMIT_move) B162_DECL(EMIT_line4) B162_DECL(EMIT_line5) B162_DECL(EMIT_line6)
B162_DECL(EMIT_moveq) B162_DECL(EMIT_line8) B162_DECL(EMIT_line9) B162_DECL(EMIT_lineB) B162_DECL(EMIT_lineC)
B162_DECL(EMIT_lineD) B162_DECL(EMIT_lineE) B162_DECL(EMIT_lineF)
void __real_RA_FlushFPURegs(uint32_t **arm_stream);
void __real_RA_ResetM68kAllocator(void);
void __wrap_RA_FlushFPURegs(uint32_t **arm_stream);
void __wrap_RA_ResetM68kAllocator(void);

#if !defined(__aarch64__) && defined(EMU68_BCC_INLINE)
#define B162 1
#else
#define B162 0
#endif
#define LZ_CNT(c) ((volatile uint32_t *)0)
#if B162
#if defined(EMU68_INNER_LOOP)
extern uint16_t *g_xlat_unit_start;
extern int       g_xlat_backedge_ok;
#endif

enum { B162_IDLE = 0, B162_PROD, B162_INL };
#define B162_TGT_MAX 256u
static struct {
    uint16_t *expect;                   /* PROD: the Bcc; INL: the instruction the unit goes on with */
    uint16_t *tgt[B162_TGT_MAX];        /* branch targets this unit has emitted (a (c) label may join there) */
    uint32_t  words[8];                 /* the producer's CCR words, as emitted */
    uint16_t  ntgt;                     /* tgt[]'s count; B162_TGT_MAX + 1: full, no more cuts in this unit */
    uint8_t   state, n, cond, w;        /* ...; the words' count; the Bcc's ARM condition; the CCR bits they write */
} b162 B162_DATA;

static inline int b162_on(void) { return 1; }

B162_TEXT static uint32_t *b162_replay(uint32_t *ptr)
{
    for (unsigned i = 0; i < b162.n; i++)
        *ptr++ = b162.words[i];
    b162.state = B162_IDLE;
    return ptr;
}

B162_TEXT static void b162_tgt(uint16_t *pc)
{
    if (b162.ntgt < B162_TGT_MAX)
        b162.tgt[b162.ntgt] = pc;
    if (b162.ntgt <= B162_TGT_MAX)
        b162.ntgt++;
}

B162_TEXT static void b162_note(const uint32_t *start, const uint32_t *end, uint16_t *pc)
{
    if (end - start >= 2 && end[-1] == INSN_TO_LE(0xfffffffe)) {
        const uint16_t op = BE16(pc[0]);
        unsigned len = 0;
        if ((op & 0xF000) == 0x6000)
            len = (op & 0xFF) == 0 ? 2 : (op & 0xFF) == 0xFF ? 3 : 1;      /* Bcc */
        else if ((op & 0xFF80) == 0xF280)
            len = (op & 0x0040) ? 3 : 2;                                   /* FBcc */
        else if ((op & 0xF0F8) == 0x50C8)
            len = 2;                                                       /* DBcc */
        else if ((op & 0xFFF8) == 0xF248)
            len = 3;                                                       /* FDBcc */
        b162_tgt((uint16_t *)(uintptr_t)end[-2]);
        if (len)
            b162_tgt(pc + len);
        else
            b162.ntgt = B162_TGT_MAX + 1;                                  /* an exit of another shape: no more cuts */
    }
}

B162_TEXT static int b162_joined(const uint16_t *pc)
{
    if (b162.ntgt > B162_TGT_MAX)
        return 1;
    for (unsigned i = 0; i < b162.ntgt; i++)
        if (b162.tgt[i] == pc)
            return 1;
    return 0;
}

B162_TEXT static int b162_simple(uint16_t op, int producer)
{
    const unsigned sz = (op >> 6) & 3, mode = (op >> 3) & 7, opm = (op >> 6) & 7;
    switch (op >> 12) {
    case 0x0:
        if (op & 0x0100)
            return producer && mode != 1;                   /* BTST / BCHG / BCLR / BSET Dn,<ea>; not MOVEP */
        if ((op & 0x0F00) == 0x0800)
            return producer;                                /* BTST / BCHG / BCLR / BSET #n,<ea> */
        if (sz == 3 || (op & 0x3F) == 0x3C)
            return 0;                                       /* CAS, CHK2, CMP2; to CCR / SR */
        switch ((op >> 9) & 7) {
        case 0: case 1: case 2: case 3: case 5: case 6:
            return 1;                                       /* ORI ANDI SUBI ADDI EORI CMPI */
        }
        return 0;
    case 0x1: case 0x2: case 0x3:
        return opm != 1;                                    /* MOVE, not MOVEA */
    case 0x4:
        if (sz != 3 && ((op & 0xFF00) == 0x4A00 || (op & 0xFF00) == 0x4200 || (op & 0xFF00) == 0x4400
                        || (op & 0xFF00) == 0x4600))
            return 1;                                       /* TST CLR NEG NOT */
        op &= 0xFFF8;
        return op == 0x4840 || op == 0x4880 || op == 0x48C0 || op == 0x49C0;   /* SWAP EXT EXTB */
    case 0x5:
        return sz != 3 && mode != 1;                        /* ADDQ / SUBQ, not to An */
    case 0x7:
        return !(op & 0x0100);                              /* MOVEQ */
    case 0x8: case 0x9: case 0xC: case 0xD:                 /* OR SUB AND ADD: not DIV / MUL, the A forms, X / BCD / EXG */
        return opm != 3 && opm != 7 && !(opm >= 4 && mode < 2);
    case 0xB:
        return 1;                                           /* CMP CMPA CMPM EOR */
    case 0xE:
        return producer && !(sz == 3 && (op & 0x0800));     /* shifts / rotates, not the bit fields */
    }
    return 0;
}

B162_TEXT static uint32_t b162_sr(uint16_t op)
{
    switch (op >> 12) {
    case 0x0: return GetSR_Line0(op);
    case 0x1: return GetSR_Line1(op);
    case 0x2: return GetSR_Line2(op);
    case 0x3: return GetSR_Line3(op);
    case 0x4: return GetSR_Line4(op);
    case 0x5: return GetSR_Line5(op);
    case 0x7: return GetSR_Line7(op);
    case 0x8: return GetSR_Line8(op);
    case 0x9: return GetSR_Line9(op);
    case 0xB: return GetSR_LineB(op);
    case 0xC: return GetSR_LineC(op);
    case 0xD: return GetSR_LineD(op);
    }
    return (uint32_t)SR_CCR << 16;
}

/* does the instruction at pc overwrite every bit the cut words write, reading none of them? */
B162_TEXT static int b162_kills(const uint16_t *pc)
{
    if ((uintptr_t)pc & 1u)
        return 0;
    const uint16_t op = BE16(pc[0]);
    if (!b162_simple(op, 0))
        return 0;
    const uint32_t f = b162_sr(op);
    const uint8_t sets = f & SR_CCR, needs = (f >> 16) & SR_CCR;
    return !(b162.w & ~sets) && !(b162.w & needs);
}

B162_TEXT static uint32_t *b162_tail(uint32_t *start, uint32_t *end)
{
    uint32_t *p = end;
    while (p > start) {
        const uint32_t x = INSN_TO_LE(p[-1]);
        if ((x & 0x0FFFFF00u) != 0x038AA000u || (x >> 28) >= 0xFu || (x & 0xFFu) > SR_CCR)
            break;                                          /* orr<cc> r10, r10, #b */
        p--;
    }
    if (p == end || p == start || (INSN_TO_LE(p[-1]) & 0xFFFFFF00u) != 0xE3CAA000u || (INSN_TO_LE(p[-1]) & 0xFFu) > SR_CCR)
        return NULL;                                        /* bic r10, r10, #m */
    p--;
    if (end - p > 8)
        return NULL;
    for (const uint32_t *q = start; q < p; q++) {
        const uint32_t x = INSN_TO_LE(*q);
        if ((x >> 28) == 0xFu                               /* the unconditional space, the translator's markers */
            || (x & 0x0E000000u) == 0x0A000000u             /* B, BL */
            || (x & 0x0FFFFFD0u) == 0x012FFF10u             /* BX, BLX */
            || (x & 0x0E108000u) == 0x08108000u)            /* LDM with pc */
            return NULL;
        if ((x & 0x08000000u) == 0 && ((x >> 12) & 15u) == 15u   /* pc written, but for TST / TEQ / CMP / CMN */
            && !((x & 0x0C000000u) == 0 && ((x >> 23) & 3u) == 2u && (x & 0x00100000u)))
            return NULL;
    }
    return p;
}

B162_TEXT static uint8_t b162_cond(const uint32_t *w, unsigned n, unsigned cc, uint8_t *bits)
{
    const uint8_t clr = INSN_TO_LE(w[0]) & 0xFFu;
    int8_t c[4] = { -1, -1, -1, -1 };                       /* C V Z N: the one orr that sets each */
    uint8_t set = 0;
    for (unsigned i = 1; i < n; i++) {
        const uint32_t x = INSN_TO_LE(w[i]);
        const uint8_t b = x & 0xFFu;
        set |= b;
        for (unsigned j = 0; j < 4; j++)
            if (b & (1u << j))
                c[j] = (c[j] == -1 && (clr & (1u << j))) ? (int8_t)(x >> 28) : -2;
    }
    *bits = (clr | set) & SR_CCR;
    const int z = c[2] == ARM_CC_EQ, nf = c[3] == ARM_CC_MI, v = c[1] == ARM_CC_VS;
    const int csub = c[0] == ARM_CC_CC, cadd = c[0] == ARM_CC_CS;
    switch (cc) {
    case M_CC_HI: return z && csub ? ARM_CC_HI : 0xFF;
    case M_CC_LS: return z && csub ? ARM_CC_LS : 0xFF;
    case M_CC_CC: return csub ? ARM_CC_CS : cadd ? ARM_CC_CC : 0xFF;
    case M_CC_CS: return csub ? ARM_CC_CC : cadd ? ARM_CC_CS : 0xFF;
    case M_CC_NE: return z ? ARM_CC_NE : 0xFF;
    case M_CC_EQ: return z ? ARM_CC_EQ : 0xFF;
    case M_CC_VC: return v ? ARM_CC_VC : 0xFF;
    case M_CC_VS: return v ? ARM_CC_VS : 0xFF;
    case M_CC_PL: return nf ? ARM_CC_PL : 0xFF;
    case M_CC_MI: return nf ? ARM_CC_MI : 0xFF;
    case M_CC_GE: return nf && v ? ARM_CC_GE : 0xFF;
    case M_CC_LT: return nf && v ? ARM_CC_LT : 0xFF;
    case M_CC_GT: return z && nf && v ? ARM_CC_GT : 0xFF;
    case M_CC_LE: return z && nf && v ? ARM_CC_LE : 0xFF;
    }
    return 0xFF;
}

/* before any instruction: the pending words run unless this is the instruction that overwrites them */
B162_TEXT static uint32_t *b162_enter(uint32_t *ptr, const uint16_t *pc)
{
    if (b162.state == B162_INL && pc == b162.expect) {
        b162.state = B162_IDLE;
        return ptr;
    }
    if (b162.state != B162_IDLE) {
        ptr = b162_replay(ptr);
    }
    return ptr;
}

/* after an instruction: a producer followed by a Bcc its words define loses them */
B162_TEXT static uint32_t *b162_after(uint32_t *start, uint32_t *ptr, uint16_t *pc, uint16_t **m68k_ptr,
                                      uint16_t consumed)
{
    b162_note(start, ptr, pc);
    if (consumed != 1 || !b162_simple(BE16(pc[0]), 1))
        return ptr;
    uint16_t *nx = *m68k_ptr;
    const uint16_t bop = BE16(nx[0]);
    if ((bop & 0xF000) != 0x6000 || ((bop >> 8) & 15) < 2 || b162_joined(nx))
        return ptr;
    uint32_t *t = b162_tail(start, ptr);
    if (!t)
        return ptr;
    uint8_t bits;
    const uint8_t c = b162_cond(t, (unsigned)(ptr - t), (bop >> 8) & 15, &bits);
    if (c == 0xFF)
        return ptr;
    b162.n = (uint8_t)(ptr - t);
    for (unsigned i = 0; i < b162.n; i++)
        b162.words[i] = t[i];
    b162.cond = c; b162.w = bits; b162.expect = nx; b162.state = B162_PROD;
    return t;
}

B162_TEXT static uint32_t *b162_bcc_pc(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr, uint8_t success_condition,
                                       uint8_t *inl, intptr_t *target)
{
    int8_t local_pc_off = 2;

    ptr = EMIT_GetOffsetPC(ptr, &local_pc_off);
    ptr = EMIT_ResetOffsetPC(ptr);

    uint8_t reg = RA_AllocARMRegister(&ptr);

    intptr_t branch_target = (intptr_t)(*m68k_ptr);
    intptr_t branch_offset = 0;

    if ((opcode & 0x00ff) == 0x00)
        branch_offset = (int16_t)BE16(*(*m68k_ptr)++);
    else if ((opcode & 0x00ff) == 0xff)
    {
        uint16_t lo16, hi16;
        hi16 = BE16(*(*m68k_ptr)++);
        lo16 = BE16(*(*m68k_ptr)++);
        branch_offset = lo16 | (hi16 << 16);
    }
    else
        branch_offset = (int8_t)(opcode & 0xff);

    branch_offset += local_pc_off;

    if (branch_offset > 0 && branch_offset < 255)
        *ptr++ = add_cc_immed(success_condition, REG_PC, REG_PC, branch_offset);
    else if (branch_offset > -256 && branch_offset < 0)
        *ptr++ = sub_cc_immed(success_condition, REG_PC, REG_PC, -branch_offset);
    else if (branch_offset != 0) {
        *ptr++ = movw_cc_immed_u16(success_condition, reg, branch_offset);
        if ((branch_offset >> 16) & 0xffff)
            *ptr++ = movt_cc_immed_u16(success_condition, reg, (branch_offset >> 16) & 0xffff);
        *ptr++ = add_cc_reg(success_condition, REG_PC, REG_PC, reg, 0);
    }
    branch_target += branch_offset - local_pc_off;

    intptr_t local_pc_off_16 = local_pc_off - 2;

    if ((opcode & 0x00ff) == 0x00)
        local_pc_off_16 += 4;
    else if ((opcode & 0x00ff) == 0xff)
        local_pc_off_16 += 6;
    else
        local_pc_off_16 += 2;

    if (local_pc_off_16 > 0 && local_pc_off_16 < 255)
        *ptr++ = add_cc_immed(success_condition ^ 1, REG_PC, REG_PC, local_pc_off_16);
    else if (local_pc_off_16 > -256 && local_pc_off_16 < 0)
        *ptr++ = sub_cc_immed(success_condition ^ 1, REG_PC, REG_PC, -local_pc_off_16);
    else if (local_pc_off_16 != 0) {
        *ptr++ = movw_cc_immed_u16(success_condition ^ 1, reg, local_pc_off_16);
        if ((local_pc_off_16 >> 16) & 0xffff)
            *ptr++ = movt_cc_immed_u16(success_condition ^ 1, reg, local_pc_off_16 >> 16);
        *ptr++ = add_cc_reg(success_condition ^ 1, REG_PC, REG_PC, reg, 0);
    }

#if EMU68_DEF_BRANCH_AUTO
    if(
        branch_target < (intptr_t)*m68k_ptr &&
        ((intptr_t)*m68k_ptr - branch_target) < EMU68_DEF_BRANCH_AUTO_RANGE
    )
        *m68k_ptr = (uint16_t *)branch_target;
#if defined(EMU68_INNER_LOOP)
    else if (g_xlat_backedge_ok && branch_target == (intptr_t)g_xlat_unit_start)
        *m68k_ptr = (uint16_t *)branch_target;
#endif
#else
#if EMU68_DEF_BRANCH_TAKEN
    *m68k_ptr = (uint16_t *)branch_target;
#endif
#endif

    RA_FreeARMRegister(&ptr, reg);

    *inl = (*m68k_ptr == (uint16_t *)branch_target) ? success_condition : (success_condition ^ 1);
    *target = branch_target;
    return ptr;
}

B162_TEXT static uint32_t *b162_trailer(uint32_t *ptr, uint8_t inl, intptr_t branch_target)
{
    uint32_t *tmpptr = ptr;
    *ptr++ = b_cc(inl, 1);
    *ptr++ = (uint32_t)(uintptr_t)tmpptr;
    *ptr++ = 1;
    *ptr++ = branch_target;
    *ptr++ = INSN_TO_LE(0xfffffffe);
    return ptr;
}

B162_TEXT static uint32_t *b162_bcc(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t inl;
    intptr_t branch_target;
    ptr = b162_bcc_pc(ptr, opcode, m68k_ptr, b162.cond, &inl, &branch_target);
    if (b162_kills(*m68k_ptr)) {
        /* the path that goes on overwrites the CCR: the words on the exit path only */
        *ptr++ = b_cc(inl, 1 + b162.n);
        for (unsigned i = 0; i < b162.n; i++)
            *ptr++ = b162.words[i];
        b162.state = B162_INL;
        b162.expect = *m68k_ptr;
    } else {
        ptr = b162_replay(ptr);
    }
    return b162_trailer(ptr, inl, branch_target);
}

static struct {
    uint32_t  words[8];                 /* the pending words */
    uint16_t *lab[B162_TGT_MAX];        /* addresses an exit of this unit may join at */
    uint16_t  nlab;                     /* lab[]'s count; B162_TGT_MAX + 1: full, every address may be one */
    uint8_t   n, w;                     /* the pending words' count (0: none); the CCR bits they write */
} lz B162_DATA;

typedef uint32_t *(*b162_emit_t)(uint32_t *, uint16_t **, uint16_t *);

static inline int lz_on(void) { return 1; }

B162_TEXT static uint32_t *lz_emit(uint32_t *ptr)
{
    for (unsigned i = 0; i < lz.n; i++)
        *ptr++ = lz.words[i];
    return ptr;
}

B162_TEXT static uint32_t *lz_replay(uint32_t *ptr, volatile uint32_t *cnt)
{
    ptr = lz_emit(ptr);
    lz.n = 0;
    return ptr;
}

/* the words emitted at spec moved back: the instruction's own words (start..end) over them */
B162_TEXT static uint32_t *lz_back(uint32_t *spec, uint32_t *start, uint32_t *end)
{
    uint32_t *d = spec;
    for (uint32_t *q = start; q < end; q++)
        *d++ = *q;
    return d;
}

B162_TEXT static void lz_note(const uint32_t *start, const uint32_t *end, const uint16_t *pc, const uint16_t *next)
{
    if (end - start < 2 || end[-1] != INSN_TO_LE(0xfffffffe))
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
    uint16_t *l = (next == tgt && tgt != ft) ? ft : tgt > pc ? tgt : 0;
    if (!l)
        return;
    if (lz.nlab < B162_TGT_MAX)
        lz.lab[lz.nlab] = l;
    if (lz.nlab <= B162_TGT_MAX)
        lz.nlab++;
}

B162_TEXT static int lz_join(const uint16_t *pc)
{
    if (((uintptr_t)pc & 1u) || lz.nlab > B162_TGT_MAX)
        return 1;
    for (unsigned i = 0; i < lz.nlab; i++)
        if (lz.lab[i] == pc)
            return 1;
    return 0;
}

B162_TEXT static int lz_stop(const uint16_t *pc)
{
    return lz_join(pc) || (BE16(pc[0]) & 0xF000) == 0xA000;
}

B162_TEXT static int lz_plain_word(uint32_t x, int flags)
{
    const uint32_t rn = (x >> 16) & 15, rd = (x >> 12) & 15, rs = (x >> 8) & 15, rm = x & 15;
    if ((x >> 28) == 0xFu)
        return 0;
    switch ((x >> 25) & 7) {
    case 0:
        if ((x & 0x90u) == 0x90u) {
            if ((x & 0x0F8000F0u) == 0x00000090u || (x & 0x0F8000F0u) == 0x00800090u)       /* MUL / MLA, long multiplies */
                return (flags || !(x & 0x00100000u)) && rn != 10 && rn != 15 && rd != 10 && rd != 15 && rs != 10
                       && rs != 15 && rm != 10 && rm != 15;
            if ((x & 0x60u) == 0)
                return 0;                                                                     /* SWP, LDREX, ... */
            if (rn == 10 || rn == 15 || rd == 10 || rd == 15 || (!(x & 0x00400000u) && (rm == 10 || rm == 15)))
                return 0;                                                                     /* LDRH / STRH / LDRD ... */
            if ((x & 0x00100040u) == 0x00000040u && (rd == 9 || rd == 14))
                return 0;                                                                     /* LDRD / STRD: rd + 1 */
            if (rn == REG_CTX && (x & 0x00400000u)
                && ((x & 0xFu) | ((x >> 4) & 0xF0u)) == __builtin_offsetof(struct M68KState, SR))
                return 0;
            return 1;
        }
        __attribute__((fallthrough));                           /* data processing, register forms */
    case 1: {
        const uint32_t opc = (x >> 21) & 15, s = (x >> 20) & 1, imm = (x >> 25) & 1;
        if (!s && (opc & 0xC) == 0x8) {                       /* the miscellaneous space: MOVW / MOVT only */
            if (imm && (opc == 8 || opc == 10))
                return rd != 10 && rd != 15;
            return 0;
        }
        if (s && !flags)
            return 0;
        if (rd == 15 || ((opc & 0xC) != 0x8 && rd == 10))
            return 0;
        if (opc != 13 && opc != 15 && (rn == 10 || rn == 15))
            return 0;
        if (!imm && (rm == 10 || rm == 15 || ((x & 0x10u) && (rs == 10 || rs == 15))))
            return 0;
        return 1;
    }
    case 2:                                                     /* LDR / STR immediate */
        if (rn == 10 || rn == 15 || rd == 10 || rd == 15)
            return 0;
        return !(rn == REG_CTX && (x & 0xFFFu) == __builtin_offsetof(struct M68KState, SR));
    case 3:                                                     /* LDR / STR register; the media space */
        if (x & 0x10u)
            return rn != 10 && rd != 10 && rs != 10 && rm != 10 && rd != 15;
        return rn != 10 && rn != 15 && rd != 10 && rd != 15 && rm != 10 && rm != 15;
    case 4:                                                     /* LDM / STM */
        return rn != 10 && rn != 15 && !(x & ((1u << 10) | (1u << 15)));
    case 6:                                                     /* VLDR / VSTR / VLDM / VSTM, VMOV two cores */
        if (((x >> 8) & 0xE) != 0xA)
            return 0;
        if ((x & 0x0FE00000u) == 0x0C400000u)
            return rn != 10 && rn != 15 && rd != 10 && rd != 15;
        return rn != 10 && rn != 15;
    case 7:
        if (x & 0x01000000u)
            return 0;                                           /* SVC */
        if (((x >> 8) & 0xE) != 0xA)
            return 0;                                           /* coprocessors but VFP */
        if (!(x & 0x10u))
            return 1;                                           /* VFP data processing */
        if ((x & 0x0FEF0F10u) == 0x0EE10A10u)                  /* VMRS / VMSR FPSCR */
            return rd != 10 && (rd != 15 || flags);
        return rd != 10 && rd != 15;                            /* VMOV core <-> single */
    }
    return 0;                                                   /* B, BL */
}

B162_TEXT static int lz_plain(const uint32_t *s, const uint32_t *e, int flags)
{
    int set = 0;                                                /* a conditional word reads the flags its own code set */
    for (; s < e; s++) {
        const uint32_t x = INSN_TO_LE(*s);
        if (!lz_plain_word(x, flags) || ((x >> 28) != 0xEu && !set))
            return 0;
        if ((!((x >> 25) & 6) && (x & 0x00100000u) && !((x & 0x0E000090u) == 0x00000090u && (x & 0x60u)))
            || (x & 0x0FFFFFFFu) == 0x0EF1FA10u)
            set = 1;
    }
    return 1;
}

B162_TEXT static int lz_traps(uint16_t op)
{
    switch (op >> 12) {
    case 0x0:
        return (op & 0x00BF) == 0x003C || (op & 0xF9C0) == 0x00C0 || ((op & 0xF9C0) == 0x08C0 && (op & 0x0600))
               || (op & 0xFF00) == 0x0E00;                      /* to CCR / SR, CHK2 / CMP2, CAS, MOVES */
    case 0x4:
        return (op & 0xF9C0) == 0x40C0 || (op & 0xF140) == 0x4100 || (op & 0xFF80) == 0x4C00 || op == 0x4AFC
               || (op & 0xFFF8) == 0x4848 || (op & 0xFFC0) == 0x4E40 || (op & 0xFFE0) == 0x4E60
               || (op & 0xFFF0) == 0x4E70;
    case 0x5:
        return (op & 0xF0F8) == 0x50F8;                         /* TRAPcc */
    case 0x8:
        return (op & 0x00C0) == 0x00C0;
    case 0xA: case 0xF:
        return 1;
    }
    return 0;
}

B162_TEXT static uint32_t lz_sr(uint16_t op)
{
    switch (op >> 12) {
    case 0x6: return GetSR_Line6(op);
    case 0xE: return GetSR_LineE(op);
    }
    return b162_sr(op);
}

/* what the pending words do at the instruction at pc (c: the ARM condition of a Bcc / Scc they answer) */
enum { LZ_SPEC = 0, LZ_REPLAY, LZ_KILL, LZ_BCC, LZ_SCC };
B162_TEXT static unsigned lz_pre(const uint16_t *pc, uint8_t *c)
{
    const uint16_t op = BE16(pc[0]);
    const unsigned cc = (op >> 8) & 15;
    uint8_t bits;
    if ((op & 0xF000) == 0x6000 && cc >= 2)
        return (*c = b162_cond(lz.words, lz.n, cc, &bits)) != 0xFF ? LZ_BCC : LZ_REPLAY;
    if ((op & 0xF0F8) == 0x50C0 && cc >= 2 && (*c = b162_cond(lz.words, lz.n, cc, &bits)) != 0xFF)
        return LZ_SCC;
    if (lz_traps(op))
        return LZ_REPLAY;
    const uint32_t f = lz_sr(op);
    const uint8_t needs = (f >> 16) & SR_CCR, sets = f & SR_CCR;
    if (needs & lz.w)
        return LZ_REPLAY;
    if (!sets)
        return LZ_SPEC;
    if (lz.w & ~sets)
        return LZ_REPLAY;                                       /* overwrites part of them */
    return b162_simple(op, 0) ? LZ_KILL : LZ_SPEC;             /* the pilot's b162_kills: the CCR scan calls them dead */
}

B162_TEXT static int lz_scc(uint32_t **pp, uint16_t op, uint16_t **m68k_ptr, uint16_t *insn_consumed, uint8_t c)
{
    uint32_t *const spec = *pp, *ptr = lz_emit(*pp), *const s = ptr;
    const uint8_t dest = RA_MapM68kRegister(&ptr, op & 7);
    RA_SetDirtyM68kRegister(&ptr, op & 7);
    if (!lz_plain(s, ptr, 0)) {
        lz.n = 0;
        *pp = ptr;
        return 0;
    }
    ptr = lz_back(spec, s, ptr);
    (*m68k_ptr)++;
    *insn_consumed = 1;
    *ptr++ = orr_cc_immed(c, dest, dest, 0xff);
    *ptr++ = bfc_cc(c ^ 1, dest, 0, 8);
    *pp = EMIT_AdvancePC(ptr, 2);
    return 1;
}

B162_TEXT static uint32_t *lz_line(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed, b162_emit_t real)
{
    uint16_t *const pc = *m68k_ptr;
    const uint16_t op = BE16(pc[0]);
    uint32_t *start = ptr, *spec = NULL;
    if (lz.n) {
        uint8_t c = 0xFF, nc;
        switch (lz_pre(pc, &c)) {
        case LZ_BCC: {
            uint8_t inl;
            intptr_t target;
            *insn_consumed = 1;
            (*m68k_ptr)++;
            ptr = b162_bcc_pc(ptr, op, m68k_ptr, c, &inl, &target);
            if (lz_stop(*m68k_ptr) || lz_pre(*m68k_ptr, &nc) == LZ_REPLAY)
                ptr = lz_replay(ptr, LZ_CNT(g_b162_lz_lab));      /* the path that goes on needs them: both paths */
            else {
                *ptr++ = b_cc(inl, 1 + lz.n);
                ptr = lz_emit(ptr);                             /* the exit path's */
            }
            ptr = b162_trailer(ptr, inl, target);
            lz_note(start, ptr, pc, *m68k_ptr);
            return ptr;
        }
        case LZ_SCC:
            if (lz_scc(&ptr, op, m68k_ptr, insn_consumed, c))
                goto ahead;
            goto emit;
        case LZ_KILL:
            lz.n = 0;
            break;
        case LZ_SPEC:
            spec = ptr;                                         /* the words first: moved back if not needed */
            ptr = lz_emit(ptr);
            break;
        default:
            ptr = lz_replay(ptr, LZ_CNT(g_b162_lz_rep));
        }
    }
emit:
    start = ptr;
    ptr = real(ptr, m68k_ptr, insn_consumed);
    if (spec) {
        uint32_t *t = b162_tail(start, ptr);
        if (!t && lz_plain(start, ptr, 0)) {
            ptr = lz_back(spec, start, ptr);                   /* crossed: the words stay pending */
            start = spec;
        } else if (t && (INSN_TO_LE(t[0]) & lz.w) == lz.w && lz_plain(start, t, 1)) {
            ptr = lz_back(spec, start, ptr);                   /* overwritten by this producer's own words */
            start = spec;
            lz.n = 0;
        } else {
            lz.n = 0;
        }
    }
    lz_note(start, ptr, pc, *m68k_ptr);
    if (!lz.n && (op & 0xF000) != 0x6000) {
        uint32_t *t = b162_tail(start, ptr);
        if (t) {
            uint8_t m = 1;
            lz.words[0] = t[0];
            for (uint32_t *q = t + 1; q < ptr; q++) {
                unsigned j = 1;
                while (j < m && lz.words[j] != *q)
                    j++;
                if (j == m)
                    lz.words[m++] = *q;
            }
            uint8_t set = 0;
            for (unsigned i = 1; i < m; i++)
                set |= INSN_TO_LE(lz.words[i]) & 0xFFu;
            lz.n = m;
            lz.w = (INSN_TO_LE(t[0]) | set) & SR_CCR;
            ptr = t;
        }
    }
ahead:
    if (lz.n && lz_stop(*m68k_ptr))
        ptr = lz_replay(ptr, LZ_CNT(g_b162_lz_lab));
    return ptr;
}
B162_TEXT static void lz_flush(uint32_t **arm_stream)
{
    if (lz.n)
        *arm_stream = lz_replay(*arm_stream, LZ_CNT(g_b162_lz_end));
}
static inline void lz_reset(void) { lz.n = 0; lz.nlab = 0; }

#if JIT_JF_LEV_DEFAULT
int jf_fbf_nop(uint32_t **pptr, uint16_t **m68k_ptr, uint16_t *insn_consumed);
#define FPL_FBF(p, m, c) jf_fbf_nop((p), (m), (c))
#else
#define FPL_FBF(p, m, c) 0
#endif
#if defined(EMU68_INNER_LOOP)
static inline int fpl_on(void) { return 1; }   /* JIT_R10_LEV_DEFAULT has bits 1 and 3 */

B162_TEXT static uint32_t *fpl_lineF(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    const uint16_t op = BE16((*m68k_ptr)[0]);
    if (FPL_FBF(&ptr, m68k_ptr, insn_consumed))
        return ptr;
    uint32_t *const start = ptr;
    ptr = __real_EMIT_lineF(ptr, m68k_ptr, insn_consumed);
    if (fpl_on() && (op & 0xFF80) == 0xF280 && g_xlat_backedge_ok
        && ptr - start >= 5 && ptr[-1] == INSN_TO_LE(0xfffffffe) && ptr[-3] == 0u
        && (uint16_t *)(uintptr_t)ptr[-2] == g_xlat_unit_start && *m68k_ptr != g_xlat_unit_start) {
        *m68k_ptr = g_xlat_unit_start;
    }
    return ptr;
}
#else
#define fpl_lineF __real_EMIT_lineF
#endif

#if JIT_JF_LEV_DEFAULT
#define JF_HOOK 1
typedef uint32_t *(*jf_emit_t)(uint32_t *, uint16_t **, uint16_t *);
uint32_t *jf_line(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed, jf_emit_t inner);
void jf_flush(uint32_t **arm_stream);
void jf_reset(void);
static inline int jf_on(void) { return 1; }
/* (the copies in src/jit_b166.c's section, LAST: this file's tail grows by the wraps' test only) */
#define B166J_TEXT __attribute__((section(".b166_tail_text"), noinline))
#define JF_COPY(n) B166J_TEXT static uint32_t *b162j_##n(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)     \
                   { return b162i_##n(ptr, m68k_ptr, insn_consumed); }
#define B162_JF(n) (jf_on() ? jf_line(ptr, m68k_ptr, insn_consumed, b162j_##n) : b162i_##n(ptr, m68k_ptr, insn_consumed))
B166J_TEXT int jf_flags_free(void) { return !lz.n && b162.state == B162_IDLE; }
B166J_TEXT void jf_flags_drain(uint32_t **arm_stream)
{
    lz_flush(arm_stream);
    if (b162.state != B162_IDLE) {
        {
            *arm_stream = b162_replay(*arm_stream);
        }
    }
}
#else
#define JF_HOOK 0
#define JF_COPY(n)
#define B162_JF(n) b162i_##n(ptr, m68k_ptr, insn_consumed)
#endif

#define B162_LINE(n) B162_LINEX(n, __real_##n)
#if !JF_HOOK
#define B162_LINEX(n, real)                                                                             \
B162_TEXT uint32_t *__wrap_##n(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)            \
{                                                                                                       \
    if (!b162_on())                                                                                     \
        return real(ptr, m68k_ptr, insn_consumed);                                                      \
    if (lz_on())                                                                                        \
        return lz_line(ptr, m68k_ptr, insn_consumed, real);                                             \
    uint16_t *pc = *m68k_ptr;                                                                           \
    ptr = b162_enter(ptr, pc);                                                                          \
    uint32_t *start = ptr;                                                                              \
    ptr = real(ptr, m68k_ptr, insn_consumed);                                                           \
    return b162_after(start, ptr, pc, m68k_ptr, *insn_consumed);                                        \
}
#else
#define B162_LINEX(n, real)                                                                             \
B166J_TEXT static uint32_t *b162j_##n(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)      \
{                                                                                                       \
    if (!b162_on())                                                                                     \
        return real(ptr, m68k_ptr, insn_consumed);                                                      \
    if (lz_on())                                                                                        \
        return lz_line(ptr, m68k_ptr, insn_consumed, real);                                             \
    uint16_t *pc = *m68k_ptr;                                                                           \
    ptr = b162_enter(ptr, pc);                                                                          \
    uint32_t *start = ptr;                                                                              \
    ptr = real(ptr, m68k_ptr, insn_consumed);                                                           \
    return b162_after(start, ptr, pc, m68k_ptr, *insn_consumed);                                        \
}                                                                                                       \
B162_TEXT uint32_t *__wrap_##n(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)            \
{                                                                                                       \
    if (jf_on())                                                                                        \
        return jf_line(ptr, m68k_ptr, insn_consumed, b162j_##n);                                        \
    return b162j_##n(ptr, m68k_ptr, insn_consumed);                                                     \
}
#endif
B162_LINE(EMIT_line0) B162_LINE(EMIT_move) B162_LINE(EMIT_line4) B162_LINE(EMIT_line5) B162_LINE(EMIT_moveq)
B162_LINE(EMIT_line8) B162_LINE(EMIT_line9) B162_LINE(EMIT_lineB) B162_LINE(EMIT_lineC) B162_LINE(EMIT_lineD)
B162_LINE(EMIT_lineE) B162_LINEX(EMIT_lineF, fpl_lineF)

#if !JF_HOOK
B162_TEXT uint32_t *__wrap_EMIT_line6(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
#else
B166J_TEXT static uint32_t *b162j_EMIT_line6(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
#endif
{
    if (!b162_on())
        return __real_EMIT_line6(ptr, m68k_ptr, insn_consumed);
    if (lz_on())
        return lz_line(ptr, m68k_ptr, insn_consumed, __real_EMIT_line6);
    uint16_t *pc = *m68k_ptr;
    const uint16_t op = BE16(pc[0]);
    uint32_t *start = ptr;
    if (b162.state == B162_PROD && pc == b162.expect && ((op >> 8) & 15) >= 2) {
        *insn_consumed = 1;
        (*m68k_ptr)++;
        ptr = b162_bcc(ptr, op, m68k_ptr);
    } else {
        ptr = b162_enter(ptr, pc);
        start = ptr;
        ptr = __real_EMIT_line6(ptr, m68k_ptr, insn_consumed);
    }
    b162_note(start, ptr, pc);
    return ptr;
}
#if JF_HOOK
B162_TEXT uint32_t *__wrap_EMIT_line6(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    if (jf_on())
        return jf_line(ptr, m68k_ptr, insn_consumed, b162j_EMIT_line6);
    return b162j_EMIT_line6(ptr, m68k_ptr, insn_consumed);
}
#endif

B162_TEXT void __wrap_RA_FlushFPURegs(uint32_t **arm_stream)
{
#if JF_HOOK
    if (jf_on())
        jf_flush(arm_stream);
#endif
    lz_flush(arm_stream);
    if (b162.state != B162_IDLE) {
        {
            *arm_stream = b162_replay(*arm_stream);
        }
    }
    __real_RA_FlushFPURegs(arm_stream);
}

B162_TEXT void __wrap_RA_ResetM68kAllocator(void)
{
    b162.state = B162_IDLE;
    b162.ntgt = 0;
    lz_reset();
#if JF_HOOK
    jf_reset();
#endif
    __real_RA_ResetM68kAllocator();
}

#else

#define B162_LINE(n)                                                                                    \
B162_TEXT uint32_t *__wrap_##n(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)            \
{                                                                                                       \
    return __real_##n(ptr, m68k_ptr, insn_consumed);                                                    \
}
B162_LINE(EMIT_line0) B162_LINE(EMIT_move) B162_LINE(EMIT_line4) B162_LINE(EMIT_line5) B162_LINE(EMIT_line6)
B162_LINE(EMIT_moveq) B162_LINE(EMIT_line8) B162_LINE(EMIT_line9) B162_LINE(EMIT_lineB) B162_LINE(EMIT_lineC)
B162_LINE(EMIT_lineD) B162_LINE(EMIT_lineE) B162_LINE(EMIT_lineF)
B162_TEXT void __wrap_RA_FlushFPURegs(uint32_t **arm_stream) { __real_RA_FlushFPURegs(arm_stream); }
B162_TEXT void __wrap_RA_ResetM68kAllocator(void) { __real_RA_ResetM68kAllocator(); }

#endif
