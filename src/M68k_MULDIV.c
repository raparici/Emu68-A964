#include "Emu68_ARM_emit.h"
/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "config.h"   /* CPUMODEL_* - the tri-state CPU model predicates */

static inline uint32_t *emit_flags_nz_from_shifted(uint32_t *ptr, uint8_t reg, uint8_t shift)
{
#ifdef __aarch64__
    *ptr++ = adds_reg(31, 31, reg, LSL, shift);
#else
    uint8_t nzr = RA_AllocARMRegister(&ptr);
    *ptr++ = lsls_immed(nzr, reg, shift);
    RA_FreeARMRegister(&ptr, nzr);
#endif
    return ptr;
}
#include "EmuFeatures.h"

uint32_t *EMIT_MULU(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr) __attribute__((alias("EMIT_MUL_DIV")));
uint32_t *EMIT_MULS(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr) __attribute__((alias("EMIT_MUL_DIV")));

/* AArch64 has both multiply and divide. No need to have them in C form */
#ifndef __aarch64__
struct Result32 uidiv(uint32_t n, uint32_t d)
{
    struct Result32 res = { 0, 0 };

    if (n == 0)
        return res;

    for (int i = 31 - __builtin_clzl(n); i >= 0; --i)
    {
        res.r <<= 1;
        if (n & (1 << i)) res.r |= 1;
        if (res.r >= d) {
            res.r -= d;
            res.q |= 1 << i;
        }
    }

    return res;
}

struct Result32 sidiv(int32_t n, int32_t d)
{
    struct Result32 res = { 0, 0 };

    if (d < 0) {
        res = sidiv(n, -d);
        res.q = -res.q;
        return res;
    }

    if (n < 0) {
        /* Truncation toward zero: negate both quotient and remainder */
        res = sidiv(-n, d);
        res.q = -res.q;
        if (res.r != 0)
            res.r = -(int32_t)res.r;  /* remainder has sign of dividend */
        return res;
    }

    res = uidiv(n, d);

    return res;
}

struct Result64 uldiv(uint64_t n, uint64_t d)
{
    struct Result64 res = { 0, 0 };

    if (n == 0)
        return res;

    for (int i = 63 - __builtin_clzll(n); i >= 0; --i)
    {
        res.r <<= 1;
        if (n & ((uint64_t)1 << i)) res.r |= 1;
        if (res.r >= d) {
            res.r -= d;
            res.q |= (uint64_t)1 << i;
        }
    }

    return res;
}

struct Result64 sldiv(int64_t n, int64_t d)
{
    struct Result64 res = { 0, 0 };

    if (d < 0) {
        res = sldiv(n, -d);
        res.q = -res.q;
        return res;
    }

    if (n < 0) {
        /* Truncation toward zero: negate both quotient and remainder */
        res = sldiv(-n, d);
        res.q = -res.q;
        if (res.r != 0)
            res.r = -(int64_t)res.r;  /* remainder has sign of dividend */
        return res;
    }

    res = uldiv(n, d);

    return res;
}

/* ARM EABI divide helpers implemented in support_rpi.c (armhf target only) */
extern void __aeabi_uidivmod(void);
extern void __aeabi_idivmod(void);
extern void emu68_uldivmod_lepair(void);   /* unsigned 64/64: r0:r1 = n lo:hi, r2:r3 = d lo:hi -> q lo:hi, r lo:hi */
extern void emu68_ldivmod_lepair(void);    /* signed   64/64, the same LE-pair contract */
#endif

uint32_t *EMIT_MULS_W(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t update_mask = M68K_GetSRMask(*m68k_ptr - 1);
    uint8_t reg;
    uint8_t src = 0xff;
    uint8_t ext_words = 0;

    // Fetch 16-bit register: source and destination
    reg = RA_MapM68kRegister(&ptr, (opcode >> 9) & 7);
    RA_SetDirtyM68kRegister(&ptr, (opcode >> 9) & 7);

    // Fetch 16-bit multiplicant
    ptr = EMIT_LoadFromEffectiveAddress(ptr, 2, &src, opcode & 0x3f, *m68k_ptr, &ext_words, 0, NULL);

    // Sign-extend 16-bit multiplicants
#ifdef __aarch64__
    *ptr++ = sxth(reg, reg);
    *ptr++ = sxth(src, src);

    *ptr++ = mul(reg, reg, src);
#else
    *ptr++ = sxth(reg, reg);
    *ptr++ = sxth(src, src);

    *ptr++ = muls(reg, reg, src);
#endif
    RA_FreeARMRegister(&ptr, src);

    ptr = EMIT_AdvancePC(ptr, 2 * (ext_words + 1));
    (*m68k_ptr) += ext_words;

    if (update_mask)
    {
        uint8_t cc = RA_ModifyCC(&ptr);
#ifdef __aarch64__
        *ptr++ = cmn_reg(31, reg, LSL, 0);
#endif
        ptr = EMIT_GetNZ00(ptr, cc, &update_mask);
        if (update_mask & SR_Z) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
        }
        if (update_mask & SR_N) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
        }
    }

    return ptr;
}

uint32_t *EMIT_MULU_W(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t update_mask = M68K_GetSRMask(*m68k_ptr - 1);
    uint8_t reg;
    uint8_t src = 0xff;
    uint8_t ext_words = 0;

    // Fetch 16-bit register: source and destination
    reg = RA_MapM68kRegister(&ptr, (opcode >> 9) & 7);
    RA_SetDirtyM68kRegister(&ptr, (opcode >> 9) & 7);

    // Fetch 16-bit multiplicant
    ptr = EMIT_LoadFromEffectiveAddress(ptr, 2, &src, opcode & 0x3f, *m68k_ptr, &ext_words, 0, NULL);

    // Sign-extend 16-bit multiplicants
#ifdef __aarch64__
    *ptr++ = uxth(reg, reg);
    *ptr++ = uxth(src, src);
    *ptr++ = mul(reg, reg, src);
#else
    *ptr++ = uxth(reg, reg);
    *ptr++ = uxth(src, src);

    *ptr++ = muls(reg, reg, src);
#endif
    RA_FreeARMRegister(&ptr, src);

    ptr = EMIT_AdvancePC(ptr, 2 * (ext_words + 1));
    (*m68k_ptr) += ext_words;

    if (update_mask)
    {
        uint8_t cc = RA_ModifyCC(&ptr);
#ifdef __aarch64__
        *ptr++ = cmn_reg(31, reg, LSL, 0);
#endif
        ptr = EMIT_GetNZ00(ptr, cc, &update_mask);
        if (update_mask & SR_Z) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
        }
        if (update_mask & SR_N) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
        }
    }

    return ptr;
}

uint32_t *EMIT_MULS_L(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t update_mask = M68K_GetSRMask(*m68k_ptr - 1);
    uint8_t reg_dl;
    uint8_t reg_dh = 0xff;
    uint8_t src = 0xff;
    uint8_t ext_words = 1;
    uint16_t opcode2 = BE16((*m68k_ptr)[0]);
#ifdef EMU68_CPU040
    extern int CpuModel;
    const int cpumodel = CpuModel;
#else
    const int cpumodel = CPUMODEL_68EC020;
#endif

    // Fetch 32-bit register: source and destination
    reg_dl = RA_MapM68kRegister(&ptr, (opcode2 >> 12) & 7);
    RA_SetDirtyM68kRegister(&ptr, (opcode2 >> 12) & 7);

    // Fetch 32-bit multiplicant
    ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &src, opcode & 0x3f, *m68k_ptr, &ext_words, 1, NULL);

    if (opcode2 & (1 << 10))
    {
        reg_dh = RA_MapM68kRegisterForWrite(&ptr, (opcode2 & 7));
    }
    else
    {
        reg_dh = RA_AllocARMRegister(&ptr);
    }

#ifdef __aarch64__
    if (opcode2 & (1 << 11))
        *ptr++ = smull(reg_dl, reg_dl, src);
    else
        *ptr++ = umull(reg_dl, reg_dl, src);
    if (opcode2 & (1 << 10) && (reg_dh != reg_dl))
    {
        *ptr++ = add64_reg(reg_dh, 31, reg_dl, LSR, 32);
    }
#else
    {
        uint8_t rdhi = reg_dh, hi_scratch = 0xff;
        if (rdhi == reg_dl) { hi_scratch = RA_AllocARMRegister(&ptr); rdhi = hi_scratch; }
        if (opcode2 & (1 << 11))
            *ptr++ = smulls(rdhi, reg_dl, reg_dl, src);
        else
            *ptr++ = umulls(rdhi, reg_dl, reg_dl, src);
        if (hi_scratch != 0xff) {
            if (CPUMODEL_IS_020(cpumodel)) *ptr++ = mov_reg(reg_dl, hi_scratch);
            RA_FreeARMRegister(&ptr, hi_scratch);
        }
    }
#endif

    RA_FreeARMRegister(&ptr, src);

    ptr = EMIT_AdvancePC(ptr, 2 * (ext_words + 1));
    (*m68k_ptr) += ext_words;

    if (update_mask)
    {
        uint8_t cc = RA_ModifyCC(&ptr);

#ifdef __aarch64__
        if (opcode2 & (1 << 10)) { 
            *ptr++ = cmn64_reg(31, reg_dl, LSL, 0);
        }
        else {
            *ptr++ = cmn_reg(31, reg_dl, LSL, 0);
        }
#else
        if (0 == (opcode2 & (1 << 10)))
            *ptr++ = cmp_immed(reg_dl, 0);
#endif
        uint8_t old_mask = update_mask & SR_V;
        ptr = EMIT_GetNZ00(ptr, cc, &update_mask);
        update_mask |= old_mask;

        if (update_mask & SR_Z) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
        }
        if (update_mask & SR_N) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
        }
        if ((update_mask & SR_V) && 0 == (opcode2 & (1 << 10))) {
            ptr = EMIT_ClearFlags(ptr, cc, SR_V);
#ifdef __aarch64__
            uint8_t tmp = RA_AllocARMRegister(&ptr);
            /* If signed multiply check higher 32bit against 0 or -1. For unsigned multiply upper 32 bit must be zero */
            if (opcode2 & (1 << 11)) {
                *ptr++ = cmn_reg(reg_dl, 31, LSL, 0);
                EMIT_csetm(&ptr, tmp, A64_CC_MI);
            } else {
                *ptr++ = mov_immed_u16(tmp, 0, 0);
            }
            *ptr++ = cmp64_reg(tmp, reg_dl, LSR, 32);
            RA_FreeARMRegister(&ptr, tmp);
#else
            if (opcode2 & (1 << 11))
            {
                uint8_t tmp = RA_AllocARMRegister(&ptr);
                /* Signed: V unless the top 32 bits are the sign-extension of the low 32. */
                *ptr++ = cmp_immed(reg_dl, 0);
                *ptr++ = mvn_cc_immed_u8(ARM_CC_MI, tmp, 0);
                *ptr++ = mov_cc_immed_u8(ARM_CC_PL, tmp, 0);
                *ptr++ = subs_reg(tmp, reg_dh, tmp, LSL, 0);
                RA_FreeARMRegister(&ptr, tmp);
            }
            else
            {
                /* Unsigned: V unless the top 32 bits are zero. */
                *ptr++ = cmp_immed(reg_dh, 0);
            }
#endif
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_V, ARM_CC_NE);
        }
    }

    RA_FreeARMRegister(&ptr, reg_dh);

    return ptr;
}

uint8_t M68K_GetSRMask_DivW(uint16_t *insn_stream);
uint32_t *EMIT_DIVS_W(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t update_mask = M68K_GetSRMask_DivW(*m68k_ptr - 1);
    uint8_t reg_a = RA_MapM68kRegister(&ptr, (opcode >> 9) & 7);
    uint8_t reg_q = 0xff;
    uint8_t reg_quot = RA_AllocARMRegister(&ptr);
    uint8_t reg_rem = RA_AllocARMRegister(&ptr);
    uint8_t ext_words = 0;
#ifndef __aarch64__
    uint32_t *div0_pop_loc = NULL;   /* exception-closure: patched pop of the div0 exit */
#endif

    ptr = EMIT_LoadFromEffectiveAddress(ptr, 2, &reg_q, opcode & 0x3f, *m68k_ptr, &ext_words, 0, NULL);
    ptr = EMIT_FlushPC(ptr);
    RA_GetCC(&ptr);

#ifdef __aarch64__
    *ptr++ = ands_immed(31, reg_q, 16, 0);
    uint32_t *tmp_ptr = ptr;
    *ptr++ = b_cc(A64_CC_NE, 2);

    if (1)
    {
        /*
            This is a point of no return. Issue division by zero exception here
        */
        *ptr++ = add_immed(REG_PC, REG_PC, 2 * (ext_words + 1));

        ptr = EMIT_Exception(ptr, VECTOR_DIVIDE_BY_ZERO, 2, (uint32_t)(intptr_t)(*m68k_ptr - 1));

        RA_StoreDirtyFPURegs(&ptr);
        RA_StoreDirtyOMMXRegs(&ptr);
        RA_StoreDirtyM68kRegs(&ptr);

        RA_StoreCC(&ptr);
        RA_StoreFPCR(&ptr);
        RA_StoreFPSR(&ptr);
        
#if EMU68_INSN_COUNTER
        extern uint32_t insn_count;
        uint8_t ctx_free = 0;
        uint8_t ctx = RA_TryCTX(&ptr);
        uint8_t tmp = RA_AllocARMRegister(&ptr);
        if (ctx == 0xff)
        {
            ctx = RA_AllocARMRegister(&ptr);
            *ptr++ = mrs(ctx, 3, 3, 13, 0, 3);
            ctx_free = 1;
        }
        EMIT_ldr64_offset(&ptr, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
        EMIT_add64_immed(&ptr, tmp, tmp, insn_count & 0xfff);
        if (insn_count & 0xfff000)
            *ptr++ = adds64_immed_lsl12(tmp, tmp, insn_count >> 12);
        EMIT_str64_offset(&ptr, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));

        RA_FreeARMRegister(&ptr, tmp);
        if (ctx_free)
            RA_FreeARMRegister(&ptr, ctx);
#endif
        /* Return here */
        *ptr++ = bx_lr();
    }
    /* Update branch to the continuation */
    *tmp_ptr = b_cc(A64_CC_NE, ptr - tmp_ptr);
#else
    {
        struct RA_Snapshot ra_snap;
        uint8_t z16 = RA_AllocARMRegister(&ptr);
        *ptr++ = sxth(z16, reg_q);
        *ptr++ = cmp_immed(z16, 0);
        RA_FreeARMRegister(&ptr, z16);
        RA_SaveState(&ra_snap);
        uint32_t *div0_skip = ptr;
        *ptr++ = b_cc(ARM_CC_NE, 2);   /* placeholder; back-patched to skip exception block */
        /* Division by zero: raise M68k VECTOR_DIVIDE_BY_ZERO exception then return */
        *ptr++ = add_immed(REG_PC, REG_PC, 2 * (ext_words + 1));
        if (update_mask & SR_C) { uint8_t _cc_div0 = RA_ModifyCC(&ptr); ptr = EMIT_ClearFlags(ptr, _cc_div0, SR_C); }
        ptr = EMIT_Exception(ptr, VECTOR_DIVIDE_BY_ZERO, 2, (uint32_t)(intptr_t)(*m68k_ptr - 1));
        ptr = EMIT_ExceptionUnitExit(ptr, &div0_pop_loc);
        /* Back-patch the NE branch to skip over the exception block */
        *div0_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - div0_skip));
        RA_RestoreState(&ra_snap);
    }
#endif


#ifdef __aarch64__
    *ptr++ = sxth(reg_rem, reg_q);
    *ptr++ = sdiv(reg_quot, reg_a, reg_rem);
    *ptr++ = msub(reg_rem, reg_a, reg_quot, reg_rem);
#else
    if (Features.ARM_SUPPORTS_DIV)
    {
        /* Sign extend divisor from 16-bit to 32-bit */
        *ptr++ = sxth(reg_rem, reg_q);
        *ptr++ = sdiv(reg_quot, reg_a, reg_rem);
        *ptr++ = mls(reg_rem, reg_a, reg_quot, reg_rem);
    }
    else
    {
        {
            uint8_t d_n = RA_AllocFPURegister(&ptr);
            uint8_t d_d = RA_AllocFPURegister(&ptr);

            *ptr++ = sxth(reg_rem, reg_q);
            *ptr++ = fmsr(d_n * 2, reg_a);
            *ptr++ = fsitod(d_n, d_n * 2);          /* (double)(int32)dividend  - exact */
            *ptr++ = fmsr(d_d * 2, reg_rem);
            *ptr++ = fsitod(d_d, d_d * 2);          /* (double)(int32)divisor   - exact */
            *ptr++ = fdivd(d_n, d_n, d_d);
            *ptr++ = ftosidrz(d_n * 2, d_n);        /* truncate toward zero, as the 68k does */
            *ptr++ = fmrs(reg_quot, d_n * 2);
            *ptr++ = mls(reg_rem, reg_a, reg_quot, reg_rem);

            RA_FreeFPURegister(&ptr, d_d);
            RA_FreeFPURegister(&ptr, d_n);
        }
    }
#endif

#ifdef __aarch64__
    uint8_t tmp = RA_AllocARMRegister(&ptr);

    *ptr++ = sxth(tmp, reg_quot);
    *ptr++ = cmp_reg(tmp, reg_quot, LSL, 0);

    RA_FreeARMRegister(&ptr, tmp);
#else
    /* Overflow check: EQ if quotient fits in signed 16-bit range, NE if overflow */
    uint8_t tmp = RA_AllocARMRegister(&ptr);
    *ptr++ = sxth(tmp, reg_quot);
    *ptr++ = cmp_reg(tmp, reg_quot, LSL, 0);    /* EQ if sxth==quot (in range) */
    RA_FreeARMRegister(&ptr, tmp);
#endif

    (*m68k_ptr) += ext_words;

    RA_SetDirtyM68kRegister(&ptr, (opcode >> 9) & 7);

    /* if temporary register was 0 the division was successful, otherwise overflow occured! */
    if (update_mask)
    {
        uint8_t cc = RA_ModifyCC(&ptr);

        ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_V | SR_C));

        if (update_mask & SR_V) {
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_V, ARM_CC_NE);
        }
        if (update_mask & (SR_Z | SR_N))
        {
#ifdef __aarch64__
            ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_Z | SR_N));
            ptr = emit_flags_nz_from_shifted(ptr, reg_quot, 16);
            ptr = EMIT_GetNZxx(ptr, cc, &update_mask);
            if (update_mask & SR_Z) {
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
            }
            if (update_mask & SR_N) {
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
            }
#else
            {
                uint32_t *nz_skip = ptr;
                *ptr++ = b_cc(ARM_CC_NE, 2);     /* overflow -> N/Z KEPT; back-patched */
                ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_Z | SR_N));
                *ptr++ = cmp_immed(reg_quot, 0);
                ptr = EMIT_GetNZxx(ptr, cc, &update_mask);
                if (update_mask & SR_Z) {
                    ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
                }
                if (update_mask & SR_N) {
                    ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
                }
                *nz_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - nz_skip));
            }
#endif
        }
    }

#ifdef __aarch64__
    if (update_mask & SR_V) {
        uint8_t cc = RA_GetCC(&ptr);
        EMIT_tbnz(&ptr, cc, SRB_V, 3);
    }
    else {
        *ptr++ = b_cc(A64_CC_NE, 3);
    }
    /* AArch64: writeback (conditionally skipped by branch above on overflow) */
    *ptr++ = mov_reg(reg_a, reg_quot);
    *ptr++ = bfi(reg_a, reg_rem, 16, 16);
#else
    /* ARMv7: on overflow leave destination unchanged (M68k spec); skip writeback if V set */
    if (update_mask & SR_V) {
        uint8_t cc = RA_GetCC(&ptr);
        *ptr++ = _tst_val(cc, SR_V);             /* TST cc, #SR_V */
        uint32_t *ovf_skip = ptr;
        *ptr++ = b_cc(ARM_CC_NE, 2);             /* placeholder: skip writeback if V set */
        *ptr++ = mov_reg(reg_a, reg_quot);
        *ptr++ = bfi(reg_a, reg_rem, 16, 16);
        *ovf_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - ovf_skip));  /* back-patch */
    } else {
        /* V not tracked: unconditional writeback */
        *ptr++ = mov_reg(reg_a, reg_quot);
        *ptr++ = bfi(reg_a, reg_rem, 16, 16);
    }
#endif

    /* Advance PC */
    ptr = EMIT_AdvancePC(ptr, 2 * (ext_words + 1));

    RA_FreeARMRegister(&ptr, reg_a);
    RA_FreeARMRegister(&ptr, reg_q);
    RA_FreeARMRegister(&ptr, reg_quot);
    RA_FreeARMRegister(&ptr, reg_rem);

#ifndef __aarch64__
    ptr = EMIT_ExceptionUnitExitTail(ptr, div0_pop_loc);
    if (!Features.ARM_SUPPORTS_DIV)
        *ptr++ = INSN_TO_LE(0xfffffff0);
#endif

    return ptr;
}

uint32_t *EMIT_DIVU_W(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t update_mask = M68K_GetSRMask_DivW(*m68k_ptr - 1);
    uint8_t reg_a = RA_MapM68kRegister(&ptr, (opcode >> 9) & 7);
    uint8_t reg_q = 0xff;
    uint8_t reg_quot = RA_AllocARMRegister(&ptr);
    uint8_t reg_rem = RA_AllocARMRegister(&ptr);
    uint8_t ext_words = 0;
#ifndef __aarch64__
    uint32_t *div0_pop_loc = NULL;   /* exception-closure: patched pop of the div0 exit */
#endif

    ptr = EMIT_LoadFromEffectiveAddress(ptr, 2, &reg_q, opcode & 0x3f, *m68k_ptr, &ext_words, 0, NULL);
    ptr = EMIT_FlushPC(ptr);
    RA_GetCC(&ptr);

#ifdef __aarch64__
    *ptr++ = ands_immed(31, reg_q, 16, 0);
    uint32_t *tmp_ptr = ptr;
    *ptr++ = b_cc(A64_CC_NE, 2);

    if (1)
    {
        /*
            This is a point of no return. Issue division by zero exception here
        */
        *ptr++ = add_immed(REG_PC, REG_PC, 2 * (ext_words + 1));

        ptr = EMIT_Exception(ptr, VECTOR_DIVIDE_BY_ZERO, 2, (uint32_t)(intptr_t)(*m68k_ptr - 1));

        RA_StoreDirtyFPURegs(&ptr);
        RA_StoreDirtyOMMXRegs(&ptr);
        RA_StoreDirtyM68kRegs(&ptr);

        RA_StoreCC(&ptr);
        RA_StoreFPCR(&ptr);
        RA_StoreFPSR(&ptr);
        
#if EMU68_INSN_COUNTER
        extern uint32_t insn_count;
        uint8_t ctx_free = 0;
        uint8_t ctx = RA_TryCTX(&ptr);
        uint8_t tmp = RA_AllocARMRegister(&ptr);
        if (ctx == 0xff)
        {
            ctx = RA_AllocARMRegister(&ptr);
            *ptr++ = mrs(ctx, 3, 3, 13, 0, 3);
            ctx_free = 1;
        }
        EMIT_ldr64_offset(&ptr, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
        EMIT_add64_immed(&ptr, tmp, tmp, insn_count & 0xfff);
        if (insn_count & 0xfff000)
            *ptr++ = adds64_immed_lsl12(tmp, tmp, insn_count >> 12);
        EMIT_str64_offset(&ptr, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));

        RA_FreeARMRegister(&ptr, tmp);
        if (ctx_free)
            RA_FreeARMRegister(&ptr, ctx);
#endif
        /* Return here */
        *ptr++ = bx_lr();
    }
    /* Update branch to the continuation */
    *tmp_ptr = b_cc(A64_CC_NE, ptr - tmp_ptr);
#else
    {
        struct RA_Snapshot ra_snap;
        uint8_t z16 = RA_AllocARMRegister(&ptr);
        *ptr++ = uxth(z16, reg_q);
        *ptr++ = cmp_immed(z16, 0);
        RA_FreeARMRegister(&ptr, z16);
        RA_SaveState(&ra_snap);
        uint32_t *div0_skip = ptr;
        *ptr++ = b_cc(ARM_CC_NE, 2);   /* placeholder; back-patched to skip exception block */
        /* Division by zero: raise M68k VECTOR_DIVIDE_BY_ZERO exception then return */
        *ptr++ = add_immed(REG_PC, REG_PC, 2 * (ext_words + 1));
        if (update_mask & SR_C) { uint8_t _cc_div0 = RA_ModifyCC(&ptr); ptr = EMIT_ClearFlags(ptr, _cc_div0, SR_C); }
        ptr = EMIT_Exception(ptr, VECTOR_DIVIDE_BY_ZERO, 2, (uint32_t)(intptr_t)(*m68k_ptr - 1));
        ptr = EMIT_ExceptionUnitExit(ptr, &div0_pop_loc);
        /* Back-patch the NE branch to skip over the exception block */
        *div0_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - div0_skip));
        RA_RestoreState(&ra_snap);
    }
#endif

#ifdef __aarch64__
    *ptr++ = uxth(reg_rem, reg_q);
    *ptr++ = udiv(reg_quot, reg_a, reg_rem);
    *ptr++ = msub(reg_rem, reg_a, reg_quot, reg_rem);
#else
    if (Features.ARM_SUPPORTS_DIV)
    {
        /* Sign extend divisor from 16-bit to 32-bit */
        *ptr++ = uxth(reg_rem, reg_q);
        *ptr++ = udiv(reg_quot, reg_a, reg_rem);
        *ptr++ = mls(reg_rem, reg_a, reg_quot, reg_rem);
    }
    else
    {
        {
            uint8_t d_n = RA_AllocFPURegister(&ptr);
            uint8_t d_d = RA_AllocFPURegister(&ptr);

            *ptr++ = uxth(reg_rem, reg_q);
            *ptr++ = fmsr(d_n * 2, reg_a);
            *ptr++ = fuitod(d_n, d_n * 2);          /* (double)(uint32)dividend - exact */
            *ptr++ = fmsr(d_d * 2, reg_rem);
            *ptr++ = fuitod(d_d, d_d * 2);
            *ptr++ = fdivd(d_n, d_n, d_d);
            *ptr++ = ftouidrz(d_n * 2, d_n);
            *ptr++ = fmrs(reg_quot, d_n * 2);
            *ptr++ = mls(reg_rem, reg_a, reg_quot, reg_rem);

            RA_FreeFPURegister(&ptr, d_d);
            RA_FreeFPURegister(&ptr, d_n);
        }
    }
#endif

#ifdef __aarch64__
    uint8_t tmp = RA_AllocARMRegister(&ptr);

    *ptr++ = uxth(tmp, reg_quot);
    *ptr++ = cmp_reg(tmp, reg_quot, LSL, 0);

    RA_FreeARMRegister(&ptr, tmp);
#else
    /* Overflow check: EQ if quotient fits in unsigned 16-bit range, NE if overflow */
    uint8_t tmp = RA_AllocARMRegister(&ptr);
    *ptr++ = uxth(tmp, reg_quot);
    *ptr++ = cmp_reg(tmp, reg_quot, LSL, 0);    /* EQ if uxth==quot (in range) */
    RA_FreeARMRegister(&ptr, tmp);
#endif

    (*m68k_ptr) += ext_words;

    RA_SetDirtyM68kRegister(&ptr, (opcode >> 9) & 7);

    /* if temporary register was 0 the division was successful, otherwise overflow occured! */
    if (update_mask)
    {
        uint8_t cc = RA_ModifyCC(&ptr);

        ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_V | SR_C));

        if (update_mask & SR_V) {
            
            ptr = EMIT_SetFlagsConditional(ptr, cc, SR_V, ARM_CC_NE);
        }

        if (update_mask & (SR_Z | SR_N))
        {
#ifdef __aarch64__
            ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_Z | SR_N));
            ptr = emit_flags_nz_from_shifted(ptr, reg_quot, 16);
            ptr = EMIT_GetNZxx(ptr, cc, &update_mask);
            if (update_mask & SR_Z) {
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
            }
            if (update_mask & SR_N) {
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
            }
#else
            {
                uint8_t nzt = RA_AllocARMRegister(&ptr);
                uint32_t *nz_skip = ptr;
                *ptr++ = b_cc(ARM_CC_NE, 2);     /* overflow -> N/Z KEPT (68040); back-patched */
                ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_Z | SR_N));   /* success path: N/Z start clean */
                *ptr++ = sxth(nzt, reg_quot);
                *ptr++ = cmp_immed(nzt, 0);
                ptr = EMIT_GetNZxx(ptr, cc, &update_mask);
                if (update_mask & SR_Z) {
                    ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
                }
                if (update_mask & SR_N) {
                    ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
                }
                *nz_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - nz_skip));
                RA_FreeARMRegister(&ptr, nzt);
            }
#endif
        }
    }

#ifdef __aarch64__
    if (update_mask & SR_V) {
        uint8_t cc = RA_GetCC(&ptr);
        EMIT_tbnz(&ptr, cc, SRB_V, 3);
    }
    else {
        *ptr++ = b_cc(A64_CC_NE, 3);
    }
    /* AArch64: writeback (conditionally skipped by branch above on overflow) */
    *ptr++ = mov_reg(reg_a, reg_quot);
    *ptr++ = bfi(reg_a, reg_rem, 16, 16);
#else
    /* ARMv7: on overflow leave destination unchanged (M68k spec); skip writeback if V set */
    if (update_mask & SR_V) {
        uint8_t cc = RA_GetCC(&ptr);
        *ptr++ = _tst_val(cc, SR_V);             /* TST cc, #SR_V */
        uint32_t *ovf_skip = ptr;
        *ptr++ = b_cc(ARM_CC_NE, 2);             /* placeholder: skip writeback if V set */
        *ptr++ = mov_reg(reg_a, reg_quot);
        *ptr++ = bfi(reg_a, reg_rem, 16, 16);
        *ovf_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - ovf_skip));  /* back-patch */
    } else {
        /* V not tracked: unconditional writeback */
        *ptr++ = mov_reg(reg_a, reg_quot);
        *ptr++ = bfi(reg_a, reg_rem, 16, 16);
    }
#endif

    /* Advance PC */
    ptr = EMIT_AdvancePC(ptr, 2 * (ext_words + 1));

    RA_FreeARMRegister(&ptr, reg_a);
    RA_FreeARMRegister(&ptr, reg_q);
    RA_FreeARMRegister(&ptr, reg_quot);
    RA_FreeARMRegister(&ptr, reg_rem);

#ifndef __aarch64__
    ptr = EMIT_ExceptionUnitExitTail(ptr, div0_pop_loc);
    if (!Features.ARM_SUPPORTS_DIV)
        *ptr++ = INSN_TO_LE(0xfffffff0);
#endif

    return ptr;
}

uint32_t *EMIT_DIVUS_L(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    uint8_t update_mask = M68K_GetSRMask(*m68k_ptr - 1);
    uint16_t opcode2 = BE16((*m68k_ptr)[0]);
    RA_FlushOMMXRegs(&ptr);
    uint8_t sig = (opcode2 & (1 << 11)) != 0;
    uint8_t div64 = (opcode2 & (1 << 10)) != 0;
    uint8_t reg_q = 0xff;
    uint8_t reg_dq = RA_MapM68kRegister(&ptr, (opcode2 >> 12) & 7);
    uint8_t reg_dr = RA_MapM68kRegister(&ptr, opcode2 & 7);
    uint8_t ext_words = 1;
#ifndef __aarch64__
    uint32_t *div0_pop_loc = NULL;   /* exception-closure: patched pop of the div0 exit */
#endif

    // Load divisor
    ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &reg_q, opcode & 0x3f, *m68k_ptr, &ext_words, 1, NULL);
    ptr = EMIT_FlushPC(ptr);
    RA_GetCC(&ptr);

    // Check if division by 0
#ifdef __aarch64__
    uint32_t *tmp_ptr = ptr;
    EMIT_cbnz(&ptr, reg_q, 2);

    if (1)
    {
        /*
            This is a point of no return. Issue division by zero exception here
        */
        *ptr++ = add_immed(REG_PC, REG_PC, 2 * (ext_words + 1));

        ptr = EMIT_Exception(ptr, VECTOR_DIVIDE_BY_ZERO, 2, (uint32_t)(intptr_t)(*m68k_ptr - 1));

        RA_StoreDirtyFPURegs(&ptr);
        RA_StoreDirtyOMMXRegs(&ptr);
        RA_StoreDirtyM68kRegs(&ptr);

        RA_StoreCC(&ptr);
        RA_StoreFPCR(&ptr);
        RA_StoreFPSR(&ptr);
        
#if EMU68_INSN_COUNTER
        extern uint32_t insn_count;
        uint8_t ctx_free = 0;
        uint8_t ctx = RA_TryCTX(&ptr);
        uint8_t tmp = RA_AllocARMRegister(&ptr);
        if (ctx == 0xff)
        {
            ctx = RA_AllocARMRegister(&ptr);
            *ptr++ = mrs(ctx, 3, 3, 13, 0, 3);
            ctx_free = 1;
        }
        EMIT_ldr64_offset(&ptr, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
        EMIT_add64_immed(&ptr, tmp, tmp, insn_count & 0xfff);
        if (insn_count & 0xfff000)
            *ptr++ = adds64_immed_lsl12(tmp, tmp, insn_count >> 12);
        EMIT_str64_offset(&ptr, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));

        RA_FreeARMRegister(&ptr, tmp);
        if (ctx_free)
            RA_FreeARMRegister(&ptr, ctx);
#endif
        /* Return here */
        *ptr++ = bx_lr();
    }
    /* Update branch to the continuation */
    EMIT_cbnz(&(uint32_t*){tmp_ptr}, reg_q, ptr - tmp_ptr) /*back-patch off runtime-verify*/;
#else
    {
        struct RA_Snapshot ra_snap;
        RA_SaveState(&ra_snap);
        *ptr++ = cmp_immed(reg_q, 0);
        uint32_t *div0_skip = ptr;
        *ptr++ = b_cc(ARM_CC_NE, 2);   /* placeholder; back-patched to skip exception block */
        /* Division by zero: raise M68k VECTOR_DIVIDE_BY_ZERO exception then return */
        *ptr++ = add_immed(REG_PC, REG_PC, 2 * (ext_words + 1));
        if (update_mask & SR_C) { uint8_t _cc_div0 = RA_ModifyCC(&ptr); ptr = EMIT_ClearFlags(ptr, _cc_div0, SR_C); }
        ptr = EMIT_Exception(ptr, VECTOR_DIVIDE_BY_ZERO, 2, (uint32_t)(intptr_t)(*m68k_ptr - 1));
        ptr = EMIT_ExceptionUnitExit(ptr, &div0_pop_loc);
        /* Back-patch the NE branch to skip over the exception block */
        *div0_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - div0_skip));
        RA_RestoreState(&ra_snap);
    }
#endif


#ifdef __aarch64__
    if (div64)
    {
        extern void emu68_ldivmod_lepair(void);   /* signed   64/64 -> quo r0:r1, rem r2:r3 (LE pairs) */
        extern void emu68_uldivmod_lepair(void);  /* unsigned 64/64 -> quo r0:r1, rem r2:r3 (LE pairs) */

        /* build dividend pair r0:r1 = (reg_dq low, reg_dr high), divisor pair r2:r3 */
        *ptr++ = mov_reg(0, reg_dq);
        *ptr++ = mov_reg(1, reg_dr);
        *ptr++ = mov_reg(2, reg_q);
        if (sig)
            *ptr++ = asr_immed(3, reg_q, 31);
        else
            *ptr++ = mov_immed_u8(3, 0);

        /* caller-saved spill around the BLX; result regs r0-r3 left OUT of the mask */
        uint32_t __div_save = (RA_GetTempAllocMask() | REG_PROTECT) & ~0x000fu;
        ptr = EMIT_SaveRegFrame(ptr, __div_save);
        uint8_t __div_hreg = RA_AllocARMRegister(&ptr);
        if (sig) {
            *ptr++ = movw_immed_u16(__div_hreg, (uint16_t)((uintptr_t)&emu68_ldivmod_lepair & 0xffff));
            *ptr++ = movt_immed_u16(__div_hreg, (uint16_t)(((uintptr_t)&emu68_ldivmod_lepair >> 16) & 0xffff));
        } else {
            *ptr++ = movw_immed_u16(__div_hreg, (uint16_t)((uintptr_t)&emu68_uldivmod_lepair & 0xffff));
            *ptr++ = movt_immed_u16(__div_hreg, (uint16_t)(((uintptr_t)&emu68_uldivmod_lepair >> 16) & 0xffff));
        }
        *ptr++ = blx_cc_reg(ARM_CC_AL, __div_hreg);   /* quotient->r0:r1, remainder->r2:r3 */
        ptr = EMIT_RestoreRegFrame(ptr, __div_save);  /* POP does NOT clobber r0-r3 */
        RA_FreeARMRegister(&ptr, __div_hreg);

        /* 32-bit overflow check (quotient must fit signed/unsigned 32) */
        uint8_t __div_sext = RA_AllocARMRegister(&ptr);
        if (sig) {
            *ptr++ = asr_immed(__div_sext, 0, 31);
            *ptr++ = cmp_reg(__div_sext, 1, LSL, 0);  /* overflow iff sext(q_lo) != q_hi (A64-shadow cmp_reg) */
        } else {
            (void)__div_sext;
            *ptr++ = cmp_immed(1, 0);                 /* overflow iff q_hi != 0 */
        }
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_NE, 2);                  /* skip writeback on overflow (Dq,Dr unchanged) */
        *ptr++ = mov_reg(reg_dq, 0);
        if (reg_dr != reg_dq)
            *ptr++ = mov_reg(reg_dr, 2);
        *tmp_ptr = b_cc(ARM_CC_NE, ptr - tmp_ptr);
        RA_FreeARMRegister(&ptr, __div_sext);
    }
    else
    {
        if (reg_dr == reg_dq)
        {
            if (sig)
                *ptr++ = sdiv(reg_dq, reg_dq, reg_q);
            else
                *ptr++ = udiv(reg_dq, reg_dq, reg_q);
        }
        else
        {
            uint8_t tmp = RA_AllocARMRegister(&ptr);

            if (sig)
                *ptr++ = sdiv(tmp, reg_dq, reg_q);
            else
                *ptr++ = udiv(tmp, reg_dq, reg_q);

            *ptr++ = msub(reg_dr, reg_dq, tmp, reg_q);
            *ptr++ = mov_reg(reg_dq, tmp);

            RA_FreeARMRegister(&ptr, tmp);
        }
    }
#else
    if (Features.ARM_SUPPORTS_DIV)
    {
        if (div64)
        {
            /* Div64/32 ->32:32 routine based on article: https://gmplib.org/~tege/division-paper.pdf */
            /* Warning - the routine exhausts all registers from allocator! */
            uint8_t tmp_r0 = reg_dr; /* High 32 bits of dividend */
            uint8_t tmp_r1 = reg_dq; /* Low 32 bits of dividend */
            uint8_t tmp_r2 = reg_q;  /* divisor */
            uint8_t tmp_r3 = RA_AllocARMRegister(&ptr);
            uint8_t tmp_r4 = RA_AllocARMRegister(&ptr);
            uint8_t tmp_r5 = RA_AllocARMRegister(&ptr);
            uint8_t tmp_r6 = RA_AllocARMRegister(&ptr);
            uint8_t tmp_r7 = RA_AllocARMRegister(&ptr);
            uint8_t tmp_r8 = RA_AllocARMRegister(&ptr);
            uint8_t tmp_r9 = RA_AllocARMRegister(&ptr);

            kprintf("DIV%c_L 64/32->32:32\n", sig ? 'S':'U');

            if (sig) {

            }
            else {
                *ptr++ = clz(tmp_r3, tmp_r2);
                *ptr++ = cmp_immed(tmp_r3, 0);
                *ptr++ = mov_cc_reg(ARM_CC_EQ, tmp_r6, tmp_r2);
                *ptr++ = b_cc(ARM_CC_EQ, 9);
                *ptr++ = mov_reg(tmp_r4, tmp_r0);
                *ptr++ = mov_reg(tmp_r5, tmp_r1);
                *ptr++ = sub_immed(tmp_r1, tmp_r3, 32);
                *ptr++ = rsb_immed(tmp_r0, tmp_r3, 32);
                *ptr++ = lsl_reg(tmp_r4, tmp_r4, tmp_r3);
                *ptr++ = orr_reg_lsl_reg(tmp_r4, tmp_r4, tmp_r5, tmp_r1);
                *ptr++ = orr_reg_lsr_reg(tmp_r4, tmp_r4, tmp_r5, tmp_r0);
                *ptr++ = lsl_reg(tmp_r1, tmp_r5, tmp_r3);
                *ptr++ = mov_reg(tmp_r0, tmp_r4);
                *ptr++ = lsl_reg(tmp_r6, tmp_r2, tmp_r3);
                *ptr++ = movw_immed_u16(tmp_r3, 0xc200);
                *ptr++ = _and_val(tmp_r5, tmp_r6, 1);
                *ptr++ = lsr_immed(tmp_r4, tmp_r6, 22);
                *ptr++ = movt_immed_u16(tmp_r3, 0x00ff);
                *ptr++ = lsr_immed(tmp_r8, tmp_r6, 11);
                *ptr++ = udiv(tmp_r3, tmp_r3, tmp_r4);
                *ptr++ = mul(tmp_r4, tmp_r3, tmp_r3);
                *ptr++ = lsl_immed(tmp_r3, tmp_r3, 4);
                *ptr++ = add_immed(tmp_r8, tmp_r8, 1);
                *ptr++ = umull_cc(ARM_CC_AL, tmp_r9, tmp_r8, tmp_r8, tmp_r4);
                *ptr++ = sub_immed(tmp_r4, tmp_r3, 1);
                *ptr++ = rsb_immed(tmp_r3, tmp_r3, 1);
                *ptr++ = sub_reg(tmp_r7, tmp_r4, tmp_r8, LSL, 0);
                *ptr++ = mov_reg(tmp_r9, tmp_r1);
                *ptr++ = add_reg(tmp_r3, tmp_r3, tmp_r8, LSL, 0);
                *ptr++ = lsr_immed(tmp_r4, tmp_r7, 1);
                *ptr++ = mul(tmp_r4, tmp_r4, tmp_r5);
                *ptr++ = lsr_immed(tmp_r5, tmp_r6, 1);
                *ptr++ = mla(tmp_r4, tmp_r4, tmp_r5, tmp_r3);
                *ptr++ = lsl_immed(tmp_r3, tmp_r7, 15);
                *ptr++ = umull_cc(ARM_CC_AL, tmp_r5, tmp_r4, tmp_r4, tmp_r7);
                *ptr++ = mov_immed_u8(tmp_r5, 0);
                *ptr++ = add_reg_lsr_imm(tmp_r4, tmp_r3, tmp_r4, 1);
                *ptr++ = umlal(tmp_r1, tmp_r0, tmp_r0, tmp_r4);
                *ptr++ = mov_reg(tmp_r4, tmp_r0);
                *ptr++ = add_immed(tmp_r0, tmp_r4, 1);
                *ptr++ = mls(tmp_r3, tmp_r9, tmp_r6, tmp_r0);
                *ptr++ = cmp_reg(tmp_r3, tmp_r1, LSL, 0);
                *ptr++ = add_cc_reg(ARM_CC_HI, tmp_r3, tmp_r3, tmp_r6, 0);
                *ptr++ = mov_cc_reg(ARM_CC_HI, tmp_r0, tmp_r4);
                *ptr++ = cmp_reg(tmp_r3, tmp_r6, LSL, 0);
                *ptr++ = sub_cc_reg(ARM_CC_CS, tmp_r3, tmp_r3, tmp_r6, 0);
                *ptr++ = add_cc_immed(ARM_CC_CS, tmp_r0, tmp_r0, 1);
                *ptr++ = orr_reg(tmp_r7, tmp_r5, tmp_r0, LSL, 0);
                *ptr++ = mov_reg(tmp_r0, tmp_r3);
                *ptr++ = mov_reg(tmp_r1, tmp_r7);
            }

            RA_FreeARMRegister(&ptr, tmp_r3);
            RA_FreeARMRegister(&ptr, tmp_r4);
            RA_FreeARMRegister(&ptr, tmp_r5);
            RA_FreeARMRegister(&ptr, tmp_r6);
            RA_FreeARMRegister(&ptr, tmp_r7);
            RA_FreeARMRegister(&ptr, tmp_r8);
            RA_FreeARMRegister(&ptr, tmp_r9);
        }
        else
        {
            if (reg_dr == 0xff)
            {
                if (sig)
                    *ptr++ = sdiv(reg_dq, reg_dq, reg_q);
                else
                    *ptr++ = udiv(reg_dq, reg_dq, reg_q);
            }
            else
            {
                uint8_t tmp = RA_AllocARMRegister(&ptr);

                if (sig)
                    *ptr++ = sdiv(tmp, reg_dq, reg_q);
                else
                    *ptr++ = udiv(tmp, reg_dq, reg_q);

                *ptr++ = mls(reg_dr, reg_dq, tmp, reg_q);
                *ptr++ = mov_reg(reg_dq, tmp);

                RA_FreeARMRegister(&ptr, tmp);
            }
        }
    }
    else
    {
        uint32_t soft_save_mask = ((1 << reg_q) | 0x0f | (1 << 12) | (1 << 14))
                                   & ~((1 << reg_dr) | (1 << reg_dq));

        if (div64)
            *ptr++ = push(soft_save_mask);

        if (div64)
        {

            *ptr++ = push(1 << reg_dq);   /* [sp+4] = original Dq */
            *ptr++ = push(1 << reg_dr);   /* [sp+0] = original Dr */

            /* Compute d_hi into r12: 0 (unsigned) or sign-ext(divisor) (signed) */
            if (sig)
                *ptr++ = asr_immed(12, reg_q, 31);
            else
                *ptr++ = mov_immed_u8(12, 0);

            /* Push n_lo, n_hi, d_lo, d_hi in that order (LIFO: d_hi on top) */
            *ptr++ = push(1 << reg_dq);   /* push n_lo */
            *ptr++ = push(1 << reg_dr);   /* push n_hi */
            *ptr++ = push(1 << reg_q);    /* push d_lo */
            *ptr++ = push(1 << 12);       /* push d_hi */

            /* Pop into r3, r2, r1, r0 (d_hi->r3, d_lo->r2, n_hi->r1, n_lo->r0) */
            *ptr++ = pop(1 << 3);
            *ptr++ = pop(1 << 2);
            *ptr++ = pop(1 << 1);
            *ptr++ = pop(1 << 0);

            /* Trampoline: ldr r12,[pc,#4]; blx r12; b_cc(AL,0)=skip .word; .word &fn */
            *ptr++ = ldr_offset(15, 12, 4);
            *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
            *ptr++ = b_cc(ARM_CC_AL, 2);   /* superinc ARM.h conv: N=2 skips .word */
            if (sig)
                *ptr++ = (uint32_t)&emu68_ldivmod_lepair;
            else
                *ptr++ = (uint32_t)&emu68_uldivmod_lepair;

            /* Overflow check: r0=q_lo, r1=q_hi, r2=r_lo, r3=r_hi */
            if (sig) {
                *ptr++ = asr_immed(12, 0, 31);
                *ptr++ = cmp_reg(12, 1, LSL, 0);    /* NE if r12 != r1 => overflow */
            } else {
                *ptr++ = cmp_immed(1, 0);           /* NE if q_hi != 0 => overflow */
            }

            /* Branch on overflow (NE) to the restore path */
            uint32_t *ovf_branch = ptr;
            *ptr++ = b_cc(ARM_CC_NE, 2);            /* placeholder; back-patched below */

            *ptr++ = push(1 << 0);                  /* push r0 = q_lo */
            *ptr++ = push(1 << 2);                  /* push r2 = r_lo */
            *ptr++ = pop(1 << reg_dr);
            *ptr++ = pop(1 << reg_dq);
            *ptr++ = add_immed(13, 13, 8);          /* drop saved Dq/Dr (sp += 8) */

            /* Normal path: skip over the overflow restore */
            uint32_t *normal_done = ptr;
            *ptr++ = b_cc(ARM_CC_AL, 2);            /* placeholder; back-patched below */

            *ovf_branch = b_cc(ARM_CC_NE, (int32_t)(ptr - ovf_branch));
            *ptr++ = pop(1 << reg_dr);              /* restore Dr to pre-divide value */
            *ptr++ = pop(1 << reg_dq);              /* restore Dq to pre-divide value */

            /* Merge point: normal path branches here */
            *normal_done = b_cc(ARM_CC_AL, (int32_t)(ptr - normal_done));
        }
        else
        {
            uint8_t d_n = RA_AllocFPURegister(&ptr);
            uint8_t d_d = RA_AllocFPURegister(&ptr);
            uint8_t qt  = RA_AllocARMRegister(&ptr);
            uint32_t *b94_ovf = NULL;

            if (sig) {
                *ptr++ = cmn_immed(reg_q, 1);                 /* Z iff divisor == -1     */
                uint32_t *na = ptr;
                *ptr++ = b_cc(ARM_CC_NE, 2);                  /* not -1 -> divide        */
                *ptr++ = cmp_reg(reg_dq, reg_q, LSL, 31);     /* Z iff Dq == $80000000   */
                uint32_t *nb = ptr;
                *ptr++ = b_cc(ARM_CC_NE, 2);                  /* fits -> divide          */
                b94_ovf = ptr;
                *ptr++ = b_cc(ARM_CC_AL, 2);                  /* overflow: skip, Z=1     */
                *na = b_cc(ARM_CC_NE, (int32_t)(ptr - na));
                *nb = b_cc(ARM_CC_NE, (int32_t)(ptr - nb));
            }

            *ptr++ = fmsr(d_n * 2, reg_dq);
            *ptr++ = sig ? fsitod(d_n, d_n * 2) : fuitod(d_n, d_n * 2);
            *ptr++ = fmsr(d_d * 2, reg_q);
            *ptr++ = sig ? fsitod(d_d, d_d * 2) : fuitod(d_d, d_d * 2);
            *ptr++ = fdivd(d_n, d_n, d_d);
            *ptr++ = sig ? ftosidrz(d_n * 2, d_n) : ftouidrz(d_n * 2, d_n);
            *ptr++ = fmrs(qt, d_n * 2);

            if (reg_dr != 0xff && reg_dr != reg_dq)
                *ptr++ = mls(reg_dr, reg_dq, qt, reg_q);
            *ptr++ = mov_reg(reg_dq, qt);
            if (b94_ovf)
                *b94_ovf = b_cc(ARM_CC_AL, (int32_t)(ptr - b94_ovf));

            RA_FreeARMRegister(&ptr, qt);
            RA_FreeFPURegister(&ptr, d_d);
            RA_FreeFPURegister(&ptr, d_n);
        }

        if (div64)
            *ptr++ = pop(soft_save_mask);
    }
#endif

    (*m68k_ptr) += ext_words;

    /* Set Dq dirty */
    RA_SetDirtyM68kRegister(&ptr, (opcode2 >> 12) & 7);
    /* Set Dr dirty if it was used/changed */
    if (reg_dr != 0xff)
        RA_SetDirtyM68kRegister(&ptr, opcode2 & 7);

    if (update_mask)
    {
        uint8_t cc = RA_ModifyCC(&ptr);
        if (update_mask & SR_VC) {
            ptr = EMIT_ClearFlags(ptr, cc, SR_V | SR_C);
            if (div64) {
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_V, ARM_CC_NE);
            }
#ifndef __aarch64__
            else if (sig) {
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_V, ARM_CC_EQ);
            }
#endif
        }
        if (update_mask & (SR_Z | SR_N))
        {
#ifdef __aarch64__
            *ptr++ = cmn_reg(31, reg_dq, LSL, 0);
            ptr = EMIT_GetNZxx(ptr, cc, &update_mask);
            if (update_mask & SR_N)
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
            if (update_mask & SR_Z)
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
#else
            uint32_t *nz_skip = NULL; uint8_t nz_cc = ARM_CC_AL;
            if (div64)     { nz_cc = ARM_CC_NE; nz_skip = ptr; *ptr++ = b_cc(nz_cc, 2); }
            else if (sig)  { nz_cc = ARM_CC_EQ; nz_skip = ptr; *ptr++ = b_cc(nz_cc, 2); }
            ptr = EMIT_ClearFlags(ptr, cc, update_mask & (SR_Z | SR_N));   /* success path: start clean */
            *ptr++ = cmp_immed(reg_dq, 0);
            ptr = EMIT_GetNZxx(ptr, cc, &update_mask);
            if (update_mask & SR_N)
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_N, ARM_CC_MI);
            if (update_mask & SR_Z)
                ptr = EMIT_SetFlagsConditional(ptr, cc, SR_Z, ARM_CC_EQ);
            if (nz_skip) *nz_skip = b_cc(nz_cc, (int32_t)(ptr - nz_skip));
#endif
        }
    }

    /* Advance PC */
    ptr = EMIT_AdvancePC(ptr, 2 * (ext_words + 1));

    RA_FreeARMRegister(&ptr, reg_q);
    RA_FreeARMRegister(&ptr, reg_dq);
    if (reg_dr != 0xff)
        RA_FreeARMRegister(&ptr, reg_dr);

#ifndef __aarch64__
    ptr = EMIT_ExceptionUnitExitTail(ptr, div0_pop_loc);
    if (!Features.ARM_SUPPORTS_DIV)
        *ptr++ = INSN_TO_LE(0xfffffff0);
#endif

    return ptr;
}

uint32_t *EMIT_MUL_DIV(uint32_t *ptr, uint16_t opcode, uint16_t **m68k_ptr)
{
    (void)m68k_ptr;

    if ((opcode & 0xf1c0) == 0xc1c0)
    {
        ptr = EMIT_MULS_W(ptr, opcode, m68k_ptr);
    }
    if ((opcode & 0xf1c0) == 0xc0c0)
    {
        ptr = EMIT_MULU_W(ptr, opcode, m68k_ptr);
    }
    if ((opcode & 0xffc0) == 0x4c00)
    {
        ptr = EMIT_MULS_L(ptr, opcode, m68k_ptr);
    }
    if ((opcode & 0xffc0) == 0x4c40)
    {
        ptr = EMIT_DIVUS_L(ptr, opcode, m68k_ptr);
    }
    if ((opcode & 0xf1c0) == 0x81c0)
    {
        ptr = EMIT_DIVS_W(ptr, opcode, m68k_ptr);
    }
    if ((opcode & 0xf1c0) == 0x80c0)
    {
        ptr = EMIT_DIVU_W(ptr, opcode, m68k_ptr);
    }

    return ptr;
}
