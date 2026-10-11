/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* A32 shims: branches. */

#ifndef _SHIMS_E_branch_H
#define _SHIMS_E_branch_H

#include "../Emu68_ARM_shims.h"   // ARM.h + INSN_TO_LE + ARM_CC_* + arm_encode_imm12

static inline uint32_t br(uint8_t rt)
{
    return INSN_TO_LE(0x012fff10 | (ARM_CC_AL << 28) | (rt & 15));
}

static inline uint32_t blr(uint8_t rt)
{
    return INSN_TO_LE(0x012fff30 | (ARM_CC_AL << 28) | (rt & 15));
}

static inline uint32_t adr(uint8_t rd, uint32_t imm21)
{
    int32_t off = (int32_t)imm21 - 8;      /* A32 PC = this + 8 */
    uint32_t mag = (off < 0) ? (uint32_t)(-off) : (uint32_t)off;
    uint16_t f;
    if (arm_encode_imm12(mag, &f)) {
        uint32_t op = (off < 0) ? 0x024f0000u   // SUB Rd, PC, #imm   (PC = reg 15)
                                : 0x028f0000u;  // ADD Rd, PC, #imm
        return INSN_TO_LE(op | (ARM_CC_AL << 28) | (((uint32_t)(rd & 15)) << 12) | f);
    }
    return INSN_TO_LE(0xe7f000f0);  // permanently-undefined: traps a non-expressible offset
}

#endif // _SHIMS_E_branch_H
