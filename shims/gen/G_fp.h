/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 shims: VFP / NEON floating point. */

#ifndef _SHIMS_G_fp_H
#define _SHIMS_G_fp_H

#include "../Emu68_ARM_shims.h"   // ARM.h + INSN_TO_LE + ARM_CC_* + arm_encode_imm12

// permanently-undefined A32 word (UDF) - traps if an out-of-range offset slips in
#define _G_FP_UDF 0xe7f000f0u

static inline uint32_t fldd_pimm(uint8_t v_dst, uint8_t base, uint16_t offset12)
{
    uint32_t imm8 = (uint32_t)offset12 * 2u;
    if (imm8 > 0xff) return INSN_TO_LE(_G_FP_UDF);
    // 0x0d900b00 = VLDR.64 Dd,[Rn,#+imm8*4] ; cc<<28 ; Rn<<16 ; Dd<<12 ; imm8
    return INSN_TO_LE(0x0d900b00 | (ARM_CC_AL << 28)
                      | ((uint32_t)(base & 15) << 16)
                      | ((uint32_t)(v_dst & 15) << 12)
                      | imm8);
}

static inline uint32_t flds_pimm(uint8_t v_dst, uint8_t base, uint16_t offset12)
{
    uint32_t imm8 = (uint32_t)offset12;
    if (imm8 > 0xff) return INSN_TO_LE(_G_FP_UDF);
    // 0x0d900a00 = VLDR.32 Sd,[Rn,#+imm8*4]
    return INSN_TO_LE(0x0d900a00 | (ARM_CC_AL << 28)
                      | ((uint32_t)(base & 15) << 16)
                      | ((uint32_t)((v_dst >> 1) & 15) << 12)
                      | ((uint32_t)(v_dst & 1) << 22)
                      | imm8);
}

// ---- VSTR Dd, [Rn, #off12*8] : A64 STR(double,scaled) -> A32 VSTR.64 ----------
static inline uint32_t fstd_pimm(uint8_t v_dst, uint8_t base, uint16_t offset12)
{
    uint32_t imm8 = (uint32_t)offset12 * 2u;
    if (imm8 > 0xff) return INSN_TO_LE(_G_FP_UDF);
    // 0x0d800b00 = VSTR.64 Dd,[Rn,#+imm8*4]
    return INSN_TO_LE(0x0d800b00 | (ARM_CC_AL << 28)
                      | ((uint32_t)(base & 15) << 16)
                      | ((uint32_t)(v_dst & 15) << 12)
                      | imm8);
}

// ---- VSTR Sd, [Rn, #off12*4] : A64 STR(single,scaled) -> A32 VSTR.32 ----------
static inline uint32_t fsts_pimm(uint8_t v_dst, uint8_t base, uint16_t offset12)
{
    uint32_t imm8 = (uint32_t)offset12;
    if (imm8 > 0xff) return INSN_TO_LE(_G_FP_UDF);
    // 0x0d800a00 = VSTR.32 Sd,[Rn,#+imm8*4]
    return INSN_TO_LE(0x0d800a00 | (ARM_CC_AL << 28)
                      | ((uint32_t)(base & 15) << 16)
                      | ((uint32_t)((v_dst >> 1) & 15) << 12)
                      | ((uint32_t)(v_dst & 1) << 22)
                      | imm8);
}

#endif // _SHIMS_G_fp_H
