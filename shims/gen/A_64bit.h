/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 shims: 64-bit operations that reduce to one 32-bit instruction. */

#ifndef _SHIMS_A_64bit_H
#define _SHIMS_A_64bit_H

#include "../Emu68_ARM_shims.h"   // pulls in ARM.h (INSN_TO_LE, ARM_CC_*, encoders) + arm_encode_imm12

#ifndef _SHIMS_SHIFT_T_DEFINED
#define _SHIMS_SHIFT_T_DEFINED
typedef enum { LSL = 0, LSR = 1, ASR = 2, ROR = 3 } shift_t;
#endif

static inline uint32_t a32_dp_reg(uint32_t opc_s, uint8_t rd, uint8_t rn,
                                  uint8_t rm, shift_t shift, uint8_t amount)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | opc_s
                      | ((uint32_t)(rn & 15) << 16)
                      | ((uint32_t)(rd & 15) << 12)
                      | ((uint32_t)(amount & 31) << 7)
                      | ((uint32_t)(shift & 3) << 5)
                      | (rm & 15));
}
static inline uint32_t a32_dp_test_reg(uint32_t opc, uint8_t rn, uint8_t rm,
                                       shift_t shift, uint8_t amount)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | opc
                      | ((uint32_t)(rn & 15) << 16)
                      | ((uint32_t)(amount & 31) << 7)
                      | ((uint32_t)(shift & 3) << 5)
                      | (rm & 15));
}

static inline uint32_t ands64_reg(uint8_t rd, uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount)
{
    if ((rd & 31) == 31)
        return a32_dp_test_reg(0x01100000, rn, rm, shift, amount);   // TST
    return a32_dp_reg(0x00100000, rd, rn, rm, shift, amount);
}

// orr64_reg -> ORR Rd, Rn, Rm, <shift> #amount
static inline uint32_t orr64_reg(uint8_t rd, uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount)
{
    return a32_dp_reg(0x01800000, rd, rn, rm, shift, amount);
}

// eor64_reg -> EOR Rd, Rn, Rm, <shift> #amount
static inline uint32_t eor64_reg(uint8_t rd, uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount)
{
    return a32_dp_reg(0x00200000, rd, rn, rm, shift, amount);
}

// bic64_reg -> BIC Rd, Rn, Rm, <shift> #amount
static inline uint32_t bic64_reg(uint8_t rd, uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount)
{
    return a32_dp_reg(0x01c00000, rd, rn, rm, shift, amount);
}

static inline uint32_t a32_shift_reg(uint8_t shtype, uint8_t rd, uint8_t rn, uint8_t rm)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x01a00000
                      | ((uint32_t)(rd & 15) << 12)
                      | ((uint32_t)(rm & 15) << 8)
                      | ((uint32_t)(shtype & 3) << 5)
                      | (1 << 4)
                      | (rn & 15));
}
static inline uint32_t lslv64(uint8_t rd, uint8_t rn, uint8_t rm) { return a32_shift_reg(0, rd, rn, rm); }
static inline uint32_t lsrv64(uint8_t rd, uint8_t rn, uint8_t rm) { return a32_shift_reg(1, rd, rn, rm); }

static inline uint32_t sxtb64(uint8_t rd, uint8_t rn)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x06af0070 | ((uint32_t)(rd & 15) << 12) | (rn & 15)); // SXTB Rd,Rn
}
static inline uint32_t sxth64(uint8_t rd, uint8_t rn)
{
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x06bf0070 | ((uint32_t)(rd & 15) << 12) | (rn & 15)); // SXTH Rd,Rn
}

static inline uint32_t sub64_immed(uint8_t rd, uint8_t rn, uint16_t imm12)
{
    uint16_t f;
    if (arm_encode_imm12((uint32_t)(imm12 & 0xfff), &f))
        return INSN_TO_LE((ARM_CC_AL << 28) | 0x02400000
                          | ((uint32_t)(rn & 15) << 16)
                          | ((uint32_t)(rd & 15) << 12)
                          | f);                                 // SUB Rd,Rn,#imm
    return INSN_TO_LE(0xe7f000f0);                              // permanently-undefined trap
}

#endif // _SHIMS_A_64bit_H
