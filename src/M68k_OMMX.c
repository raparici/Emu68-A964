/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* OMMX: the vector extension's decoder and emitters, and ONCALL. */

#include "Emu68_ARM_emit.h"

#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "EmuFeatures.h"
#include "OMMX.h"
#ifdef EMU68_A3D
#include "jit_levers.h"
#endif

int EnableOMMX = 0;

#ifndef EMU68_A3D_TRAPPATH
#define EMU68_A3D_TRAPPATH 0
#endif
volatile uint32_t g_oncall_on = 0;
volatile uint32_t g_oncall_trap = EMU68_A3D_TRAPPATH;

static uint8_t ommx_touch_emitted = 0;
void OMMX_UnitStart(void) { ommx_touch_emitted = 0; }

#ifndef EMU68_OMMX

int OMMX_IsImplemented(uint16_t w1, uint16_t w2) { (void)w1; (void)w2; return 0; }

#else /* EMU68_OMMX */

struct ommx_insn {
    uint16_t w1, w2;
    uint8_t  A, Bb, Db;         /* word-1 selector bits                                       */
    uint8_t  vea, mode, reg;    /* VEA field and its mode/reg                                  */
    uint8_t  rb, rd;            /* REG-B / REG-D as JIT register numbers 0-31                  */
    uint8_t  op;                /* word-2 bits 7:0                                             */
    uint8_t  is_vperm;
    uint8_t  ra;                /* VPERM's third register (w2[3:0] with the A bank)            */
};

static void ommx_decode(uint16_t w1, uint16_t w2, struct ommx_insn *i)
{
    i->w1 = w1; i->w2 = w2;
    i->A  = OMMX_W1_A(w1); i->Bb = OMMX_W1_B(w1); i->Db = OMMX_W1_D(w1);
    i->vea = OMMX_W1_VEA(w1); i->mode = i->vea >> 3; i->reg = i->vea & 7;
    i->rb = OMMX_RegFromField(OMMX_W2_REGB(w2), i->Bb);
    i->rd = OMMX_RegFromField(OMMX_W2_REGD(w2), i->Db);
    i->op = OMMX_W2_OP(w2);
    i->is_vperm = OMMX_IsVperm(w1, w2);
    i->ra = OMMX_RegFromField(w2 & 0xf, i->A);
}

static int ommx_vea_ok(const struct ommx_insn *i, int allow_imm)
{
    if (i->mode <= 1) return 1;                         /* register-direct, either bank         */
    if (i->A) return (i->mode == 7 && i->reg == 4 && allow_imm);
    if (i->mode <= 6) return 1;
    if (i->mode == 7) return (i->reg <= 3) || (i->reg == 4 && allow_imm);
    return 0;
}

int OMMX_IsImplemented(uint16_t w1, uint16_t w2)
{
    struct ommx_insn i;
#ifdef EMU68_A3D
    if (w1 == OMMX_ONCALL_W1 && w2 == OMMX_ONCALL_W2)
        return g_oncall_on != 0;
#endif
    ommx_decode(w1, w2, &i);
    if (i.is_vperm) return 1;
    switch (i.op) {
        case OMMX_OP_LOAD:     return (OMMX_W2_REGB(w2) == 0) && ommx_vea_ok(&i, 1);
        case OMMX_OP_STORE:    return (OMMX_W2_REGD(w2) == 0) && ommx_vea_ok(&i, 0);
        case OMMX_OP_TRANSHI:
        case OMMX_OP_TRANSLO:  return (OMMX_W2_REGB(w2) == 0) && (i.vea & 0x33) == 0 && !(i.rd & 1);
        case OMMX_OP_PACKUSWB:
        case OMMX_OP_PACK3216: return ommx_vea_ok(&i, 0);
        case OMMX_OP_PAND:
        case OMMX_OP_POR:
        case OMMX_OP_PEOR:
        case OMMX_OP_PANDN:
        case OMMX_OP_PAVGB:
        case OMMX_OP_PADDB:  case OMMX_OP_PADDW:
        case OMMX_OP_PSUBB:  case OMMX_OP_PSUBW:
        case OMMX_OP_PADDUSB: case OMMX_OP_PADDUSW:
        case OMMX_OP_PSUBUSB: case OMMX_OP_PSUBUSW:
        case OMMX_OP_PMUL88:
        case OMMX_OP_PMULA:
        case OMMX_OP_PMULH:
        case OMMX_OP_PMULL:
        case OMMX_OP_BSEL:
        case OMMX_OP_C2P:
        case OMMX_OP_LSLQ:
        case OMMX_OP_LSRQ:     return ommx_vea_ok(&i, 1);
        case OMMX_OP_STOREM:
        case OMMX_OP_STOREC:
        case OMMX_OP_STOREILM:
        case OMMX_OP_STOREM3:  return ommx_vea_ok(&i, 0);
        case OMMX_OP_MINTERM:  return (OMMX_W2_REGB(w2) == 0) && (i.vea & 0x33) == 0;
        case OMMX_OP_BFLYB:
        case OMMX_OP_BFLYW:    return ommx_vea_ok(&i, 1) && !(i.rd & 1);
        case OMMX_OP_OADD_L:
        case OMMX_OP_OSUB_L:
        case OMMX_OP_OMUL_L:
        case OMMX_OP_OMLA_L:
        case OMMX_OP_OPADD_L:
                               return !OMMX_W1_IS_AMMX(w1) && ommx_vea_ok(&i, 1)
                                      && !(i.A && i.mode == 7 && i.reg == 4);
        case OMMX_OP_OSRAR_L:
        case OMMX_OP_OSRA_L:   return !OMMX_W1_IS_AMMX(w1) && i.A && i.mode == 7 && i.reg == 4;
        case OMMX_OP_OIDENT:
                               return !OMMX_W1_IS_AMMX(w1) && (w1 & 0x03f8) == 0 && w2 == 0x00E0;
        default:
            if (i.op >= OMMX_OP_PCMP_BASE && i.op <= 0x2f) {
                uint8_t cc = (i.op >> 1) & 7;
                return (cc == 0 || cc == 1 || cc == 6 || cc == 7) && ommx_vea_ok(&i, 1);
            }
            if (i.op >= OMMX_OP_PMIN_BASE && i.op <= 0x37) return ommx_vea_ok(&i, 1);
            return 0;
    }
}

#define OFF_E(k)     (__builtin_offsetof(struct M68KState, E[k]))
#define OFF_DHI(n)   (__builtin_offsetof(struct M68KState, D_hi[n]))

/* read architectural register r (0-31) into a NEON register; *is_scratch tells the caller to free */
static uint8_t ommx_rd_reg(uint32_t **p, uint8_t r, uint8_t *is_scratch)
{
    if (r >= OMMX_REG_E8) {
        *is_scratch = 0;
        return RA_MapOMMXRegister(p, r - 8);            /* E8..E23 -> e_reg 8..23 -> d16+ */
    }
    uint8_t d = RA_AllocFPURegister(p);
    *is_scratch = 1;
    if (r >= OMMX_REG_E0) {
        **p = fldd(d, REG_CTX, OFF_E(r - 8) / 4); (*p)++;
    } else {
        uint8_t lo = RA_MapM68kRegister(p, r);
        uint8_t hi = RA_AllocARMRegister(p);
        **p = ldr_offset(REG_CTX, hi, OFF_DHI(r)); (*p)++;
        **p = fmdrr(d, hi, lo); (*p)++;
        RA_FreeARMRegister(p, hi);
    }
    return d;
}

static uint8_t ommx_dst_reg(uint32_t **p, uint8_t r, uint8_t *is_scratch)
{
    if (r >= OMMX_REG_E8) { *is_scratch = 0; return RA_MapOMMXRegisterForWrite(p, r - 8); }
    *is_scratch = 1;
    return RA_AllocFPURegister(p);
}

/* commit the NEON register d (from ommx_dst_reg) to architectural register r */
static void ommx_wr_reg(uint32_t **p, uint8_t r, uint8_t d)
{
    if (r >= OMMX_REG_E8) return;                       /* already the register itself, marked dirty */
    if (r >= OMMX_REG_E0) {
        **p = fstd(d, REG_CTX, OFF_E(r - 8) / 4); (*p)++;
    } else {
        uint8_t lo = RA_MapM68kRegisterForWrite(p, r);
        uint8_t hi = RA_AllocARMRegister(p);
        **p = fmrrd(hi, lo, d); (*p)++;
        **p = str_offset(REG_CTX, hi, OFF_DHI(r)); (*p)++;
        RA_SetDirtyM68kRegister(p, r);
        RA_FreeARMRegister(p, hi);
    }
}

static void ommx_const64(uint32_t **p, uint8_t d, uint64_t v)
{
    uint16_t h = (uint16_t)v;
    if (v == ((uint64_t)h << 48 | (uint64_t)h << 32 | (uint64_t)h << 16 | h)) {  /* halfword splat */
        if ((h & 0xff00) == 0)      { **p = vmov_i16(d, (uint8_t)h); (*p)++; return; }
        if ((h & 0x00ff) == 0)      { **p = vmov_i16h(d, (uint8_t)(h >> 8)); (*p)++; return; }
        if ((h >> 8) == (h & 0xff)) { **p = vmov_i8(d, (uint8_t)h); (*p)++; return; }
    }
    **p = fldd(d, 15, 0); (*p)++;                       /* VLDR Dd,[pc,#0] -> the two words below  */
    **p = b_cc(ARM_CC_AL, 3); (*p)++;
    **p = BE32((uint32_t)(v >> 32)); (*p)++;             /* big-endian data words in the stream     */
    **p = BE32((uint32_t)v); (*p)++;
}

static void ommx_ld64(uint32_t **p, uint8_t d, uint8_t base, int32_t off)
{
    uint8_t t1 = RA_AllocARMRegister(p), t2 = RA_AllocARMRegister(p);
    if (off > -4096 && off < 4096 - 4) {
        **p = ldr_offset(base, t1, off); (*p)++;
        **p = ldr_offset(base, t2, off + 4); (*p)++;
    } else {
        uint8_t a = RA_AllocARMRegister(p);
        **p = movw_immed_u16(a, off & 0xffff); (*p)++;
        if ((off >> 16) & 0xffff) { **p = movt_immed_u16(a, (off >> 16) & 0xffff); (*p)++; }
        **p = add_reg(a, base, a, LSL, 0); (*p)++;
        **p = ldr_offset(a, t1, 0); (*p)++;
        **p = ldr_offset(a, t2, 4); (*p)++;
        RA_FreeARMRegister(p, a);
    }
    **p = fmdrr(d, t1, t2); (*p)++;                      /* bytes 0-3 -> bits 63:32           */
    RA_FreeARMRegister(p, t2); RA_FreeARMRegister(p, t1);
}

static void ommx_st64(uint32_t **p, uint8_t d, uint8_t base, int32_t off)
{
    uint8_t t1 = RA_AllocARMRegister(p), t2 = RA_AllocARMRegister(p);
    **p = fmrrd(t1, t2, d); (*p)++;
    if (off > -4096 && off < 4096 - 4) {
        **p = str_offset(base, t1, off); (*p)++;
        **p = str_offset(base, t2, off + 4); (*p)++;
    } else {
        uint8_t a = RA_AllocARMRegister(p);
        **p = movw_immed_u16(a, off & 0xffff); (*p)++;
        if ((off >> 16) & 0xffff) { **p = movt_immed_u16(a, (off >> 16) & 0xffff); (*p)++; }
        **p = add_reg(a, base, a, LSL, 0); (*p)++;
        **p = str_offset(a, t1, 0); (*p)++;
        **p = str_offset(a, t2, 4); (*p)++;
        RA_FreeARMRegister(p, a);
    }
    RA_FreeARMRegister(p, t2); RA_FreeARMRegister(p, t1);
}

static uint32_t *ommx_fetch_vea(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr,
                                uint8_t *ext, uint8_t *out_reg, uint8_t *is_scratch)
{
    if (i->mode <= 1) {                                                   /* register-direct */
        *out_reg = ommx_rd_reg(&ptr, OMMX_RegFromVea(i->vea, i->A), is_scratch);
        return ptr;
    }
    if (i->mode == 7 && i->reg == 4) {                                    /* immediate       */
        uint64_t v;
        if (i->A) {
            uint16_t h = BE16(m68k_ptr[(*ext)++]);
            v = ((uint64_t)h << 48) | ((uint64_t)h << 32) | ((uint64_t)h << 16) | h;
        } else {                                                          /* #imm.Q          */
            v = 0;
            for (int k = 0; k < 4; k++) v = (v << 16) | BE16(m68k_ptr[(*ext)++]);
        }
        uint8_t d = RA_AllocFPURegister(&ptr);
        ommx_const64(&ptr, d, v);
        *out_reg = d; *is_scratch = 1;
        return ptr;
    }
    /* memory: address into a GPR, then the two-word load */
    uint8_t d = RA_AllocFPURegister(&ptr);
    uint8_t base = 0xff; int32_t off = 0;
    if (i->mode == 3 || i->mode == 4) {
        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base, i->vea, m68k_ptr, ext, 0, NULL);
        if (i->mode == 4) { *ptr++ = sub_immed(base, base, 8); }
        ommx_ld64(&ptr, d, base, 0);
        if (i->mode == 3) { *ptr++ = add_immed(base, base, 8); }
        RA_SetDirtyM68kRegister(&ptr, 8 + i->reg);
    } else {
        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base, i->vea, m68k_ptr, ext, 1, &off);
        ommx_ld64(&ptr, d, base, off);
    }
    RA_FreeARMRegister(&ptr, base);
    *out_reg = d; *is_scratch = 1;
    return ptr;
}

/* Store NEON register d to the <vea> DESTINATION (STORE, PACKUSWB, PACK3216). */
static uint32_t *ommx_store_vea(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr,
                                uint8_t *ext, uint8_t d)
{
    if (i->mode <= 1) {                                                   /* register-direct */
        uint8_t r = OMMX_RegFromVea(i->vea, i->A);
        if (r >= OMMX_REG_E8) {
            uint8_t dst = RA_MapOMMXRegisterForWrite(&ptr, r - 8);
            if (dst != d) *ptr++ = vmov_d(dst, d);
        } else {
            ommx_wr_reg(&ptr, r, d);
        }
        return ptr;
    }
    uint8_t base = 0xff; int32_t off = 0;
    if (i->mode == 3 || i->mode == 4) {
        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base, i->vea, m68k_ptr, ext, 0, NULL);
        if (i->mode == 4) { *ptr++ = sub_immed(base, base, 8); }
        ommx_st64(&ptr, d, base, 0);
        if (i->mode == 3) { *ptr++ = add_immed(base, base, 8); }
        RA_SetDirtyM68kRegister(&ptr, 8 + i->reg);
    } else {
        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base, i->vea, m68k_ptr, ext, 1, &off);
        ommx_st64(&ptr, d, base, off);
    }
    RA_FreeARMRegister(&ptr, base);
    return ptr;
}

static uint32_t *EMIT_OMMX_LineF(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t opcode)
{
    ptr = EMIT_FlushPC(ptr);
    ptr = EMIT_InjectDebugString(ptr, "[JIT] OMMX opcode %04x at %08x not implemented\n", opcode, *m68k_ptr);
    ptr = EMIT_Exception(ptr, VECTOR_LINE_F, 0);
    *ptr++ = INSN_TO_LE(0xffffffff);
    return ptr;
}

#define FREE_IF(scr, r)  do { if (scr) RA_FreeFPURegister(&ptr, (r)); } while (0)

/* LOAD <vea>,d */
static uint32_t *emit_load(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t src, sscr;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &src, &sscr);
    if (i->rd >= OMMX_REG_E8) {
        uint8_t dst = RA_MapOMMXRegisterForWrite(&ptr, i->rd - 8);
        if (dst != src) *ptr++ = vmov_d(dst, src);
    } else {
        ommx_wr_reg(&ptr, i->rd, src);
    }
    FREE_IF(sscr, src);
    return ptr;
}

static uint32_t *emit_oident(uint32_t *ptr, const struct ommx_insn *i)
{
    uint8_t dn = RA_MapM68kRegisterForWrite(&ptr, i->vea & 7);
    const uint32_t word = OMMX_IDENT_WORD | (g_oncall_on ? OMMX_FEAT_ONCALL : 0u);
    *ptr++ = movw_immed_u16(dn, word & 0xffff);
    *ptr++ = movt_immed_u16(dn, (word >> 16) & 0xffff);
    RA_SetDirtyM68kRegister(&ptr, i->vea & 7);
    return ptr;
}

#ifdef EMU68_A3D
extern uint32_t a3d_oncall_jit(uint32_t *regs, uint32_t id);

_Static_assert(__builtin_offsetof(struct M68KState, ONCALL_REQ) < 4096
               && __builtin_offsetof(struct M68KState, ONCALL_ID) < 4096,
               "ONCALL_REQ/ID must stay within str's 12-bit immediate");

static uint32_t *emit_oncall_trap(uint32_t *ptr, uint16_t id)
{
    uint8_t ctx = RA_GetCTX(&ptr);
    uint8_t tmp = RA_AllocARMRegister(&ptr);
    uint8_t adr = RA_AllocARMRegister(&ptr);
    const uint32_t a = (uint32_t)(uintptr_t)&ipl_poll_countdown;
    *ptr++ = movw_immed_u16(tmp, id);
    *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, ONCALL_ID));
    *ptr++ = mov_immed_u8(tmp, 1);
    *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, ONCALL_REQ));
    *ptr++ = movw_immed_u16(adr, a & 0xffffu);
    *ptr++ = movt_immed_u16(adr, a >> 16);
    *ptr++ = mov_immed_u8(tmp, 0);
    *ptr++ = str_offset(adr, tmp, 0);                     /* ipl_poll_countdown := 0 */
    RA_FreeARMRegister(&ptr, adr);
    RA_FreeARMRegister(&ptr, tmp);
    return ptr;
}

static uint32_t *emit_oncall_jit(uint32_t *ptr, uint16_t id)
{
    RA_FlushOMMXRegs(&ptr);                               /* d16-d31 are caller-saved */
    const uint8_t r[4] = {
        RA_MapM68kRegister(&ptr, 0),                      /* D0 */
        RA_MapM68kRegister(&ptr, 1),                      /* D1 */
        RA_MapM68kRegister(&ptr, 8),                      /* A0 */
        RA_MapM68kRegister(&ptr, 9),                      /* A1 */
    };
    *ptr++ = push(0x0f | (1 << 12));
    *ptr++ = mov_reg(9, 13);                              /* r9 := sp */
    *ptr++ = bic_cc_immed(ARM_CC_AL, 13, 13, 7);          /* 8-byte align */
    *ptr++ = sub_immed(13, 13, 16);
    for (int k = 0; k < 4; k++)
        *ptr++ = str_offset(13, r[k], 4 * k);             /* regs[k] := the guest register */
    *ptr++ = mov_reg(0, 13);
    *ptr++ = movw_immed_u16(1, id);
    *ptr++ = ldr_offset(15, 12, 4);                       /* ip := the literal (pc + 8 + 4) */
    *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
    *ptr++ = b_cc(ARM_CC_AL, 2);                          /* over the literal */
    *ptr++ = BE32((uint32_t)(uintptr_t)a3d_oncall_jit);
    for (int k = 0; k < 2; k++)                           /* D0, D1 := regs[0], regs[1] */
    {
        if (r[k] <= 3)
        {
            *ptr++ = ldr_offset(13, 12, 4 * k);           /* ip := regs[k] (ip is restored by the pop) */
            *ptr++ = str_offset(9, 12, 4 * r[k]);         /* into rN's pushed slot */
        }
        else
            *ptr++ = ldr_offset(13, r[k], 4 * k);
    }
    *ptr++ = mov_reg(13, 9);                              /* sp := r9 */
    *ptr++ = pop(0x0f | (1 << 12));
    RA_SetDirtyM68kRegister(&ptr, 0);
    RA_SetDirtyM68kRegister(&ptr, 1);
    return ptr;
}

/* ONCALL's whole instruction; *m68k_ptr points at word 2 */
static uint32_t *emit_oncall(uint32_t *ptr, uint16_t **m68k_ptr)
{
    const uint16_t id = BE16((*m68k_ptr)[1]);
    if (g_oncall_trap)
    {
        ptr = emit_oncall_trap(ptr, id);
        ptr = EMIT_AdvancePC(ptr, 6);
        (*m68k_ptr) += 2;                                 /* word 2 and the id */
        *ptr++ = 0;
        *ptr++ = 0;                                       /* branch target (unused) */
        *ptr++ = INSN_TO_LE(0xfffffffe);                  /* block epilogue: flush regs/SR/PC */
        *ptr++ = INSN_TO_LE(0xffffffff);                  /* hard block end */
        return ptr;
    }
    ptr = emit_oncall_jit(ptr, id);
    ptr = EMIT_AdvancePC(ptr, 6);
    (*m68k_ptr) += 2;
    *ptr++ = INSN_TO_LE(0xfffffff0);                      /* the unit saves lr (blx clobbers it) */
    return ptr;
}
#endif /* EMU68_A3D */

/* STORE b,<vea> */
static uint32_t *emit_store(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t src, sscr;
    src = ommx_rd_reg(&ptr, i->rb, &sscr);
    ptr = ommx_store_vea(ptr, i, m68k_ptr, ext, src);
    FREE_IF(sscr, src);
    return ptr;
}

static uint32_t *emit_3op(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t a, ascr, b, bscr, d, dscr;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    if (i->op == OMMX_OP_OMLA_L) {
        d = ommx_rd_reg(&ptr, i->rd, &dscr);
        if (!dscr)
            RA_SetDirtyOMMXRegister(&ptr, i->rd - 8);
    } else
        d = ommx_dst_reg(&ptr, i->rd, &dscr);               /* after the sources: no fill skipped */
    switch (i->op) {
        case OMMX_OP_PAND:    *ptr++ = vand_d(d, b, a); break;
        case OMMX_OP_POR:     *ptr++ = vorr_d(d, b, a); break;
        case OMMX_OP_PEOR:    *ptr++ = veor_d(d, b, a); break;
        case OMMX_OP_PANDN:   *ptr++ = vbic_d(d, b, a); break;
        case OMMX_OP_PAVGB:   *ptr++ = vrhadd_u(0, d, b, a); break;  /* (a+b+1)>>1, u8          */
        case OMMX_OP_PADDB:   *ptr++ = vadd_i(0, d, b, a); break;
        case OMMX_OP_PADDW:   *ptr++ = vadd_i(1, d, b, a); break;
        case OMMX_OP_PSUBB:   *ptr++ = vsub_i(0, d, b, a); break;
        case OMMX_OP_PSUBW:   *ptr++ = vsub_i(1, d, b, a); break;
        case OMMX_OP_PADDUSB: *ptr++ = vqadd_u(0, d, b, a); break;
        case OMMX_OP_PADDUSW: *ptr++ = vqadd_u(1, d, b, a); break;
        case OMMX_OP_PSUBUSB: *ptr++ = vqsub_u(0, d, b, a); break;
        case OMMX_OP_PSUBUSW: *ptr++ = vqsub_u(1, d, b, a); break;
        case OMMX_OP_PMULL:   *ptr++ = vmul_i(1, d, b, a); break;    /* low 16 of the product   */
        case OMMX_OP_OADD_L:  *ptr++ = vadd_i(2, d, b, a); break;
        case OMMX_OP_OSUB_L:  *ptr++ = vsub_i(2, d, b, a); break;
        case OMMX_OP_OMUL_L:  *ptr++ = vmul_i(2, d, b, a); break;    /* low 32 - OPT_SSO exact  */
        case OMMX_OP_OMLA_L:  *ptr++ = vmla_i(2, d, b, a); break;    /* d loaded by the RMW path */
        case OMMX_OP_OPADD_L: *ptr++ = vpadd_i(2, d, b, a); break;
        default:
            if (i->op >= OMMX_OP_PCMP_BASE && i->op <= 0x2f) {
                uint8_t sz = i->op & 1, cc = (i->op >> 1) & 7;
                if (cc == 0)      *ptr++ = vceq_i(sz, d, b, a);
                else if (cc == 1) *ptr++ = vcgt_u(sz, d, b, a);      /* hi: unsigned b > a      */
                else if (cc == 6) *ptr++ = vcge_s(sz, d, b, a);      /* ge: signed              */
                else              *ptr++ = vcgt_s(sz, d, b, a);      /* gt: signed              */
            } else if (i->op >= OMMX_OP_PMIN_BASE && i->op <= 0x37) {
                uint8_t sz = i->op & 1, U = (i->op >> 1) & 1, is_max = (i->op >> 2) & 1;
                if (is_max) *ptr++ = U ? vmax_u(sz, d, b, a) : vmax_s(sz, d, b, a);
                else        *ptr++ = U ? vmin_u(sz, d, b, a) : vmin_s(sz, d, b, a);
            }
            break;
    }
    ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d); FREE_IF(bscr, b); FREE_IF(ascr, a);
    return ptr;
}

/* PMUL88 <vea>,b,d : d[i] = (a[i] * b[i]) >> 8, signed 16 - vmull.s16 into a Q, vshrn #8 */
static uint32_t *emit_pmul88(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t q = RA_AllocFPURegisterPair(&ptr);          /* pairs first: they need two free d's */
    uint8_t a, ascr, b, bscr, d, dscr;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    d = ommx_dst_reg(&ptr, i->rd, &dscr);
    *ptr++ = vmull_s(1, q, b, a);
    *ptr++ = vshrn_imm(1, d, q, (i->op == OMMX_OP_PMULH) ? 16 : 8);   /* bits 31:16 or 23:8   */
    ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d); FREE_IF(bscr, b); FREE_IF(ascr, a);
    RA_FreeFPURegister(&ptr, q); RA_FreeFPURegister(&ptr, q + 1);
    return ptr;
}

static uint32_t *emit_osrar(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t b, bscr, d, dscr;
    uint16_t n = BE16(m68k_ptr[1]);                     /* m68k_ptr -> word 2; [1] = ext word */
    if (n < 1)  n = 1;
    if (n > 32) n = 32;
    *ext = 2;
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    d = ommx_dst_reg(&ptr, i->rd, &dscr);
    if (i->op == OMMX_OP_OSRA_L)
        *ptr++ = vshr_s_imm(2, d, b, (uint8_t)n);
    else
        *ptr++ = vrshr_s_imm(2, d, b, (uint8_t)n);
    ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d); FREE_IF(bscr, b);
    return ptr;
}

static uint32_t *emit_bflyw(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t q = RA_AllocFPURegisterPair(&ptr);
    uint8_t a, ascr, b, bscr;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    {
        uint8_t sz = (i->op == OMMX_OP_BFLYB) ? 0 : 1;
        *ptr++ = vadd_i(sz, q, b, a);
        *ptr++ = vsub_i(sz, q + 1, b, a);
    }
    FREE_IF(bscr, b); FREE_IF(ascr, a);
    for (int k = 0; k < 2; k++) {
        uint8_t r = i->rd + k;
        if (r >= OMMX_REG_E8) {
            uint8_t dst = RA_MapOMMXRegisterForWrite(&ptr, r - 8);
            *ptr++ = vmov_d(dst, q + k);
        } else {
            ommx_wr_reg(&ptr, r, q + k);
        }
    }
    RA_FreeFPURegister(&ptr, q); RA_FreeFPURegister(&ptr, q + 1);
    return ptr;
}

static uint32_t *emit_vperm(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint32_t N = ((uint32_t)BE16(m68k_ptr[1]) << 16) | BE16(m68k_ptr[2]);
    *ext += 2;
    uint8_t tbl = RA_AllocFPURegisterPair(&ptr);
    uint8_t a, ascr, b, bscr, d, dscr, idx;
    a = ommx_rd_reg(&ptr, i->ra, &ascr);
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    *ptr++ = vmov_d(tbl, a);
    *ptr++ = vmov_d(tbl + 1, b);
    FREE_IF(bscr, b); FREE_IF(ascr, a);
    uint64_t v = 0;
    for (int lane = 0; lane < 8; lane++) {
        int ai = 7 - lane;
        uint32_t n = (N >> (28 - 4 * ai)) & 0xf;
        uint32_t t = (n < 8) ? (7 - n) : (23 - n);
        v |= (uint64_t)t << (8 * lane);
    }
    idx = RA_AllocFPURegister(&ptr);
    ommx_const64(&ptr, idx, v);
    d = ommx_dst_reg(&ptr, i->rd, &dscr);
    *ptr++ = vtbl8(d, tbl, 1, idx);
    ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d);
    RA_FreeFPURegister(&ptr, idx);
    RA_FreeFPURegister(&ptr, tbl); RA_FreeFPURegister(&ptr, tbl + 1);
    return ptr;
}

static uint32_t *emit_trans(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    (void)m68k_ptr; (void)ext;
    uint8_t base = (uint8_t)((i->A ? 16 : 0) + ((i->vea >> 2) & 3) * 4);
    uint8_t c0 = RA_AllocFPURegisterPair(&ptr);          /* c0:c1 */
    uint8_t c2 = RA_AllocFPURegisterPair(&ptr);          /* c2:c3 */
    uint8_t c[4] = { c0, (uint8_t)(c0 + 1), c2, (uint8_t)(c2 + 1) };
    for (int k = 0; k < 4; k++) {
        uint8_t s, sscr;
        s = ommx_rd_reg(&ptr, base + k, &sscr);
        *ptr++ = vmov_d(c[k], s);
        FREE_IF(sscr, s);
    }
    *ptr++ = vtrn_d(1, c[0], c[1]);
    *ptr++ = vtrn_d(1, c[2], c[3]);
    *ptr++ = vtrn_d(2, c[0], c[2]);
    *ptr++ = vtrn_d(2, c[1], c[3]);
    uint8_t src_d  = (i->op == OMMX_OP_TRANSLO) ? c[1] : c[3];
    uint8_t src_d2 = (i->op == OMMX_OP_TRANSLO) ? c[0] : c[2];
    for (int k = 0; k < 2; k++) {
        uint8_t r = i->rd + k, s = k ? src_d2 : src_d;
        if (r >= OMMX_REG_E8) {
            uint8_t dst = RA_MapOMMXRegisterForWrite(&ptr, r - 8);
            *ptr++ = vrev64_d(1, dst, s);
        } else {
            *ptr++ = vrev64_d(1, s, s);
            ommx_wr_reg(&ptr, r, s);
        }
    }
    RA_FreeFPURegister(&ptr, c0); RA_FreeFPURegister(&ptr, c0 + 1);
    RA_FreeFPURegister(&ptr, c2); RA_FreeFPURegister(&ptr, c2 + 1);
    return ptr;
}

static uint32_t *emit_packuswb(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t q = RA_AllocFPURegisterPair(&ptr);
    uint8_t b, bscr, d, dscr, r;
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    d = ommx_rd_reg(&ptr, i->rd, &dscr);
    *ptr++ = vmov_d(q, d);
    *ptr++ = vmov_d(q + 1, b);
    FREE_IF(dscr, d); FREE_IF(bscr, b);
    r = RA_AllocFPURegister(&ptr);
    *ptr++ = vqmovun_s(1, r, q);
    ptr = ommx_store_vea(ptr, i, m68k_ptr, ext, r);
    RA_FreeFPURegister(&ptr, r);
    RA_FreeFPURegister(&ptr, q); RA_FreeFPURegister(&ptr, q + 1);
    return ptr;
}

static uint32_t *emit_pack3216(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t q = RA_AllocFPURegisterPair(&ptr);
    uint8_t mR = RA_AllocFPURegister(&ptr), mG = RA_AllocFPURegister(&ptr), mB = RA_AllocFPURegister(&ptr);
    uint8_t t = RA_AllocFPURegister(&ptr);
    *ptr++ = vmov_i32(mB, 0x1f);
    *ptr++ = vmov_i32sh8(mR, 0xf8);
    *ptr++ = vmov_i32(mG, 0x3f);                         /* 0x000007E0 = 0x3F << 5           */
    *ptr++ = vshl_imm(2, mG, mG, 5);
    for (int k = 0; k < 2; k++) {
        uint8_t s, sscr, out = q + k;
        s = ommx_rd_reg(&ptr, k ? i->rb : i->rd, &sscr);
        *ptr++ = vshr_u_imm(2, out, s, 8);  *ptr++ = vand_d(out, out, mR);
        *ptr++ = vshr_u_imm(2, t, s, 5);    *ptr++ = vand_d(t, t, mG);   *ptr++ = vorr_d(out, out, t);
        *ptr++ = vshr_u_imm(2, t, s, 3);    *ptr++ = vand_d(t, t, mB);   *ptr++ = vorr_d(out, out, t);
        FREE_IF(sscr, s);
    }
    *ptr++ = vmovn_i(2, t, q);                           /* 4 x 16 from the pair             */
    ptr = ommx_store_vea(ptr, i, m68k_ptr, ext, t);
    RA_FreeFPURegister(&ptr, t); RA_FreeFPURegister(&ptr, mB); RA_FreeFPURegister(&ptr, mG); RA_FreeFPURegister(&ptr, mR);
    RA_FreeFPURegister(&ptr, q); RA_FreeFPURegister(&ptr, q + 1);
    return ptr;
}

static uint32_t *emit_bsel(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t a, ascr, mk, mscr, d, dscr;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    mk = ommx_rd_reg(&ptr, i->rb, &mscr);
    d  = ommx_rd_reg(&ptr, i->rd, &dscr);
    *ptr++ = vbit_d(d, a, mk);
    if (i->rd >= OMMX_REG_E8) RA_SetDirtyOMMXRegister(&ptr, i->rd - 8);
    else                      ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d); FREE_IF(mscr, mk); FREE_IF(ascr, a);
    return ptr;
}

static uint32_t *emit_lsq(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t a, ascr, b, bscr, d, dscr, t;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    t = RA_AllocFPURegister(&ptr);
    *ptr++ = vmov_i32(t, 0x3f);
    *ptr++ = vand_d(t, t, a);                            /* count mod 64 in the low byte     */
    if (i->op == OMMX_OP_LSRQ) *ptr++ = vneg_s(0, t, t);
    d = ommx_dst_reg(&ptr, i->rd, &dscr);
    *ptr++ = vshl_u_reg(3, d, b, t);
    ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d); RA_FreeFPURegister(&ptr, t); FREE_IF(bscr, b); FREE_IF(ascr, a);
    return ptr;
}

static uint32_t *emit_c2p(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    static const uint8_t  K[3] = { 7, 14, 28 };
    static const uint64_t M[3] = { 0x00AA00AA00AA00AAull, 0x0000CCCC0000CCCCull, 0x00000000F0F0F0F0ull };
    uint8_t a, ascr, d, dscr, t, m;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    d = ommx_dst_reg(&ptr, i->rd, &dscr);
    t = RA_AllocFPURegister(&ptr); m = RA_AllocFPURegister(&ptr);
    if (d != a) *ptr++ = vmov_d(d, a);
    for (int s = 0; s < 3; s++) {
        ommx_const64(&ptr, m, M[s]);
        *ptr++ = vshr_u_imm(3, t, d, K[s]);
        *ptr++ = veor_d(t, t, d);
        *ptr++ = vand_d(t, t, m);                        /* t &= M                           */
        *ptr++ = veor_d(d, d, t);                        /* x ^= t                           */
        *ptr++ = vshl_imm(3, t, t, K[s]);                /* t <<= k                          */
        *ptr++ = veor_d(d, d, t);                        /* x ^= t << k                      */
    }
    ommx_wr_reg(&ptr, i->rd, d);
    RA_FreeFPURegister(&ptr, m); RA_FreeFPURegister(&ptr, t);
    FREE_IF(dscr, d); FREE_IF(ascr, a);
    return ptr;
}

static uint32_t *emit_pmula(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t q = RA_AllocFPURegisterPair(&ptr);
    uint8_t a, ascr, b, bscr, d, dscr, al, t;
    ptr = ommx_fetch_vea(ptr, i, m68k_ptr, ext, &a, &ascr);
    b = ommx_rd_reg(&ptr, i->rb, &bscr);
    al = RA_AllocFPURegister(&ptr); t = RA_AllocFPURegister(&ptr);
    ommx_const64(&ptr, t, 0x0707070703030303ull);
    *ptr++ = vtbl8(al, a, 0, t);
    *ptr++ = vmull_u(0, q, al, b);
    *ptr++ = vshrn_imm(0, t, q, 8);
    *ptr++ = vqadd_u(0, t, t, a);
    *ptr++ = vmov_i8(q, 0xff);
    *ptr++ = vceq_i(0, q, al, q);
    d = ommx_dst_reg(&ptr, i->rd, &dscr);
    *ptr++ = vbsl_d(q, b, t);
    *ptr++ = vmvn_i32sh24(t, 0xff);
    *ptr++ = vand_d(d, q, t);                            /* clear the alpha bytes            */
    ommx_wr_reg(&ptr, i->rd, d);
    FREE_IF(dscr, d); RA_FreeFPURegister(&ptr, t); RA_FreeFPURegister(&ptr, al);
    FREE_IF(bscr, b); FREE_IF(ascr, a);
    RA_FreeFPURegister(&ptr, q); RA_FreeFPURegister(&ptr, q + 1);
    return ptr;
}

/* rotated-immediate form of (0xFF << (8*k)) for the ARM data-processing imm12 field */
static uint32_t ommx_imm_bytemask(int k) { static const uint16_t r[4] = { 0x0ff, 0xcff, 0x8ff, 0x4ff }; return r[k & 3]; }

static uint32_t *emit_maskstore(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    uint8_t src, sscr;
    src = ommx_rd_reg(&ptr, i->rb, &sscr);
    if (i->mode <= 1) {                                                   /* register dest: plain copy */
        ptr = ommx_store_vea(ptr, i, m68k_ptr, ext, src);
        FREE_IF(sscr, src);
        return ptr;
    }
    /* the selection: a NEON byte mask (all-ones = write) or, for STOREC, the count in a GPR */
    uint8_t sel = 0xff, cnt = 0xff;
    if (i->op == OMMX_OP_STOREC) {
        if (i->rd < OMMX_REG_E0) {
            cnt = RA_MapM68kRegister(&ptr, i->rd);                        /* the integer Dn IS the count */
        } else {
            uint8_t e, escr;
            e = ommx_rd_reg(&ptr, i->rd, &escr);
            cnt = RA_AllocARMRegister(&ptr);
            *ptr++ = vmov32_to_gpr(cnt, e, 0);                            /* low 32 bits                 */
            FREE_IF(escr, e);
        }
    } else {
        uint8_t t = RA_AllocFPURegister(&ptr);
        sel = RA_AllocFPURegister(&ptr);
        if (i->op == OMMX_OP_STOREM3) {
            switch (OMMX_W2_REGD(i->w2) & 3) {
                case 0:  *ptr++ = vshr_s_imm(2, sel, src, 31); break;
                case 1:  *ptr++ = vtst_i(0, sel, src, src); break;                    /* byte != 0               */
                case 2:  ommx_const64(&ptr, t, 0xF81FF81FF81FF81Full);
                         *ptr++ = vceq_i(1, sel, src, t);                             /* == $F81F ...            */
                         *ptr++ = vmvn_i32(t, 0);  *ptr++ = veor_d(sel, sel, t); break;   /* ... inverted        */
                default: *ptr++ = vshr_s_imm(1, sel, src, 15);                        /* msb set ...             */
                         *ptr++ = vmvn_i32(t, 0);  *ptr++ = veor_d(sel, sel, t); break;   /* ... inverted        */
            }
        } else {
            uint8_t mk, mscr;
            mk = ommx_rd_reg(&ptr, i->rd, &mscr);
            if (i->op == OMMX_OP_STOREM) {
                *ptr++ = vdup_lane(0, sel, mk, 0);                        /* the low byte everywhere     */
                ommx_const64(&ptr, t, 0x8040201008040201ull);
                *ptr++ = vtst_i(0, sel, sel, t);
            } else {                                                      /* STOREILM: bit 0 of each byte clear */
                *ptr++ = vmov_i8(t, 0x01);
                *ptr++ = vtst_i(0, sel, mk, t);                           /* set where bit 0 SET ...     */
                *ptr++ = vmvn_i32(t, 0);  *ptr++ = veor_d(sel, sel, t);   /* ... inverted                */
            }
            FREE_IF(mscr, mk);
        }
        RA_FreeFPURegister(&ptr, t);
    }
    /* the address, with the (An)+ / -(An) bookkeeping */
    uint8_t base = 0xff; int32_t off = 0;
    if (i->mode == 3 || i->mode == 4) {
        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base, i->vea, m68k_ptr, ext, 0, NULL);
        if (i->mode == 4) *ptr++ = sub_immed(base, base, 8);
    } else {
        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base, i->vea, m68k_ptr, ext, 1, &off);
        if (off < -4088 || off > 4088) {                                  /* keep the byte offsets in strb's imm12 */
            uint8_t a = RA_AllocARMRegister(&ptr);
            *ptr++ = movw_immed_u16(a, off & 0xffff);
            if ((off >> 16) & 0xffff) *ptr++ = movt_immed_u16(a, (off >> 16) & 0xffff);
            *ptr++ = add_reg(a, base, a, LSL, 0);
            RA_FreeARMRegister(&ptr, base); base = a; off = 0;
        }
    }
    /* the byte loop: data and mask halves in GPRs, one conditional strb per byte */
    uint8_t dhi = RA_AllocARMRegister(&ptr), dlo = RA_AllocARMRegister(&ptr), tmp = RA_AllocARMRegister(&ptr);
    uint8_t mhi = 0xff, mlo = 0xff;
    *ptr++ = fmrrd(dhi, dlo, src);
    if (sel != 0xff) { mhi = RA_AllocARMRegister(&ptr); mlo = RA_AllocARMRegister(&ptr); *ptr++ = fmrrd(mhi, mlo, sel); }
    for (int k = 0; k < 8; k++) {
        uint8_t dw = (k < 4) ? dhi : dlo, mw = (k < 4) ? mhi : mlo;
        int sh = 24 - 8 * (k & 3);
        uint8_t cc;
        if (sel != 0xff) { *ptr++ = tst_cc_immed(ARM_CC_AL, mw, ommx_imm_bytemask(3 - (k & 3))); cc = ARM_CC_NE; }
        else             { *ptr++ = cmp_cc_immed(ARM_CC_AL, cnt, k); cc = ARM_CC_GT; }         /* count > k (signed) */
        if (sh) { *ptr++ = lsr_immed(tmp, dw, sh); *ptr++ = strb_cc_offset(cc, base, tmp, off + k); }
        else    { *ptr++ = strb_cc_offset(cc, base, dw, off + k); }
    }
    if (i->mode == 3) *ptr++ = add_immed(base, base, 8);
    if (i->mode == 3 || i->mode == 4) RA_SetDirtyM68kRegister(&ptr, 8 + i->reg);
    if (mhi != 0xff) { RA_FreeARMRegister(&ptr, mhi); RA_FreeARMRegister(&ptr, mlo); }
    RA_FreeARMRegister(&ptr, tmp); RA_FreeARMRegister(&ptr, dlo); RA_FreeARMRegister(&ptr, dhi);
    RA_FreeARMRegister(&ptr, base);
    if (sel != 0xff) RA_FreeFPURegister(&ptr, sel);
    if (cnt != 0xff && i->rd >= OMMX_REG_E0) RA_FreeARMRegister(&ptr, cnt);
    FREE_IF(sscr, src);
    return ptr;
}

static uint32_t *emit_minterm(uint32_t *ptr, const struct ommx_insn *i, uint16_t *m68k_ptr, uint8_t *ext)
{
    (void)m68k_ptr; (void)ext;
    uint8_t base = (uint8_t)((i->A ? 16 : 0) + ((i->vea >> 2) & 3) * 4);
    uint8_t cd, cscr, K, t, acc, mk;
    cd = ommx_rd_reg(&ptr, base + 3, &cscr);
    K = RA_AllocFPURegister(&ptr); t = RA_AllocFPURegister(&ptr);
    *ptr++ = vdup_lane(0, K, cd, 0);
    FREE_IF(cscr, cd);
    ommx_const64(&ptr, t, 0x8040201008040201ull);
    *ptr++ = vtst_i(0, K, K, t);
    RA_FreeFPURegister(&ptr, t);
    uint8_t a, ascr, b, bscr, c, cscr2;
    a = ommx_rd_reg(&ptr, base + 0, &ascr);
    b = ommx_rd_reg(&ptr, base + 1, &bscr);
    c = ommx_rd_reg(&ptr, base + 2, &cscr2);
    acc = RA_AllocFPURegister(&ptr); mk = RA_AllocFPURegister(&ptr);
    *ptr++ = veor_d(acc, acc, acc);
    for (int k = 0; k < 8; k++) {
        *ptr++ = vdup_lane(0, mk, K, k);
        *ptr++ = (k & 4) ? vand_d(mk, mk, a) : vbic_d(mk, mk, a);
        *ptr++ = (k & 2) ? vand_d(mk, mk, b) : vbic_d(mk, mk, b);
        *ptr++ = (k & 1) ? vand_d(mk, mk, c) : vbic_d(mk, mk, c);
        *ptr++ = vorr_d(acc, acc, mk);
    }
    FREE_IF(cscr2, c); FREE_IF(bscr, b); FREE_IF(ascr, a);
    RA_FreeFPURegister(&ptr, mk); RA_FreeFPURegister(&ptr, K);
    if (i->rd >= OMMX_REG_E8) {
        uint8_t dst = RA_MapOMMXRegisterForWrite(&ptr, i->rd - 8);
        *ptr++ = vmov_d(dst, acc);
    } else {
        ommx_wr_reg(&ptr, i->rd, acc);
    }
    RA_FreeFPURegister(&ptr, acc);
    return ptr;
}

_Static_assert(__builtin_offsetof(struct M68KState, OMMX_TOUCHED) < 4096,
               "OMMX_TOUCHED must stay within str's 12-bit immediate");
static uint32_t *ommx_emit_touch(uint32_t *ptr)
{
    if (!ommx_touch_emitted) {
        *ptr++ = str_offset(REG_CTX, REG_CTX, __builtin_offsetof(struct M68KState, OMMX_TOUCHED));
        ommx_touch_emitted = 1;
    }
    if (!g_ommx_banks_armed) ommx_banks_arm();
    return ptr;
}

uint32_t *EMIT_OMMX(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    uint16_t w1 = BE16((*m68k_ptr)[0]);
    uint16_t w2 = BE16((*m68k_ptr)[1]);
    struct ommx_insn i;
    uint8_t ext = 1;                                      /* word 2 consumed                  */

    *insn_consumed = 1;
    if (!OMMX_IsImplemented(w1, w2))
        return EMIT_OMMX_LineF(ptr, m68k_ptr, w1);

    ommx_decode(w1, w2, &i);
    (*m68k_ptr)++;                                        /* past word 1; m68k_ptr -> word 2  */

#ifdef EMU68_A3D
    if (w1 == OMMX_ONCALL_W1 && w2 == OMMX_ONCALL_W2)
        return emit_oncall(ptr, m68k_ptr);
#endif

    if (i.is_vperm || i.op != OMMX_OP_OIDENT)
        ptr = ommx_emit_touch(ptr);

    if (i.is_vperm)                       ptr = emit_vperm(ptr, &i, *m68k_ptr, &ext);
    else switch (i.op) {
        case OMMX_OP_LOAD:                ptr = emit_load(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_STORE:               ptr = emit_store(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_TRANSHI:
        case OMMX_OP_TRANSLO:             ptr = emit_trans(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_PACKUSWB:            ptr = emit_packuswb(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_PACK3216:            ptr = emit_pack3216(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_PMUL88:
        case OMMX_OP_PMULH:               ptr = emit_pmul88(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_PMULA:               ptr = emit_pmula(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_BFLYB:
        case OMMX_OP_BFLYW:               ptr = emit_bflyw(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_BSEL:                ptr = emit_bsel(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_C2P:                 ptr = emit_c2p(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_OSRAR_L:
        case OMMX_OP_OSRA_L:              ptr = emit_osrar(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_LSLQ:
        case OMMX_OP_LSRQ:                ptr = emit_lsq(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_OIDENT:              ptr = emit_oident(ptr, &i); break;
        case OMMX_OP_STOREM:
        case OMMX_OP_STOREC:
        case OMMX_OP_STOREILM:
        case OMMX_OP_STOREM3:             ptr = emit_maskstore(ptr, &i, *m68k_ptr, &ext); break;
        case OMMX_OP_MINTERM:             ptr = emit_minterm(ptr, &i, *m68k_ptr, &ext); break;
        default:                          ptr = emit_3op(ptr, &i, *m68k_ptr, &ext); break;
    }

    ptr = EMIT_AdvancePC(ptr, 2 * (ext + 1));
    (*m68k_ptr) += ext;
    return ptr;
}

#endif /* EMU68_OMMX */
