/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* A32 (VFPv3 / NEON) implementations of A64 floating-point encoders. */

#ifndef _ARMHF_FP_GAPS_H
#define _ARMHF_FP_GAPS_H

#ifndef _ARMHF_TS_ENUM
#define _ARMHF_TS_ENUM
enum TS { TS_B = 1, TS_H = 2, TS_S = 4, TS_D = 8 };
#endif

static inline uint32_t mov_reg_to_simd_cc(uint8_t cc, uint8_t v_dst, enum TS ts, uint8_t index, uint8_t rn)
{
    if (ts == TS_D)
        return fmdrr_cc(cc, v_dst & 31, (rn + 1) & 15, rn & 15);

    uint32_t Dd = v_dst & 31;
    uint32_t w  = 0x0e000b10 | (cc << 28)
                | ((Dd & 0xf) << 16)   /* Vd[19:16] */
                | ((rn & 15) << 12)    /* Rt[15:12] */
                | ((Dd >> 4) << 7);    /* D[7]      */
    switch (ts) {
        case TS_S: /* idx in bit21 (valid idx 0..1) */
            w |= ((uint32_t)(index & 1) << 21);
            break;
        case TS_H: /* idx[1]->bit21, idx[0]->bit6, opc=1<<5 (valid idx 0..3) */
            w |= (1u << 5)
               | ((uint32_t)(index & 2) << 20)   /* 2<<20 = 1<<21 */
               | ((uint32_t)(index & 1) << 6);
            break;
        case TS_B: /* idx[2]->bit21, idx[1]->bit6, idx[0]->bit5, opc.hi=1<<22 */
            w |= (1u << 22)
               | ((uint32_t)(index & 4) << 19)   /* 4<<19 = 1<<21 */
               | ((uint32_t)(index & 2) << 5)    /* 2<<5  = 1<<6  */
               | ((uint32_t)(index & 1) << 5);
            break;
        default:
            return INSN_TO_LE(0xe7f000f0); /* UDF: unmappable size */
    }
    return INSN_TO_LE(w);
}
static inline uint32_t mov_reg_to_simd(uint8_t v_dst, enum TS ts, uint8_t index, uint8_t rn)
{
    return mov_reg_to_simd_cc(ARM_CC_AL, v_dst, ts, index, rn);
}

static inline uint32_t mov_simd_to_reg(uint8_t rd, uint8_t v_src, enum TS ts, uint8_t index)
{
    if (ts == TS_D) {
        return INSN_TO_LE((ARM_CC_AL << 28) | 0x0c500b10
            | ((uint32_t)(v_src & 15))                       // Vm
            | (((uint32_t)(v_src >> 4) & 1) << 5)
            | (((uint32_t)rd & 15) << 12)
            | (((uint32_t)((rd + 1) & 15)) << 16));
    }

    uint32_t insn = (ARM_CC_AL << 28) | 0x0e100b10
                  | (((uint32_t)(v_src >> 4) & 1) << 7)
                  | (((uint32_t)v_src & 15) << 16)
                  | (((uint32_t)rd & 15) << 12);              // Rt

    if (ts == TS_S) {
        insn |= ((uint32_t)(index & 1) << 21);
    } else if (ts == TS_H) {
        insn |= (1u << 23)
              | (((uint32_t)(index >> 1) & 1) << 21)
              | (1u << 5)
              | (((uint32_t)index & 1) << 6);
    } else { // TS_B
        insn |= (1u << 23)
              | (1u << 22)
              | (((uint32_t)(index >> 2) & 1) << 21)
              | (((uint32_t)(index >> 1) & 1) << 6)
              | (((uint32_t)index & 1) << 5);
    }
    return INSN_TO_LE(insn);
}

static inline uint8_t A32_VREMAP_D(uint8_t v)
{
    switch (v) {
        case 30: return 1;          // insn-count accumulator V30 -> d1
        default: return v & 0xf;    // V0 -> d0; other low V-regs pass through
    }
}

static inline uint32_t vadd_2d(uint8_t v_dst, uint8_t v_rn, uint8_t v_rm)
{
    uint8_t d = A32_VREMAP_D(v_dst);
    uint8_t n = A32_VREMAP_D(v_rn);
    uint8_t m = A32_VREMAP_D(v_rm);
    // A32 NEON VADD (I64) Dd,Dn,Dm: 1111 0010 0 D 11 Vn Vd 1000 N 0 M 0 Vm
    return INSN_TO_LE(0xF2300800u
        | (((uint32_t)(d >> 4) & 1) << 22)
        | (((uint32_t)(n & 0xf)) << 16)
        | (((uint32_t)(d & 0xf)) << 12)
        | (((uint32_t)(n >> 4) & 1) << 7)
        | (((uint32_t)(m >> 4) & 1) << 5)
        | ((uint32_t)(m & 0xf)));
}

static inline uint32_t get_fpcr(uint8_t rt) { return INSN_TO_LE(0x0ef10a10 | (ARM_CC_AL << 28) | (rt << 12)); }
static inline uint32_t set_fpcr(uint8_t rt) { return INSN_TO_LE(0x0ee10a10 | (ARM_CC_AL << 28) | ((uint32_t)rt << 12)); }

static inline uint32_t fldd_preindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0d300b02 | (ARM_CC_AL << 28) | ((base & 15) << 16)
                      | ((v_dst & 15) << 12) | (((v_dst >> 4) & 1) << 22));
}
static inline uint32_t fldd_postindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0cb00b02 | (ARM_CC_AL << 28) | ((base & 15) << 16)
                      | ((v_dst & 15) << 12) | ((v_dst & 16) ? (1u << 22) : 0u));
}
// flds_preindex  - A64 LDR St,[Xn,#-4]! -> A32 VLDMDB base!,{Sd} (base-=4, load).
static inline uint32_t flds_preindex_cc(uint8_t cc, uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0d300a01 | (cc << 28) | (base << 16) | ((v_dst >> 1) << 12) | ((v_dst & 1) << 22));
}
static inline uint32_t flds_preindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    return flds_preindex_cc(ARM_CC_AL, v_dst, base, offset9);
}
// flds_postindex - A64 LDR St,[Xn],#4 -> A32 VLDMIA base!,{Sd} (load, base+=4).
static inline uint32_t flds_postindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0xecb00a01 | ((base & 15) << 16) | ((v_dst >> 1) << 12) | ((v_dst & 1) << 22));
}
// fstd_preindex  - A64 STR Dt,[Xn,#-8]! -> A32 VSTMDB base!,{Dd} (base-=8, store).
static inline uint32_t fstd_preindex_cc(uint8_t cc, uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0d200b02 | (cc << 28) | ((base & 0xf) << 16) | ((v_dst & 0xf) << 12));
}
static inline uint32_t fstd_preindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    return fstd_preindex_cc(ARM_CC_AL, v_dst, base, offset9);
}
// fstd_postindex - A64 STR Dt,[Xn],#8 -> A32 VSTMIA base!,{Dd} (store, base+=8).
static inline uint32_t fstd_postindex_cc(uint8_t cc, uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0ca00b02 | (cc << 28) | ((base & 15) << 16) | ((v_dst & 15) << 12) | (((v_dst >> 4) & 1) << 22));
}
static inline uint32_t fstd_postindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    return fstd_postindex_cc(ARM_CC_AL, v_dst, base, offset9);
}
// fsts_preindex  - A64 STR St,[Xn,#-4]! -> A32 VSTMDB base!,{Sd} (base-=4, store).
static inline uint32_t fsts_cc_preindex(uint8_t cc, uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0d200a01 | (cc << 28) | ((base & 15) << 16) | ((v_dst >> 1) << 12) | ((v_dst & 1) << 22));
}
static inline uint32_t fsts_preindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    return fsts_cc_preindex(ARM_CC_AL, v_dst, base, offset9);
}
// fsts_postindex - A64 STR St,[Xn],#4 -> A32 VSTMIA base!,{Sd} (store, base+=4).
static inline uint32_t fsts_postindex_cc(uint8_t cc, uint8_t v_dst, uint8_t base, int16_t offset9) {
    (void)offset9;
    return INSN_TO_LE(0x0ca00a01 | (cc << 28) | (base << 16) | ((v_dst >> 1) << 12) | ((v_dst & 1) << 22));
}
static inline uint32_t fsts_postindex(uint8_t v_dst, uint8_t base, int16_t offset9) {
    return fsts_postindex_cc(ARM_CC_AL, v_dst, base, offset9);
}

static inline void EMIT_scvtf_32toD(uint32_t **s, uint8_t v_dst, uint8_t src)
{
    *(*s)++ = fmsr(0, src);        // VMOV s0, Rsrc
    *(*s)++ = fsitod(v_dst, 0);    // VCVT.F64.S32 Dd, s0
}

static inline void EMIT_fcvtzs_Dto32(uint32_t **s, uint8_t dst, uint8_t v_src)
{
    uint8_t s_tmp = (uint8_t)(v_src << 1);      // low single of D[v_src]
    *(*s)++ = ftosidrz(s_tmp, v_src);           // vcvt.s32.f64 Stmp, Dv_src (round-to-zero)
    *(*s)++ = fmrs(dst, s_tmp);                 // vmov         dst,  Stmp
}

static inline void EMIT_fcvtzs_Dto64(uint32_t **s, uint8_t dst, uint8_t v_src)
{
    *(*s)++ = ftosidrz(1, v_src);          // VCVT.S32.F64 s1, d<v_src> (round to zero)
    *(*s)++ = fmrs(dst, 1);                // VMOV dst, s1             (low 32 bits)
    *(*s)++ = asr_immed(dst + 1, dst, 31); // ASR dst+1, dst, #31      (sign-extend high)
}

#define FRINT64_SCRATCH_S 0
static inline void EMIT_frint64x(uint32_t **s, uint8_t v_dst, uint8_t v_src) {
    *(*s)++ = ftosid(FRINT64_SCRATCH_S, v_src);   /* round per FPSCR -> s0 */
    *(*s)++ = fsitod(v_dst, FRINT64_SCRATCH_S);   /* back to double */
}
static inline void EMIT_frint64z(uint32_t **s, uint8_t v_dst, uint8_t v_src) {
    *(*s)++ = ftosidrz(FRINT64_SCRATCH_S, v_src); /* truncate -> s0 */
    *(*s)++ = fsitod(v_dst, FRINT64_SCRATCH_S);   /* back to double */
}

static inline void EMIT_fmov_from_reg(uint32_t **s, uint8_t v_dst, uint8_t src)
{
    *(*s)++ = mov_immed_u8(REG_EMIT_TMP, 0);     /* high 32 bits = 0          */
    *(*s)++ = fmdrr(v_dst, REG_EMIT_TMP, src);
}

#endif // _ARMHF_FP_GAPS_H
