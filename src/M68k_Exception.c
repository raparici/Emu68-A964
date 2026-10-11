#include "Emu68_ARM_emit.h"
#include <stdint.h>
#include <stdarg.h>
/*
    Copyright © 2020 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "config.h"

uint32_t *EMIT_Exception(uint32_t *ptr, uint16_t exception, uint8_t format, ...)
{

    va_list args;
    uint8_t ctx = RA_TryCTX(&ptr); //RA_GetCTX(&ptr);
    uint8_t sp = RA_MapM68kRegister(&ptr, 15);
    uint8_t vbr = RA_AllocARMRegister(&ptr);
    uint8_t cc = RA_ModifyCC(&ptr);
    int need_free_ctx = 0;
    
    if (ctx == 0xff) {
        need_free_ctx = 1;
        ctx = RA_GetCTX(&ptr);
    }

    va_start(args, format);

    RA_SetDirtyM68kRegister(&ptr, 15);

    /* Check if we are changing stack due to user->supervisor transition */
    EMIT_tbnz(&ptr, cc, SRB_S, 7);

    /* We were in user mode. Store A7 as USP */
    *ptr++ = str_offset(ctx, sp, __builtin_offsetof(struct M68KState, USP));

    /* Check if we need to load ISP or MSP */
    EMIT_tbnz(&ptr, cc, SRB_M, 3);

    /* Load ISP to A7 */
    *ptr++ = ldr_offset(ctx, sp, __builtin_offsetof(struct M68KState, ISP));
    *ptr++ = b(2);
    *ptr++ = ldr_offset(ctx, sp, __builtin_offsetof(struct M68KState, MSP));

#if   !defined(__aarch64__)
    if (format == 2 || format == 3)
    {
        uint32_t ea = va_arg(args, uint32_t);
        *ptr++ = movw_immed_u16(vbr, ea & 0xffff);
        *ptr++ = movt_immed_u16(vbr, ea >> 16);
        *ptr++ = str_offset_preindex(sp, vbr, -4);
    }
    else if (format == 4)
    {
        uint32_t ea = va_arg(args, uint32_t);
        *ptr++ = movw_immed_u16(vbr, ea & 0xffff);
        *ptr++ = movt_immed_u16(vbr, ea >> 16);
        *ptr++ = str_offset_preindex(sp, vbr, -4);

        ea = va_arg(args, uint32_t);
        *ptr++ = movw_immed_u16(vbr, ea & 0xffff);
        *ptr++ = movt_immed_u16(vbr, ea >> 16);
        *ptr++ = str_offset_preindex(sp, vbr, -4);
    }

    *ptr++ = movw_immed_u16(vbr, (uint16_t)((format << 12) | (exception & 0xfff)));
    *ptr++ = strh_offset_preindex(sp, vbr, -2);
    *ptr++ = str_offset_preindex(sp, REG_PC, -4);
#else
    if (format == 2 || format == 3)
    {
        /* Format 2 and 3, store Address / Effective address */
        uint32_t ea = va_arg(args, uint32_t);
        *ptr++ = movw_immed_u16(vbr, ea & 0xffff);
        if ((ea >> 16) != 0)
            *ptr++ = movt_immed_u16(vbr, ea >> 16);
        *ptr++ = str_offset_preindex(sp, vbr, -4);
    }
    else if (format == 4)
    {
        /* Format 2 and 3, store Effective address and address of faulting instruction */
        uint32_t ea = va_arg(args, uint32_t);
        *ptr++ = movw_immed_u16(vbr, ea & 0xffff);
        if ((ea >> 16) != 0)
            *ptr++ = movt_immed_u16(vbr, ea >> 16);
        *ptr++ = str_offset_preindex(sp, vbr, -4);

        ea = va_arg(args, uint32_t);
        *ptr++ = movw_immed_u16(vbr, ea & 0xffff);
        if ((ea >> 16) != 0)
            *ptr++ = movt_immed_u16(vbr, ea >> 16);
        *ptr++ = str_offset_preindex(sp, vbr, -4);
    }

    /* Store exception vector and type */
    *ptr++ = mov_immed_u16(vbr, (format << 12) | (exception & 0xfff), 0);
    *ptr++ = strh_offset_preindex(sp, vbr, -2);

    /* Store program counter */
    *ptr++ = str_offset_preindex(sp, REG_PC, -4);
#endif

    /* Store SR */
    *ptr++ = strh_offset_preindex(sp, cc, -2);

    /* Clear trace flags, set supervisor */
    *ptr++ = bic_immed(cc, cc, 2, 32 - SRB_T0);
    *ptr++ = orr_immed(cc, cc, 1, 32 - SRB_S);

#ifdef __aarch64__
    *ptr++ = mov_immed_u16(vbr, 1, 0);
#else
    *ptr++ = movw_immed_u16(vbr, 1);
#endif
    *ptr++ = str_offset(ctx, vbr, __builtin_offsetof(struct M68KState, EXC_TAKEN));

    /* Load VBR */
    *ptr++ = ldr_offset(ctx, vbr, __builtin_offsetof(struct M68KState, VBR));
    *ptr++ = ldr_offset(vbr, REG_PC, exception);



    RA_FreeARMRegister(&ptr, vbr);
    
    if (need_free_ctx) {
        RA_FlushCTX(&ptr);
    }

    va_end(args);

    return ptr;
}

#ifndef __aarch64__
uint32_t *EMIT_ExceptionUnitExit(uint32_t *ptr, uint32_t **pop_loc)
{
    RA_StoreDirtyFPURegs(&ptr);
    RA_StoreDirtyOMMXRegs(&ptr);
    RA_StoreDirtyM68kRegs(&ptr);
    RA_StoreCC(&ptr);
    RA_StoreFPCR(&ptr);
    RA_StoreFPSR(&ptr);

    *ptr++ = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));

#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
    *ptr++ = setend_le();
#endif

    *pop_loc = ptr;
    *ptr++ = pop(1 << REG_SR);   /* placeholder - patched with the unit's mask */
    *ptr++ = bx_lr();

    return ptr;
}

uint32_t *EMIT_ExceptionUnitExitTail(uint32_t *ptr, uint32_t *pop_loc)
{
    if (pop_loc) {
        *ptr++ = (uint32_t)(uintptr_t)pop_loc;
        *ptr++ = INSN_TO_LE(0xfffffff2);
    }
    return ptr;
}

uint32_t *EMIT_AddressError(uint32_t *ptr, uint8_t addr_reg, uint16_t ssw, uint16_t ir)
{
    uint8_t ctx = RA_TryCTX(&ptr);
    uint8_t sp  = RA_MapM68kRegister(&ptr, 15);
    uint8_t tmp = RA_AllocARMRegister(&ptr);
    uint8_t tmp2 = RA_AllocARMRegister(&ptr);
    uint8_t cc  = RA_ModifyCC(&ptr);
    int need_free_ctx = 0;

    if (ctx == 0xff) {
        need_free_ctx = 1;
        ctx = RA_GetCTX(&ptr);
    }

    RA_SetDirtyM68kRegister(&ptr, 15);

    EMIT_tbnz(&ptr, cc, SRB_S, 7);
    *ptr++ = str_offset(ctx, sp, __builtin_offsetof(struct M68KState, USP));
    EMIT_tbnz(&ptr, cc, SRB_M, 3);
    *ptr++ = ldr_offset(ctx, sp, __builtin_offsetof(struct M68KState, ISP));
    *ptr++ = b(2);
    *ptr++ = ldr_offset(ctx, sp, __builtin_offsetof(struct M68KState, MSP));

    /* ---- group-0 frame, pushed from the top down ---- */
    *ptr++ = str_offset_preindex(sp, REG_PC, -4);        /* [sp+10] program counter    */
    *ptr++ = strh_offset_preindex(sp, cc, -2);           /* [sp+8]  status register    */
    *ptr++ = movw_immed_u16(tmp, ir);
    *ptr++ = strh_offset_preindex(sp, tmp, -2);          /* [sp+6]  instruction reg    */
    *ptr++ = str_offset_preindex(sp, addr_reg, -4);      /* [sp+2]  access address     */
    *ptr++ = ubfx(tmp, cc, SRB_S, 1);
    *ptr++ = lsl_immed(tmp, tmp, 2);
    *ptr++ = movw_immed_u16(tmp2, ssw);
    *ptr++ = orr_reg(tmp, tmp, tmp2, LSL, 0);
    *ptr++ = strh_offset_preindex(sp, tmp, -2);          /* [sp+0]  special status word */

    /* Clear trace flags, set supervisor */
    *ptr++ = bic_immed(cc, cc, 2, 32 - SRB_T0);
    *ptr++ = orr_immed(cc, cc, 1, 32 - SRB_S);

    *ptr++ = movw_immed_u16(tmp, 1);
    *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, EXC_TAKEN));

    /* Load VBR and fetch the vector-3 handler into REG_PC */
    *ptr++ = ldr_offset(ctx, tmp, __builtin_offsetof(struct M68KState, VBR));
    *ptr++ = ldr_offset(tmp, REG_PC, VECTOR_ADDRESS_ERROR);


    RA_FreeARMRegister(&ptr, tmp2);
    RA_FreeARMRegister(&ptr, tmp);

    if (need_free_ctx) {
        RA_FlushCTX(&ptr);
    }

    return ptr;
}
#endif /* !__aarch64__ */


static inline uint32_t trace_rd32(uint32_t a)
{
#ifdef CHIPSET_BACKEND
    extern unsigned int ps_read_32(unsigned int address);
    if (a < 0x200000u) return ps_read_32(a);
#endif
    return BE32(*(volatile uint32_t *)(uintptr_t)a);
}
static inline void trace_wr32(uint32_t a, uint32_t v)
{
#ifdef CHIPSET_BACKEND
    extern void ps_write_32(unsigned int address, unsigned int data);
    if (a < 0x200000u) { ps_write_32(a, v); return; }
#endif
    *(volatile uint32_t *)(uintptr_t)a = BE32(v);
}
static inline void trace_wr16(uint32_t a, uint16_t v)
{
#ifdef CHIPSET_BACKEND
    extern void ps_write_16(unsigned int address, unsigned int data);
    if (a < 0x200000u) { ps_write_16(a, v); return; }
#endif
    *(volatile uint16_t *)(uintptr_t)a = BE16(v);
}

void m68k_do_trace(struct M68KState *m68k, uint32_t traced_pc)
{
    uint32_t vbr     = BE32(m68k->VBR);
    uint32_t handler = trace_rd32(vbr + VECTOR_TRACE);
    uint16_t sr      = BE16(m68k->SR);
    uint32_t pc      = BE32(m68k->PC);

    if (!(sr & SR_S)) {
        m68k->USP.u32  = m68k->A[7].u32;
        m68k->A[7].u32 = (sr & SR_M) ? m68k->MSP.u32 : m68k->ISP.u32;
    }
    uint32_t sp = BE32(m68k->A[7].u32);
    sp -= 4; trace_wr32(sp, traced_pc);
    sp -= 2; trace_wr16(sp, (uint16_t)(0x2000u | VECTOR_TRACE));
    sp -= 4; trace_wr32(sp, pc);
    sp -= 2; trace_wr16(sp, sr);
    m68k->A[7].u32 = BE32(sp);
    m68k->SR = BE16((uint16_t)((sr | SR_S) & ~(SR_T0 | SR_T1)));
    m68k->PC = BE32(handler);
}
