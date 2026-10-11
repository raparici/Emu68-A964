/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 shims: loads and stores. */

#ifndef _SHIMS_B_loadstore_H
#define _SHIMS_B_loadstore_H

#include "../Emu68_ARM_shims.h"   // pulls in ARM.h, INSN_TO_LE, ARM_CC_*, arm_encode_imm12

static inline int32_t shim_sext9(int16_t offset9)
{
    int32_t v = offset9 & 0x1ff;
    if (v & 0x100) v -= 0x200;   // bit 8 is the sign bit of a 9-bit field
    return v;
}

static inline uint32_t ldur_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    return ldr_offset(rn & 15, rt & 15, (int16_t)shim_sext9(offset9));   // LDR Rt,[Rn,#off]
}
static inline uint32_t ldurb_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    return ldrb_offset(rn & 15, rt & 15, (int16_t)shim_sext9(offset9));  // LDRB Rt,[Rn,#off]
}
static inline uint32_t stur_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    return str_offset(rn & 15, rt & 15, (int16_t)shim_sext9(offset9));   // STR Rt,[Rn,#off]
}
static inline uint32_t sturb_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    return strb_offset(rn & 15, rt & 15, (int16_t)shim_sext9(offset9));  // STRB Rt,[Rn,#off]
}

static inline uint32_t ldurh_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    return ldrh_offset(rn & 15, rt & 15, (int16_t)shim_sext9(offset9));  // LDRH Rt,[Rn,#off]
}
static inline uint32_t sturh_offset(uint8_t rn, uint8_t rt, int16_t offset9)
{
    return strh_offset(rn & 15, rt & 15, (int16_t)shim_sext9(offset9));  // STRH Rt,[Rn,#off]
}

static inline uint32_t ldrsh_offset_preindex(uint8_t rn, uint8_t rt, int16_t offset9)
{
    int32_t off = shim_sext9(offset9);
    uint32_t base = (off >= 0) ? 0x01f000f0u : 0x017000f0u;
    uint32_t m = (uint32_t)(off >= 0 ? off : -off) & 0xff;
    return INSN_TO_LE(base | (ARM_CC_AL << 28)
                      | ((uint32_t)(rn & 15) << 16) | ((uint32_t)(rt & 15) << 12)
                      | (m & 0x0f) | ((m << 4) & 0xf00));
}

static inline uint32_t ldxr(uint8_t rn, uint8_t rt)
{
    return ldrex(rn & 15, rt & 15);    // LDREX Rt,[Rn]
}
static inline uint32_t ldxrh(uint8_t rn, uint8_t rt)
{
    return ldrexh(rn & 15, rt & 15);   // LDREXH Rt,[Rn]
}
static inline uint32_t ldxrb(uint8_t rn, uint8_t rt)
{
    return ldrexb(rn & 15, rt & 15);   // LDREXB Rt,[Rn]
}
static inline uint32_t stxrb(uint8_t rn, uint8_t rt, uint8_t rs)
{
    return strexb(rn & 15, rt & 15, rs & 15);   // STREXB Rs,Rt,[Rn]
}

#endif // _SHIMS_B_loadstore_H
