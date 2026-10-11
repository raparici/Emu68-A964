/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 single-instruction implementations of A64 encoder names used by the m68k handlers. */

#ifndef EMU68_ARM_SHIMS_H
#define EMU68_ARM_SHIMS_H

#include <stdint.h>
#include "ARM.h"        // the b100dbd A32 encoder base (INSN_TO_LE, ARM_CC_*, movw/movt, ...)
#include "A64_compat.h" // A64_CC_* aliases the arch-neutral handlers reference

static inline int arm_encode_imm12(uint32_t value, uint16_t *field12)
{
    for (int r = 0; r < 16; r++) {
        uint32_t rol = (r == 0) ? value : ((value << (2 * r)) | (value >> (32 - 2 * r)));
        if (rol <= 0xff) { *field12 = (uint16_t)((r << 8) | rol); return 1; }
    }
    return 0;
}

static inline uint32_t mov_immed_u16(uint8_t reg, uint16_t val, uint8_t shift16)
{
    if (shift16 == 0)
        return movw_immed_u16(reg, val);
    uint32_t v = (uint32_t)val << (16 * shift16);
    uint16_t f;
    if (arm_encode_imm12(v, &f))
        return INSN_TO_LE(0x03a00000 | (ARM_CC_AL << 28) | ((uint32_t)(reg & 15) << 12) | f);  // MOV reg,#v
    return INSN_TO_LE(0xe7f000f0);  // permanently-undefined: traps if a non-expressible case slips through
}

#include "gen/A_64bit.h"
#include "gen/B_loadstore.h"
#include "gen/C_shift.h"
#include "gen/E_branch.h"
#include "gen/F_immediate.h"
#include "gen/G_fp.h"
#include "gen/H_alu_misc.h"

#endif // EMU68_ARM_SHIMS_H
