/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* Multi-word A32 sequences: 64-bit immediates. */

#ifndef _EMIT_imm64_H
#define _EMIT_imm64_H

#include "../Emu68_ARM_emit.h"   // pulls in ../shims/Emu68_ARM_shims.h -> ARM.h leaf encoders

static inline int imm64_decode_bm(uint8_t width, uint8_t ror, uint8_t n, uint64_t *out)
{
    unsigned imms = (unsigned)(width - 1) & 0x3f;
    unsigned immr = (unsigned)ror & 0x3f;
    unsigned N    = n ? 1u : 0u;

    unsigned bits = (N << 6) | ((~imms) & 0x3f);
    int len = -1;
    for (int i = 6; i >= 0; i--) { if (bits & (1u << i)) { len = i; break; } }
    if (len < 1) return 0;

    unsigned esize  = 1u << len;           // element size in bits (2..64)
    unsigned levels = esize - 1;
    unsigned S = imms & levels;
    unsigned R = immr & levels;
    if (S == levels) return 0;             // reserved

    uint64_t welem = (S + 1 >= 64) ? ~0ULL : ((1ULL << (S + 1)) - 1);
    uint64_t rotated = (R == 0) ? welem
                                : ((welem >> R) | (welem << (esize - R)));
    uint64_t emask = (esize >= 64) ? ~0ULL : ((1ULL << esize) - 1);
    rotated &= emask;

    uint64_t wmask = 0;                     // replicate esize-bit element to 64 bits
    for (unsigned i = 0; i < 64; i += esize) wmask |= (rotated << i);
    *out = wmask;
    return 1;
}

static inline int imm64_load_lo32(uint32_t **s, uint8_t tmp, uint8_t width, uint8_t ror, uint8_t n)
{
    uint64_t v;
    if (!imm64_decode_bm(width, ror, n, &v)) {
        *(*s)++ = INSN_TO_LE(0xe7f000f0);            // permanently-undefined trap
        return 0;
    }
    uint32_t lo = (uint32_t)(v & 0xffffffffu);
    *(*s)++ = movw_immed_u16(tmp, (uint16_t)(lo & 0xffff));
    if ((lo >> 16) != 0) {
        *(*s)++ = movt_immed_u16(tmp, (uint16_t)(lo >> 16));
        return 2;
    }
    return 1;
}

static inline void EMIT_orr64_immed(uint32_t **s, uint8_t Rd, uint8_t Rn,
                                    uint8_t width, uint8_t ror, uint8_t n, uint8_t tmp)
{
    if (!imm64_load_lo32(s, tmp, width, ror, n)) return;   // trap already emitted
    *(*s)++ = orr_reg(Rd, Rn, tmp, LSL, 0);                     // ORR Rd, Rn, tmp
}

static inline void EMIT_eor64_immed(uint32_t **s, uint8_t Rd, uint8_t Rn,
                                    uint8_t width, uint8_t ror, uint8_t n, uint8_t tmp)
{
    if (!imm64_load_lo32(s, tmp, width, ror, n)) return;
    *(*s)++ = eor_reg(Rd, Rn, tmp, LSL, 0);                     // EOR Rd, Rn, tmp
}

static inline void EMIT_bic64_immed(uint32_t **s, uint8_t Rd, uint8_t Rn,
                                    uint8_t width, uint8_t ror, uint8_t n, uint8_t tmp)
{
    uint8_t a_width = (uint8_t)(64 - width);
    uint8_t a_ror   = (uint8_t)(ror - width);
    if (!imm64_load_lo32(s, tmp, a_width, a_ror, n)) return;
    *(*s)++ = and_reg(Rd, Rn, tmp, LSL, 0);                     // AND Rd, Rn, tmp
}

static inline void EMIT_ands64_immed(uint32_t **s, uint8_t Rd, uint8_t Rn,
                                     uint8_t width, uint8_t ror, uint8_t n, uint8_t tmp)
{
    if (!imm64_load_lo32(s, tmp, width, ror, n)) return;
    if ((Rd & 31) == 31)
        *(*s)++ = tst_reg(Rn, tmp, LSL, 0);                     // TST Rn, tmp (flags only)
    else
        *(*s)++ = ands_reg(Rd, Rn, tmp, LSL, 0);                // ANDS Rd, Rn, tmp
}

static inline void EMIT_tst64_immed(uint32_t **s, uint8_t Rn,
                                    uint8_t width, uint8_t ror, uint8_t n, uint8_t tmp)
{
    if (!imm64_load_lo32(s, tmp, width, ror, n)) return;
    *(*s)++ = tst_reg(Rn, tmp, LSL, 0);                         // TST Rn, tmp
}

#endif // _EMIT_imm64_H
