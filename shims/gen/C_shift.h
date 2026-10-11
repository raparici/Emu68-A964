/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 shims: shifts and rotates. */

#ifndef _SHIMS_C_shift_H
#define _SHIMS_C_shift_H

#include "../Emu68_ARM_shims.h"   // pulls in ARM.h, INSN_TO_LE, ARM_CC_*, arm_encode_imm12

static inline uint32_t lsl(uint8_t rd, uint8_t rn, uint8_t lsb) { return lsl_immed(rd, rn, lsb); }
static inline uint32_t lsr(uint8_t rd, uint8_t rn, uint8_t lsb) { return lsb ? lsr_immed(rd, rn, lsb) : lsl_immed(rd, rn, 0); }
static inline uint32_t asr(uint8_t rd, uint8_t rn, uint8_t lsb) { return lsb ? asr_immed(rd, rn, lsb) : lsl_immed(rd, rn, 0); }
static inline uint32_t ror(uint8_t rd, uint8_t rn, uint8_t lsb) { return lsb ? ror_immed(rd, rn, lsb) : lsl_immed(rd, rn, 0); }

static inline uint32_t lslv(uint8_t rd, uint8_t rn, uint8_t rm) { return lsl_reg(rd, rn, rm); }
static inline uint32_t lsrv(uint8_t rd, uint8_t rn, uint8_t rm) { return lsr_reg(rd, rn, rm); }
static inline uint32_t rorv(uint8_t rd, uint8_t rn, uint8_t rm) { return ror_reg(rd, rn, rm); }

#endif // _SHIMS_C_shift_H
