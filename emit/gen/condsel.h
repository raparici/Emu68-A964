/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* Multi-word A32 sequences: conditional selects. */

#ifndef _EMIT_condsel_H
#define _EMIT_condsel_H

#include "../Emu68_ARM_emit.h"   // pulls in ARM.h leaf encoders (mov_cc_reg, mvn_reg, add_cc_immed, ...)
#include <assert.h>

static inline uint8_t EMIT_invert_cc(uint8_t cc)
{
    assert(cc < ARM_CC_AL);          // 0..0xd only; AL/NV rejected
    return (uint8_t)(cc ^ 1);
}

static inline uint32_t EMIT__mvn_cc_reg(uint8_t cc, uint8_t dest, uint8_t src)
{
    uint32_t w = mvn_reg(dest, src, LSL, 0);          // MVN<AL> Rd, Rm  (0xe1e0...)
    return (w & 0x0fffffffu) | ((uint32_t)cc << 28);
}

static inline void EMIT_csel(uint32_t **s, uint8_t Rd, uint8_t Rn, uint8_t Rm,
                             uint8_t cond)
{
    assert(Rn != 31 && Rm != 31);                // no WZR support (none needed)
    *(*s)++ = mov_cc_reg(cond, Rd, Rn);
    *(*s)++ = mov_cc_reg(EMIT_invert_cc(cond), Rd, Rm);
}

static inline void EMIT_csinc(uint32_t **s, uint8_t Rd, uint8_t Rn, uint8_t Rm,
                              uint8_t cond)
{
    assert(Rn != 31 && Rm != 31);
    *(*s)++ = mov_cc_reg(cond, Rd, Rn);
    *(*s)++ = add_cc_immed(EMIT_invert_cc(cond), Rd, Rm, 1);
}

static inline void EMIT_csinv(uint32_t **s, uint8_t Rd, uint8_t Rn, uint8_t Rm,
                              uint8_t cond)
{
    assert(Rn != 31);
    *(*s)++ = mov_cc_reg(cond, Rd, Rn);
    if (Rm == 31)
        *(*s)++ = mvn_cc_immed_u8(EMIT_invert_cc(cond), Rd, 0);   // MVN<inv> Rd,#0
    else
        *(*s)++ = EMIT__mvn_cc_reg(EMIT_invert_cc(cond), Rd, Rm); // MVN<inv> Rd,Rm
}

static inline void EMIT_cset(uint32_t **s, uint8_t Rd, uint8_t cond)
{
    *(*s)++ = mov_immed_u8(Rd, 0);
    *(*s)++ = mov_cc_immed_u8(cond, Rd, 1);
}

static inline void EMIT_csetm(uint32_t **s, uint8_t Rd, uint8_t cond)
{
    *(*s)++ = mov_immed_u8(Rd, 0);
    *(*s)++ = mvn_cc_immed_u8(cond, Rd, 0);
}

#endif // _EMIT_condsel_H
