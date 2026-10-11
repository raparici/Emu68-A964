/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* Multi-word A32 sequences: 64-bit bitfield operations. */

#ifndef _EMIT_bitfield64_H
#define _EMIT_bitfield64_H

#include "../Emu68_ARM_emit.h"   // pulls in ../shims/Emu68_ARM_shims.h -> ARM.h leaf encoders

static inline void EMIT_ubfx64(uint32_t **s, uint8_t Rd, uint8_t Rn,
                               uint8_t lsb, uint8_t width)
{
    *(*s)++ = ubfx(Rd, Rn, lsb, width);   // 1 word, ARM_CC_AL baked in by ubfx()
}

static inline void EMIT_sbfx64(uint32_t **s, uint8_t Rd, uint8_t Rn,
                               uint8_t lsb, uint8_t width)
{
    *(*s)++ = sbfx(Rd, Rn, lsb, width);   // 1 word
}

static inline void EMIT_bfi64(uint32_t **s, uint8_t Rd, uint8_t Rn,
                              uint8_t lsb, uint8_t width)
{
    *(*s)++ = bfi(Rd, Rn, lsb, width);    // 1 word
}

static inline void EMIT_bfxil64(uint32_t **s, uint8_t Rd, uint8_t Rn,
                                uint8_t lsb, uint8_t width, uint8_t tmp)
{
    if (lsb == 0) {
        *(*s)++ = bfi(Rd, Rn, 0, width);          // 1 word
        return;
    }
    *(*s)++ = ubfx(tmp, Rn, lsb, width);
    *(*s)++ = bfi(Rd, tmp, 0, width);             // Rd[0 +: width] = tmp, rest kept
}

#endif // _EMIT_bitfield64_H
