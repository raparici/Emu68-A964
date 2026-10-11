/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 shims: immediates. */

#ifndef _SHIMS_F_immediate_H
#define _SHIMS_F_immediate_H

#include "../Emu68_ARM_shims.h"   // ARM.h + INSN_TO_LE + ARM_CC_* + arm_encode_imm12

static inline uint32_t movk_immed_u16(uint8_t reg, uint16_t val, uint8_t shift16)
{
    if (shift16 == 1)
        return movt_immed_u16(reg, val);   // ARM.h:189 -> 0x03400000 | (AL<<28) | ...
    return INSN_TO_LE(0xe7f000f0);
}

static inline uint32_t movn_immed_u16(uint8_t reg, uint16_t val, uint8_t shift16)
{
    if (shift16 == 0) {
        uint16_t f;
        if (arm_encode_imm12((uint32_t)val, &f))
            return INSN_TO_LE(0x03e00000 | (ARM_CC_AL << 28) | ((uint32_t)(reg & 15) << 12) | f); // MVN reg,#val
    }
    return INSN_TO_LE(0xe7f000f0);
}

#endif // _SHIMS_F_immediate_H
