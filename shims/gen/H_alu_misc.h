/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 shims: other ALU operations. */

#ifndef _SHIMS_H_alu_misc_H
#define _SHIMS_H_alu_misc_H

#include "../Emu68_ARM_shims.h"   // ARM.h + INSN_TO_LE + ARM_CC_* + arm_encode_imm12

#ifndef _SHIMS_SHIFT_T_DEFINED
#define _SHIMS_SHIFT_T_DEFINED
typedef enum { LSL = 0, LSR = 1, ASR = 2, ROR = 3 } shift_t;
#endif

static inline uint32_t msub(uint8_t rd, uint8_t ra, uint8_t rn, uint8_t rm)
{
    return mls(rd, ra, rm, rn);
}

// ---- adcs : A64 "ADCS Rd,Rn,Rm" (add with carry, set flags) -----------------
static inline uint32_t adcs(uint8_t rd, uint8_t rn, uint8_t rm)
{
    return adcs_reg(rd, rn, rm, 0);   // ARM.h: ADCS Rd,Rn,Rm,LSL #0 (sets ARM_CC_AL)
}

// ---- sbcs : A64 "SBCS Rd,Rn,Rm" (subtract with carry, set flags) ------------
static inline uint32_t sbcs(uint8_t rd, uint8_t rn, uint8_t rm)
{
    return sbcs_reg(rd, rn, rm, 0);   // ARM.h: SBCS Rd,Rn,Rm,LSL #0 (sets ARM_CC_AL)
}

static inline uint32_t ngcs(uint8_t rd, uint8_t rm)
{
    return INSN_TO_LE(0x02f00000 | (ARM_CC_AL << 28)
                      | ((uint32_t)(rd & 15) << 12)
                      | ((uint32_t)(rm & 15) << 16));   // RSCS Rd, Rm, #0
}

static inline uint32_t dsb_sy(void)
{
    return INSN_TO_LE(0xf57ff04f);   // DSB SY
}

// ---- dmb_ish : A64 "DMB ISH" -> A32 "DMB ISH" ------------------------------
static inline uint32_t dmb_ish(void)
{
    return INSN_TO_LE(0xf57ff05b);   // DMB ISH
}

static inline uint32_t hint(uint8_t h)
{
    return INSN_TO_LE(0x0320f000 | (ARM_CC_AL << 28) | (h & 0xff));
}
static inline uint32_t wfe(void) { return hint(2); }   // WFE
static inline uint32_t wfi(void) { return hint(3); }   // WFI

static inline uint32_t dc_ivac(uint8_t rt)
{
    return mcr(15, 0, rt, 7, 6, 1);    // MCR p15,0,Rt,c7,c6,1
}
static inline uint32_t dc_civac(uint8_t rt)
{
    return mcr(15, 0, rt, 7, 14, 1);   // MCR p15,0,Rt,c7,c14,1
}

static inline uint32_t set_nzcv(uint8_t rt)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x0128f000 | (rt & 15));   // MSR APSR_nzcvq, Rt
}

static inline uint32_t add_immed_lsl12(uint8_t rd, uint8_t rn, uint16_t imm12)
{
    uint32_t v = (uint32_t)(imm12 & 0xfff) << 12;
    uint16_t f;
    if (arm_encode_imm12(v, &f))
        return INSN_TO_LE(0x02800000 | (ARM_CC_AL << 28)
                          | ((uint32_t)(rn & 15) << 16)
                          | ((uint32_t)(rd & 15) << 12) | f);   // ADD Rd,Rn,#v
    return INSN_TO_LE(0xe7f000f0);   // permanently-undefined: traps non-expressible imm
}
static inline uint32_t sub_immed_lsl12(uint8_t rd, uint8_t rn, uint16_t imm12)
{
    uint32_t v = (uint32_t)(imm12 & 0xfff) << 12;
    uint16_t f;
    if (arm_encode_imm12(v, &f))
        return INSN_TO_LE(0x02400000 | (ARM_CC_AL << 28)
                          | ((uint32_t)(rn & 15) << 16)
                          | ((uint32_t)(rd & 15) << 12) | f);   // SUB Rd,Rn,#v
    return INSN_TO_LE(0xe7f000f0);   // permanently-undefined: traps non-expressible imm
}

#endif // _SHIMS_H_alu_misc_H
