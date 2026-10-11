/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* Emitter helpers placed in the image's tail. */

#include <stdint.h>
#include <stddef.h>
#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"

uint8_t M68K_GetSRMask_DivW(uint16_t *insn_stream) __attribute__((section(".hunt_tail_text"), noinline));
uint8_t M68K_GetSRMask_DivW(uint16_t *insn_stream)
{
    return (uint8_t)(M68K_GetSRMask(insn_stream) | SR_V);
}

uint32_t *EMIT_LoadFromEffectiveAddress_NBCDStore(uint32_t *ptr, uint8_t size, uint8_t *arm_reg, uint8_t ea, uint16_t *m68k_ptr,
                                                  uint8_t *ext_words, uint8_t read_only, int32_t *imm_offset)
    __attribute__((section(".hunt_tail_text"), noinline));
uint32_t *EMIT_LoadFromEffectiveAddress_NBCDStore(uint32_t *ptr, uint8_t size, uint8_t *arm_reg, uint8_t ea, uint16_t *m68k_ptr,
                                                  uint8_t *ext_words, uint8_t read_only, int32_t *imm_offset)
{
    ptr = EMIT_LoadFromEffectiveAddress(ptr, size, arm_reg, ea, m68k_ptr, ext_words, read_only, imm_offset);
    RA_FreeARMRegister(&ptr, *arm_reg);
    return ptr;
}

struct Result32 sidiv_tail(int32_t n, int32_t d) __attribute__((section(".fix_tail_text"), noinline));
struct Result32 sidiv_tail(int32_t n, int32_t d)
{
    uint32_t un = n < 0 ? 0u - (uint32_t)n : (uint32_t)n;
    uint32_t ud = d < 0 ? 0u - (uint32_t)d : (uint32_t)d;
    struct Result32 res = uidiv(un, ud);
    if ((n < 0) != (d < 0))
        res.q = 0u - res.q;
    if (n < 0)
        res.r = 0u - res.r;
    return res;
}
struct Result64 sldiv_tail(int64_t n, int64_t d) __attribute__((section(".fix_tail_text"), noinline));
struct Result64 sldiv_tail(int64_t n, int64_t d)
{
    uint64_t un = n < 0 ? 0u - (uint64_t)n : (uint64_t)n;
    uint64_t ud = d < 0 ? 0u - (uint64_t)d : (uint64_t)d;
    struct Result64 res = uldiv(un, ud);
    if ((n < 0) != (d < 0))
        res.q = 0u - res.q;
    if (n < 0)
        res.r = 0u - res.r;
    return res;
}
