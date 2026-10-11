/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* Register-allocator entry points that release the cached FPCR / FPSR first. */

#include <stdint.h>
#include <stddef.h>
#include "RegisterAllocator.h"
#include "M68k.h"

#define NOFP_FLUSH(p) do { RA_FlushFPCR(p); RA_FlushFPSR(p); } while (0)

__attribute__((section(".fpfix_tail_text"), noinline))
uint8_t RA_AllocARMRegister_NoFP(uint32_t **arm_stream)
{
    NOFP_FLUSH(arm_stream);
    return RA_AllocARMRegister(arm_stream);
}

__attribute__((section(".fpfix_tail_text"), noinline))
uint8_t RA_MapM68kRegister_NoFP(uint32_t **arm_stream, uint8_t m68k_reg)
{
    NOFP_FLUSH(arm_stream);
    return RA_MapM68kRegister(arm_stream, m68k_reg);
}

__attribute__((section(".fpfix_tail_text"), noinline))
uint32_t *EMIT_LoadFromEffectiveAddress_NoFP(uint32_t *ptr, uint8_t size, uint8_t *arm_reg, uint8_t ea, uint16_t *m68k_ptr,
                                             uint8_t *ext_words, uint8_t read_only, int32_t *imm_offset)
{
    NOFP_FLUSH(&ptr);
    return EMIT_LoadFromEffectiveAddress(ptr, size, arm_reg, ea, m68k_ptr, ext_words, read_only, imm_offset);
}
