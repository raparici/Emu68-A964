/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* Multi-word A32 sequences: branches with a 64-bit reach. */

#ifndef _EMIT_branch_H
#define _EMIT_branch_H

#include "../Emu68_ARM_emit.h"   // -> shims/Emu68_ARM_shims.h : ARM.h leaf encoders
                                 //    (cmp_immed/tst_cc_immed/b_cc) + arm_encode_imm12

static inline int32_t emit_branch_off(int32_t off_words)
{
    return off_words;
}

static inline void EMIT_cbnz(uint32_t **s, uint8_t Rn, int32_t off)
{
    *(*s)++ = cmp_immed(Rn, 0);                       // CMP Rn,#0
    *(*s)++ = b_cc(ARM_CC_NE, emit_branch_off(off));  // B.NE  off-3
}

static inline void EMIT_cbz(uint32_t **s, uint8_t Rn, int32_t off)
{
    *(*s)++ = cmp_immed(Rn, 0);                       // CMP Rn,#0
    *(*s)++ = b_cc(ARM_CC_EQ, emit_branch_off(off));  // B.EQ  off-3
}

static inline void EMIT_tbnz(uint32_t **s, uint8_t Rn, uint8_t bit, int32_t off)
{
    uint16_t imm12;
    arm_encode_imm12((uint32_t)1u << (bit & 31), &imm12);  // 1<<bit -> imm8-ROR field
    *(*s)++ = tst_cc_immed(ARM_CC_AL, Rn, imm12);          // TST Rn,#(1<<bit)
    *(*s)++ = b_cc(ARM_CC_NE, emit_branch_off(off));       // B.NE  off-3
}

static inline void EMIT_tbz(uint32_t **s, uint8_t Rn, uint8_t bit, int32_t off)
{
    uint16_t imm12;
    arm_encode_imm12((uint32_t)1u << (bit & 31), &imm12);  // 1<<bit -> imm8-ROR field
    *(*s)++ = tst_cc_immed(ARM_CC_AL, Rn, imm12);          // TST Rn,#(1<<bit)
    *(*s)++ = b_cc(ARM_CC_EQ, emit_branch_off(off));       // B.EQ  off-3
}

#endif // _EMIT_branch_H
