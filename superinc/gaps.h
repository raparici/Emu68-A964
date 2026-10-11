/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* A32 implementations of the remaining A64 encoders. */

#ifndef _ARMHF_GAPS_H
#define _ARMHF_GAPS_H

#include <assert.h>

static inline uint32_t b(uint32_t offset) { return b_cc(ARM_CC_AL, (int32_t)offset); }

static inline uint32_t svc(uint16_t code){ return INSN_TO_LE((ARM_CC_AL<<28)|0x0f000000|((uint32_t)code & 0xffffff)); }

static inline uint32_t nop(void){return INSN_TO_LE((ARM_CC_AL<<28)|0x0320f000);}

static inline uint32_t mov64_immed_u16(uint8_t reg, uint16_t val, uint8_t shift16) {
    if ((shift16 & 3) == 0)
        return movw_immed_u16(reg, val);   // MOVW reg,#val : reg = val, top half cleared
    return _UNDEF;
}

static inline uint32_t movk64_immed_u16(uint8_t reg, uint16_t val, uint8_t shift16)
{
    if (shift16 == 1)
        return movt_immed_u16(reg, val);   // MOVT reg,#val (ARM.h) : bits[31:16], keep low half
    return INSN_TO_LE(0xe7f000f0);
}

static inline uint32_t cmn_immed_lsl12(uint8_t rn, uint16_t imm12)
{
    uint32_t v = (uint32_t)(imm12 & 0xfff) << 12;
    uint16_t f;
    if (arm_encode_imm12(v, &f))
        return INSN_TO_LE(0x03700000 | (ARM_CC_AL << 28)
                          | ((uint32_t)(rn & 15) << 16) | f);   // CMN Rn,#(imm12<<12) (flags only)
    return INSN_TO_LE(0xe7f000f0);   // permanently-undefined: non-imm12-encodable shifted constant
}

static inline uint32_t movn64_immed_u16(uint8_t reg, uint16_t val, uint8_t shift16)
{
    (void)reg; (void)val;
    if (shift16 == 0) {                          // 32-bit MVN reg,#val (low half), if it ever occurs
        uint16_t f;
        if (arm_encode_imm12((uint32_t)val, &f))
            return INSN_TO_LE(0x03e00000 | (ARM_CC_AL << 28) | ((uint32_t)(reg & 15) << 12) | f);
    }
    return _UNDEF;   // shift16!=0 (the NaN-fill bits[63:48]) -> genuine-64, FPU-reset handler-ported
}

static inline void EMIT_add64_immed(uint32_t **s, uint8_t rd, uint8_t rn, uint16_t imm12)
{
    imm12 &= 0xfff;
    uint8_t lo = imm12 & 0xff;          // low 8 bits: imm8, rot=0
    uint8_t hi = (imm12 >> 8) & 0xf;    // bits[11:8]: imm8=hi, rot=0xC (ROR 24 -> <<8)
    *(*s)++ = add_immed(rd, rn, lo);                         // ADD rd, rn, #lo
    if (hi)
        *(*s)++ = add_immed(rd, rd, (uint16_t)((0xC << 8) | hi)); // ADD rd, rd, #(hi<<8)
}

static inline uint32_t add64_immed_lsl12(uint8_t rd, uint8_t rn, uint16_t imm12)
{
    uint32_t v = (uint32_t)(imm12 & 0xfff) << 12;
    uint16_t f;
    if (arm_encode_imm12(v, &f))
        return INSN_TO_LE(0x02800000 | (ARM_CC_AL << 28)
                          | ((uint32_t)(rn & 15) << 16)
                          | ((uint32_t)(rd & 15) << 12) | f);   // ADD Rd,Rn,#v
    return INSN_TO_LE(0xe7f000f0);   // permanently-undefined: traps non-expressible imm
}

static inline uint32_t sub64_reg(uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00400000,rd,rn,rm,sh,a);}

static inline uint32_t and64_reg(uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00000000,rd,rn,rm,sh,a);}

static inline uint32_t _a32_mvn_reg_sh(uint8_t rd, uint8_t rm, shift_t shift, uint8_t amount)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x01e00000
                      | ((uint32_t)(rd & 15) << 12)
                      | ((uint32_t)(amount & 31) << 7)
                      | ((uint32_t)(shift & 3) << 5)
                      | (rm & 15));
}
static inline void EMIT_orn64_reg(uint32_t **s, uint8_t Rd, uint8_t Rn, uint8_t Rm,
                                  shift_t shift, uint8_t amount, uint8_t tmp)
{
    *(*s)++ = _a32_mvn_reg_sh(tmp, Rm, shift, amount);
    *(*s)++ = orr64_reg(Rd, Rn, tmp, LSL, 0);
}

// ---- mvn64_reg: A64 is ORN rd,WZR,rm,<sh> #amt == MVN rd,rm{,<sh> #amt}. Reg is 32-bit on A32.
static inline uint32_t mvn64_reg(uint8_t rd,uint8_t rm,shift_t sh,uint8_t a){return mvn_reg(rd,rm,sh,a);}

static inline uint32_t neg_reg(uint8_t rd, uint8_t rm, shift_t shift, uint8_t amount) {
    (void)shift;
    return amount ? _UNDEF
                  : INSN_TO_LE((ARM_CC_AL<<28)|0x02600000|(((rm)&15)<<16)|(((rd)&15)<<12));  // RSB rd, rm, #0
}

static inline uint32_t negs_reg(uint8_t rd, uint8_t rm, shift_t shift, uint8_t amount) {
    (void)shift;
    return amount ? _UNDEF
                  : INSN_TO_LE((ARM_CC_AL<<28)|0x02700000|(((rm)&15)<<16)|(((rd)&15)<<12));  // RSBS rd, rm, #0
}

static inline uint32_t cmp64_reg(uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return cmp_reg(rn,rm,sh,a);}

static inline uint32_t cmn64_reg(uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount) { return cmn_reg(rn, rm, shift, amount); }

static inline uint32_t EMIT__msr_apsr_nzcvq_immed(uint8_t cc, uint8_t alt_cc)
{
    uint16_t f;
    int ok = arm_encode_imm12((uint32_t)(alt_cc & 15) << 28, &f);
    assert(ok);
    return INSN_TO_LE(0x0328f000 | ((uint32_t)cc << 28) | f);
}

static inline void EMIT_ccmp_reg(uint32_t **s, uint8_t rn, uint8_t rm,
                                 uint8_t alt_cc, uint8_t cond)
{
    uint8_t inv = EMIT_invert_cc(cond);              // from gen/condsel.h (cc ^ 1)
    if (rm == 31)
        *(*s)++ = cmp_cc_immed(cond, rn & 15, 0);                  // CMP<cond>  Rn,#0  (Rn - 0)
    else if (rn == 31)
        *(*s)++ = rsbs_cc_immed(cond, REG_EMIT_TMP, rm & 15, 0);   // RSBS<cond> r9,Rm,#0 (0 - Rm)
    else
        *(*s)++ = cmp_cc_reg(cond, rn & 15, rm & 15);              // CMP<cond>  Rn,Rm  (Rn - Rm)
    *(*s)++ = EMIT__msr_apsr_nzcvq_immed(inv, alt_cc);            // MSR<inv> APSR_nzcvq,#(alt_cc<<28)
}

static inline void EMIT_and64_immed(uint32_t **s, uint8_t Rd, uint8_t Rn,
                                    uint8_t width, uint8_t ror, uint8_t n, uint8_t tmp)
{
    if (!imm64_load_lo32(s, tmp, width, ror, n)) return;   // trap already emitted
    *(*s)++ = and_reg(Rd, Rn, tmp, LSL, 0);                     // AND Rd, Rn, tmp
}

static inline __attribute__((always_inline))
uint32_t number_to_mask(uint32_t number)
{
    unsigned shift = 0;
    unsigned width = 0;

    if (number == 0)
        return 0;

    if (number == 0xffffffff)
        return 0;

    shift = __builtin_ctz(number);

    number = number >> shift;

    width = 32 - __builtin_clz(number);

    if (number == ((1ULL << width) - 1))
        return (width << 16) | shift;
    else {
        number = ~number;

        if (number == 0)
            return 0;

        shift = __builtin_ctz(number);

        number = number >> shift;

        width = 32 - __builtin_clz(number);

        if (number == ((1ULL << width) - 1))
            return ((32 - width) << 16) | (shift + width);
        else
            return 0xffffffff;
    }
}

static inline uint32_t rorv64(uint8_t rd, uint8_t rn, uint8_t rm) {
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x01a00070 | ((rd & 15) << 12) | ((rm & 15) << 8) | (rn & 15));
}

// ---- reverse bits : (rd, rn) - A32 RBIT, mirrors A64 rbit (32-bit reg) ----
static inline uint32_t rbit(uint8_t rd, uint8_t rn){return INSN_TO_LE((ARM_CC_AL<<28)|0x06ff0f30|((rd&15)<<12)|(rn&15));}

static inline uint32_t sxtw64(uint8_t rd, uint8_t rn) {
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x01a00000 | (rn & 15) | ((rd & 15) << 12));
}

static inline uint32_t ubfx64(uint8_t rd, uint8_t rn, uint8_t lsb, uint8_t width){ return ubfx(rd, rn, lsb, width); }

static inline uint32_t udiv64(uint8_t rd, uint8_t rn, uint8_t rm) {
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x0730f010 | ((rd & 15) << 16) | ((rm & 15) << 8) | (rn & 15));
}

static inline uint32_t msub64(uint8_t rd, uint8_t ra, uint8_t rn, uint8_t rm)
{
    return mls(rd, ra, rm, rn);   // A32 MLS Rd,Rn,Rm,Ra : rd = ra - rn*rm
}

static inline uint32_t ldursb_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    int32_t off = shim_sext9(offset9);
    uint32_t base = (off >= 0) ? 0x01d000d0u : 0x015000d0u;
    uint32_t m = (uint32_t)(off >= 0 ? off : -off) & 0xff;
    return INSN_TO_LE((ARM_CC_AL << 28) | base
                      | ((uint32_t)(rn & 15) << 16) | ((uint32_t)(rt & 15) << 12)
                      | (m & 0x0f) | ((m << 4) & 0xf00));
}

static inline uint32_t ldursh_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    int32_t off = shim_sext9(offset9);
    uint32_t base = (off >= 0) ? 0x01d000f0u : 0x015000f0u;
    uint32_t m = (uint32_t)(off >= 0 ? off : -off) & 0xff;
    return INSN_TO_LE((ARM_CC_AL << 28) | base
                      | ((uint32_t)(rn & 15) << 16) | ((uint32_t)(rt & 15) << 12)
                      | (m & 0x0f) | ((m << 4) & 0xf00));
}

static inline uint32_t ldrsb_cc_offset_postindex(uint8_t cc, uint8_t dest, uint8_t src, int16_t offset){
    return (offset >= 0)
        ? INSN_TO_LE(0x00d000d0 | (cc << 28) | (dest << 16) | (src << 12) | (offset & 0x0f) | ((offset << 4) & 0xf00))
        : INSN_TO_LE(0x005000d0 | (cc << 28) | (dest << 16) | (src << 12) | ((-offset) & 0x0f) | (((-offset) << 4) & 0xf00));
}
// A64 signature: (rn = base/address reg [bits 19:16], rt = data/dest reg [bits 15:12], offset9).
static inline uint32_t ldrsb_offset_postindex(uint8_t rn, uint8_t rt, int16_t offset9){
    return ldrsb_cc_offset_postindex(ARM_CC_AL, rn, rt, offset9);
}

static inline uint32_t ldrsb_offset_preindex(uint8_t rn, uint8_t rt, int16_t offset9)
{
    int32_t off = shim_sext9(offset9);
    uint32_t base = (off >= 0) ? 0x01f000d0u : 0x017000d0u;
    uint32_t m = (uint32_t)(off >= 0 ? off : -off) & 0xff;
    return INSN_TO_LE(base | (ARM_CC_AL << 28)
                      | ((uint32_t)(rn & 15) << 16) | ((uint32_t)(rt & 15) << 12)
                      | (m & 0x0f) | ((m << 4) & 0xf00));
}

static inline uint32_t ldrsh_offset_postindex(uint8_t rn, uint8_t rt, int16_t offset9)
{
    int32_t off = (offset9 & 0x100) ? ((offset9 & 0x1ff) - 0x200) : (offset9 & 0x1ff); // sext9
    uint32_t base = (off >= 0) ? 0x00d000f0u : 0x005000f0u;
    uint32_t m = (uint32_t)(off >= 0 ? off : -off) & 0xff;
    return INSN_TO_LE(base | (ARM_CC_AL << 28)
                      | ((uint32_t)(rn & 15) << 16) | ((uint32_t)(rt & 15) << 12)
                      | (m & 0x0f) | ((m << 4) & 0xf00));
}

static inline uint32_t msr_imm(uint8_t op1, uint8_t op2, uint8_t imm) {
    (void)imm;
    if ((op1 & 7) == 3 && (op2 & 7) == 7) return INSN_TO_LE(0xf1080080); // CPSIE i : enable IRQ
    if ((op1 & 7) == 3 && (op2 & 7) == 6) return INSN_TO_LE(0xf10c0080); // CPSID i : disable IRQ
    return INSN_TO_LE(0xe7f000f0); // UDF : unsupported PSTATE selector (never hit by current handlers)
}

static inline uint32_t stlxrh(uint8_t rn, uint8_t rt, uint8_t rs)
{
    return strexh(rn & 15, rt & 15, rs & 15);   // STREXH Rs, Rt, [Rn]
}

static inline void EMIT_stp(uint32_t **s, uint8_t rn, uint8_t rt1,
                            uint8_t rt2, int16_t imm)
{
    *(*s)++ = str_offset(rn & 15, rt1 & 15, imm);                // STR rt1,[rn,#imm]
    *(*s)++ = str_offset(rn & 15, rt2 & 15, (int16_t)(imm + 4)); // STR rt2,[rn,#imm+4]
}

static inline void EMIT_ldp(uint32_t **s, uint8_t rn, uint8_t rt1,
                            uint8_t rt2, int16_t imm)
{
    *(*s)++ = ldr_offset(rn & 15, rt1 & 15, imm);                // LDR rt1,[rn,#imm]
    *(*s)++ = ldr_offset(rn & 15, rt2 & 15, (int16_t)(imm + 4)); // LDR rt2,[rn,#imm+4]
}

static inline uint32_t ldr_pcrel(uint8_t rt, int32_t n) {
    int32_t off = (int32_t)(n * 4) - 8;
    if (off >= 0)
        return INSN_TO_LE(0xe59f0000 | ((rt & 15u) << 12) | (off & 0xfff)); /* LDR Rt,[PC,+off] */
    return INSN_TO_LE(0xe51f0000 | ((rt & 15u) << 12) | ((uint32_t)(-off) & 0xfff)); /* LDR Rt,[PC,-off] */
}
static inline uint32_t ldr64_pcrel(uint8_t rt, int32_t n) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return ldr_pcrel(rt, n + 1);
#else
    return ldr_pcrel(rt, n);
#endif
}

#endif // _ARMHF_GAPS_H
