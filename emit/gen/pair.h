/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* Multi-word A32 sequences: register-pair loads and stores. */

#ifndef _EMIT_pair_H
#define _EMIT_pair_H

#include "../Emu68_ARM_emit.h"   // pulls in ARM.h leaf encoders (str_offset, ldr_offset,
                                 // str_offset_preindex, add_immed, ...)

static inline void EMIT_stp_preindex(uint32_t **s, uint8_t rn, uint8_t rt1,
                                     uint8_t rt2, int16_t imm)
{
    *(*s)++ = str_offset_preindex(rn & 15, rt1 & 15, imm);  // STR rt1,[rn,#imm]!
    *(*s)++ = str_offset(rn & 15, rt2 & 15, 4);             // STR rt2,[rn,#4]
}

static inline void EMIT_ldp_preindex(uint32_t **s, uint8_t rn, uint8_t rt1,
                                     uint8_t rt2, int16_t imm)
{
    *(*s)++ = ldr_offset_preindex(rn & 15, rt1 & 15, imm);  // LDR rt1,[rn,#imm]!
    *(*s)++ = ldr_offset(rn & 15, rt2 & 15, 4);             // LDR rt2,[rn,#4]
}

static inline void EMIT_stp_postindex(uint32_t **s, uint8_t rn, uint8_t rt1,
                                      uint8_t rt2, int16_t imm)
{
    *(*s)++ = str_offset(rn & 15, rt1 & 15, 0);             // STR rt1,[rn]
    *(*s)++ = str_offset(rn & 15, rt2 & 15, 4);             // STR rt2,[rn,#4]
    *(*s)++ = add_immed(rn & 15, rn & 15, (uint16_t)imm);   // ADD rn,rn,#imm
}

static inline void EMIT_ldp_postindex(uint32_t **s, uint8_t rn, uint8_t rt1,
                                      uint8_t rt2, int16_t imm)
{
    *(*s)++ = ldr_offset(rn & 15, rt1 & 15, 0);             // LDR rt1,[rn]
    *(*s)++ = ldr_offset(rn & 15, rt2 & 15, 4);             // LDR rt2,[rn,#4]
    *(*s)++ = add_immed(rn & 15, rn & 15, (uint16_t)imm);   // ADD rn,rn,#imm
}

#endif // _EMIT_pair_H
