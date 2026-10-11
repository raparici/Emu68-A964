/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* Multi-word A32 sequences for the A64 encoders that have no single ARMv7 instruction. */

#ifndef EMU68_ARM_EMIT_H
#define EMU68_ARM_EMIT_H

#include "../shims/Emu68_ARM_shims.h"   // ARM.h leaf encoders (bfi/ubfx/cmp/b_cc/...) + shims + arm_encode_imm12

#ifndef REG_EMIT_TMP
#define REG_EMIT_TMP 9
#endif

static inline void EMIT_bfxil(uint32_t **s, uint8_t Rd, uint8_t Rn,
                              uint8_t lsb, uint8_t width, uint8_t tmp)
{
    if (lsb == 0) {
        *(*s)++ = bfi(Rd, Rn, 0, width);
        return;
    }
    *(*s)++ = ubfx(tmp, Rn, lsb, width);
    *(*s)++ = bfi(Rd, tmp, 0, width);      // Rd[0 +: width] = tmp[0 +: width], rest kept
}

#include "gen/condsel.h"     // EMIT_csel/csinc/csinv/cset/csetm
#include "gen/bitfield64.h"  // EMIT_ubfx64/sbfx64/bfi64/bfxil64
#include "gen/imm64.h"       // EMIT_orr64_immed/eor64_immed/bic64_immed/ands64_immed/tst64_immed
#include "gen/pair.h"        // EMIT_stp_preindex/ldp_preindex/stp_postindex/ldp_postindex
#include "gen/branch.h"      // EMIT_cbnz/cbz/tbnz/tbz

#ifndef __aarch64__
#include "../superinc/gaps.h"
#include "../superinc/fp-gaps.h"
#include "../superinc/gaps64.h"
#endif

#endif // EMU68_ARM_EMIT_H
