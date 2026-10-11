#include "Emu68_ARM_emit.h"
#include <stdint.h>
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

#if defined(EMU68_BANK_FASTPATH) && !defined(__aarch64__)
#if !defined(CHIPSET_BACKEND)
#error "EMU68_BANK_FASTPATH needs the minimig-variant seam backend (ps_read_*/ps_write_*)"
#endif
#include "amiga_memmap.h"
extern unsigned int ps_read_8(unsigned int address);
extern unsigned int ps_read_16(unsigned int address);
extern unsigned int ps_read_32(unsigned int address);
extern void ps_write_8(unsigned int address, unsigned int data);
extern void ps_write_16(unsigned int address, unsigned int data);
extern void ps_write_32(unsigned int address, unsigned int data);

asm(
"       .text                                     \n"
"       .arm                                      \n"
"       .balign 4                                 \n"
/* --- reads: r0 carries the result, so r1-r3 are the caller's to get back --- */
"       .globl  emu68_jit_bank_r8                 \n"
"       .type   emu68_jit_bank_r8,%function       \n"
"emu68_jit_bank_r8:                               \n"
"       push    {r1, r2, r3, r4, r12, lr}         \n"
"       mrs     r12, cpsr                         \n"
"       mov     r4, sp                            \n"
"       bic     sp, sp, #7                        \n"
"       bl      ps_read_8                         \n"
"       mov     sp, r4                            \n"
"       msr     cpsr_f, r12                       \n"
"       pop     {r1, r2, r3, r4, r12, pc}         \n"
"       .globl  emu68_jit_bank_r16                \n"
"       .type   emu68_jit_bank_r16,%function      \n"
"emu68_jit_bank_r16:                              \n"
"       push    {r1, r2, r3, r4, r12, lr}         \n"
"       mrs     r12, cpsr                         \n"
"       mov     r4, sp                            \n"
"       bic     sp, sp, #7                        \n"
"       bl      ps_read_16                        \n"
"       mov     sp, r4                            \n"
"       msr     cpsr_f, r12                       \n"
"       pop     {r1, r2, r3, r4, r12, pc}         \n"
"       .globl  emu68_jit_bank_r32                \n"
"       .type   emu68_jit_bank_r32,%function      \n"
"emu68_jit_bank_r32:                              \n"
"       push    {r1, r2, r3, r4, r12, lr}         \n"
"       mrs     r12, cpsr                         \n"
"       mov     r4, sp                            \n"
"       bic     sp, sp, #7                        \n"
"       bl      ps_read_32                        \n"
"       mov     sp, r4                            \n"
"       msr     cpsr_f, r12                       \n"
"       pop     {r1, r2, r3, r4, r12, pc}         \n"
"       .globl  emu68_jit_bank_w8                 \n"
"       .type   emu68_jit_bank_w8,%function       \n"
"emu68_jit_bank_w8:                               \n"
"       push    {r2, r3, r4, r12, lr}             \n"
"       mrs     r12, cpsr                         \n"
"       mov     r4, sp                            \n"
"       bic     sp, sp, #7                        \n"
"       bl      emu68_bank_bw_w8                  \n"
"       mov     sp, r4                            \n"
"       msr     cpsr_f, r12                       \n"
"       pop     {r2, r3, r4, r12, pc}             \n"
"       .globl  emu68_jit_bank_w16                \n"
"       .type   emu68_jit_bank_w16,%function      \n"
"emu68_jit_bank_w16:                              \n"
"       push    {r2, r3, r4, r12, lr}             \n"
"       mrs     r12, cpsr                         \n"
"       mov     r4, sp                            \n"
"       bic     sp, sp, #7                        \n"
"       bl      emu68_bank_bw_w16                 \n"
"       mov     sp, r4                            \n"
"       msr     cpsr_f, r12                       \n"
"       pop     {r2, r3, r4, r12, pc}             \n"
"       .globl  emu68_jit_bank_w32                \n"
"       .type   emu68_jit_bank_w32,%function      \n"
"emu68_jit_bank_w32:                              \n"
"       push    {r2, r3, r4, r12, lr}             \n"
"       mrs     r12, cpsr                         \n"
"       mov     r4, sp                            \n"
"       bic     sp, sp, #7                        \n"
"       bl      emu68_bank_bw_w32                 \n"
"       mov     sp, r4                            \n"
"       msr     cpsr_f, r12                       \n"
"       pop     {r2, r3, r4, r12, pc}             \n"
);
extern void emu68_jit_bank_r8(void),  emu68_jit_bank_r16(void), emu68_jit_bank_r32(void);
extern void emu68_jit_bank_w8(void),  emu68_jit_bank_w16(void), emu68_jit_bank_w32(void);

static uint32_t bank_gate_addr(uint8_t size, int is_write)
{
    if (is_write)
        return (uint32_t)(uintptr_t)(size == 1 ? emu68_jit_bank_w8
                                   : size == 2 ? emu68_jit_bank_w16
                                               : emu68_jit_bank_w32);
    return (uint32_t)(uintptr_t)(size == 1 ? emu68_jit_bank_r8
                               : size == 2 ? emu68_jit_bank_r16
                                           : emu68_jit_bank_r32);
}

static inline int bank_reg_ok(uint8_t r) { return r <= 12 && r != 9 && r != 11; }

static uint32_t *emit_bank_load(uint32_t *ptr, uint8_t size, uint32_t addr, uint8_t dest)
{
    const uint16_t frame = (dest == 0) ? (1 << 14) : ((1 << 0) | (1 << 14));
    *ptr++ = push(frame);
    *ptr++ = movw_immed_u16(0, addr & 0xffff);
    *ptr++ = movt_immed_u16(0, addr >> 16);      /* every bank has a nonzero high half */
    *ptr++ = ldr_offset(15, 14, 4);              /* ldr lr, [pc, #4] -> the .word below */
    *ptr++ = blx_cc_reg(ARM_CC_AL, 14);
    *ptr++ = b_cc(ARM_CC_AL, 2);                 /* ARM.h convention: N=2 skips one word */
    *ptr++ = BE32(bank_gate_addr(size, 0));      /* DATA word: BE32, not INSN_TO_LE */
    if (dest != 0)
    {
        *ptr++ = mov_reg(dest, 0);               /* before the pop: the pop restores r0 */
        *ptr++ = pop(frame);
    }
    else
        *ptr++ = pop(frame);
    return ptr;
}

static uint32_t *emit_bank_store(uint32_t *ptr, uint8_t size, uint32_t addr, uint8_t val)
{
    const uint16_t frame = (1 << 0) | (1 << 1) | (1 << 14);
    *ptr++ = push(frame);
    if (val != 1)
        *ptr++ = mov_reg(1, val);
    *ptr++ = movw_immed_u16(0, addr & 0xffff);
    *ptr++ = movt_immed_u16(0, addr >> 16);
    *ptr++ = ldr_offset(15, 14, 4);
    *ptr++ = blx_cc_reg(ARM_CC_AL, 14);
    *ptr++ = b_cc(ARM_CC_AL, 2);
    *ptr++ = BE32(bank_gate_addr(size, 1));
    *ptr++ = pop(frame);
    return ptr;
}
#endif  /* EMU68_BANK_FASTPATH */


#if !defined(__aarch64__)
extern int32_t _pc_rel;

static inline uint32_t *emit_aerr_guard(uint32_t *ptr, uint8_t size, uint8_t base, uint8_t index,
                                        uint8_t shift, int32_t offset, int is_write)
{
    (void)size; (void)base; (void)index; (void)shift; (void)offset; (void)is_write;
    return ptr;
}

static uint32_t *emit_aerr_static(uint32_t *ptr, uint8_t size, uint32_t addr, int is_write)
{
    (void)size; (void)addr; (void)is_write;
    return ptr;
}
#else
static inline uint32_t *emit_aerr_guard(uint32_t *ptr, uint8_t size, uint8_t base, uint8_t index,
                                        uint8_t shift, int32_t offset, int is_write)
{
    (void)size; (void)base; (void)index; (void)shift; (void)offset; (void)is_write;
    return ptr;
}
static inline uint32_t *emit_aerr_static(uint32_t *ptr, uint8_t size, uint32_t addr, int is_write)
{
    (void)size; (void)addr; (void)is_write;
    return ptr;
}
#endif

static inline __attribute__((always_inline)) uint32_t * load_s16_ext32(uint32_t *ptr, uint8_t reg, int16_t s16)
{
#ifdef __aarch64__
    if (s16 & 0x8000)
        *ptr++ = movn_immed_u16(reg, ~s16, 0);
    else
        *ptr++ = movw_immed_u16(reg, s16);
#else
    if (s16 >= -256 && s16 < 0)
        *ptr++ = mvn_immed_u8(reg, ~s16);
    else {
        *ptr++ = movw_immed_u16(reg, s16);
        if (s16 & 0x8000)
            *ptr++ = movt_immed_u16(reg, 0xffff);
    }
#endif
    return ptr;
}

static inline __attribute__((always_inline)) uint32_t * load_reg_from_addr_offset(uint32_t *ptr, uint8_t size, uint8_t base, uint8_t reg, int32_t offset, uint8_t offset_32bit)
{
    uint8_t reg_d16 = RA_AllocARMRegister(&ptr);

    int free_base = 0;

    if (base == 0xff)
    {
        free_base = 1;
        base = RA_AllocARMRegister(&ptr);
        *ptr++ = mov_immed_u8(base, 0); /* A32: no zero register - upstream's mov_reg(base, 31) encodes garbage here */
    }

    ptr = emit_aerr_guard(ptr, size, base, 0xff, 0, offset, 0);

#ifndef __aarch64__
    if (size == 0 ||
        (size == 2 && (offset < -255 || offset > 255)) ||
        (offset < -4095 || offset > 4095))
    {
        if (offset_32bit) {
            *ptr++ = movw_immed_u16(reg_d16, offset);
            if ((offset >> 16) & 0xffff) {
                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
            }
        } else {
            *ptr++ = movw_immed_u16(reg_d16, offset);
            if (offset & 0x8000)
                *ptr++ = movt_immed_u16(reg_d16, 0xffff);
        }
    }
#endif

    switch (size)
    {
        case 4:
#ifdef __aarch64__
            if (offset == 0)
                *ptr++ = ldr_offset(base, reg, 0);
            else if (offset > -256 && offset < 256)
                *ptr++ = ldur_offset(base, reg, offset & 0x1ff);
            else if (offset > 0 && offset < 16384 && (offset & 3) == 0)
                *ptr++ = ldr_offset(base, reg, offset);
            else {
                if (offset_32bit) {
                    if ((offset & 0xffff) != 0) {
                        *ptr++ = movw_immed_u16(reg_d16, offset);
                        if ((offset >> 16) & 0xffff) {
                            *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                        }
                    } else {
                        if ((offset >> 16) & 0xffff)
                        {
                            *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                        }
                        else
                        {
                            *ptr++ = mov_reg(reg_d16, 31);
                        }
                    }
                }
                else {
                    if (offset > 0)
                        *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                    else
                        *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                }
                *ptr++ = add_reg(reg_d16, base, reg_d16, LSL, 0);
                *ptr++ = ldr_offset(reg_d16, reg, 0);
            }
#else
                if (offset > -4096 && offset < 4096)
                    *ptr++ = ldr_offset(base, reg, offset);
                else
                    *ptr++ = ldr_regoffset(base, reg, reg_d16, UXTW, 0);
#endif
                break;
            case 2:
#ifdef __aarch64__
                if (offset > -256 && offset < 256)
                    *ptr++ = ldurh_offset(base, reg, offset & 0x1ff);
                else if (offset >= 0 && offset < 8192 && (offset & 1) == 0)
                    *ptr++ = ldrh_offset(base, reg, offset);
                else {
                    if (offset_32bit) {
                        if (offset & 0xffff) {
                            *ptr++ = movw_immed_u16(reg_d16, offset);
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                            }
                        } else {
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                            } else {
                                *ptr++ = mov_reg(reg_d16, 31);
                            }
                        }
                    } else {
                        if (offset > 0)
                            *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                        else
                            *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                    }
                    *ptr++ = add_reg(reg_d16, base, reg_d16, LSL, 0);
                    *ptr++ = ldrh_offset(reg_d16, reg, 0);
                }
#else
                if (offset > -256 && offset < 256)
                    *ptr++ = ldrh_offset(base, reg, offset);
                else
                    *ptr++ = ldrh_regoffset(base, reg, reg_d16);
#endif
                break;
            case 1:
#ifdef __aarch64__
                if (offset > -256 && offset < 256)
                    *ptr++ = ldurb_offset(base, reg, offset);
                else if (offset >= 0 && offset < 4096)
                    *ptr++ = ldrb_offset(base, reg, offset);
                else {
                    if (offset_32bit) {
                        if (offset & 0xffff) {
                            *ptr++ = movw_immed_u16(reg_d16, offset);
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                            }
                        } else {
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                            } else {
                                *ptr++ = mov_reg(reg_d16, 31);
                            }
                        }
                    } else {
                        if (offset > 0)
                            *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                        else
                            *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                    }
                    *ptr++ = add_reg(reg_d16, base, reg_d16, LSL, 0);
                    *ptr++ = ldrb_offset(reg_d16, reg, 0);
                }
#else
                if (offset > -4096 && offset < 4096)
                    *ptr++ = ldrb_offset(base, reg, offset);
                else
                    *ptr++ = ldrb_regoffset(base, reg, reg_d16, 0);
#endif
                break;
            case 0:
#ifdef __aarch64__
                if (offset > -4096 && offset < 4096)
                {
                    if (offset < 0)
                        *ptr++ = sub_immed(reg, base, -offset);
                    else
                        *ptr++ = add_immed(reg, base, offset);
                }
                else
                {
                    if (offset_32bit) {
                        if (offset & 0xffff) {
                            *ptr++ = movw_immed_u16(reg_d16, offset);
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                            }
                        } else {
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                            } else {
                                *ptr++ = mov_reg(reg_d16, 31);
                            }
                        }
                    } else {
                        if (offset > 0)
                            *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                        else
                            *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                    }
                    *ptr++ = add_reg(reg, base, reg_d16, LSL, 0);
                }
#else
                *ptr++ = add_reg(reg, base, reg_d16, LSL, 0);
#endif
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
            }
            RA_FreeARMRegister(&ptr, reg_d16);

    if (free_base)
        RA_FreeARMRegister(&ptr, base);

    return ptr;
}

static inline __attribute__((always_inline)) uint32_t * load_reg_from_addr(uint32_t *ptr, uint8_t size, uint8_t base, uint8_t reg, uint8_t index, uint8_t shift)
{
    int free_base = 0;

    if (base == 0xff)
    {
        free_base = 1;
        base = RA_AllocARMRegister(&ptr);
        *ptr++ = mov_immed_u8(base, 0); /* A32: no zero register - upstream's mov_reg(base, 31) encodes garbage here */
    }

    ptr = emit_aerr_guard(ptr, size, base, index, shift, 0, 0);

    if (index == 0xff)
    {
        switch (size)
        {
            case 4:
                *ptr++ = ldr_offset(base, reg, 0);
                break;
            case 2:
                *ptr++ = ldrh_offset(base, reg, 0);
                break;
            case 1:
                *ptr++ = ldrb_offset(base, reg, 0);
                break;
            case 0:
                *ptr++ = mov_reg(reg, base);
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
    }
    else
    {
        uint8_t tmp = 0xff;
#ifdef __aarch64__
        tmp = RA_AllocARMRegister(&ptr);
        switch (size)
        {
            case 4:
                *ptr++ = add_reg(tmp, base, index, LSL, shift);
                *ptr++ = ldr_offset(tmp, reg, 0);
                break;
            case 2:
                *ptr++ = add_reg(tmp, base, index, LSL, shift);
                *ptr++ = ldrh_offset(tmp, reg, 0);
                break;
            case 1:
                *ptr++ = add_reg(tmp, base, index, LSL, shift);
                *ptr++ = ldrb_offset(tmp, reg, 0);
                break;
            case 0:
                *ptr++ = add_reg(reg, base, index, LSL, shift);
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
        RA_FreeARMRegister(&ptr, tmp);
#else
        switch (size)
        {
            case 4:
                *ptr++ = ldr_regoffset(base, reg, index, UXTW, shift);
                break;
            case 2:
                if (shift) {
                    tmp = RA_AllocARMRegister(&ptr);
                    *ptr++ = lsl_immed(tmp, index, shift);
                    *ptr++ = ldrh_regoffset(base, reg, tmp);
                    RA_FreeARMRegister(&ptr, tmp);
                } else {
                    *ptr++ = ldrh_regoffset(base, reg, index);
                }
                break;
            case 1:
                *ptr++ = ldrb_regoffset(base, reg, index, shift);
                break;
            case 0:
                *ptr++ = add_reg(reg, base, index, LSL, shift);
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
#endif
    }

    if (free_base)
        RA_FreeARMRegister(&ptr, base);

    return ptr;
}

static inline __attribute__((always_inline)) uint32_t * store_reg_to_addr_offset(uint32_t *ptr, uint8_t size, uint8_t base, uint8_t reg, int32_t offset, uint8_t offset_32bit)
{
    uint8_t reg_d16 = RA_AllocARMRegister(&ptr);
    int free_base = 0;

    if (base == 0xff)
    {
        free_base = 1;
        base = RA_AllocARMRegister(&ptr);
        *ptr++ = mov_immed_u8(base, 0); /* A32: no zero register - upstream's mov_reg(base, 31) encodes garbage here */
    }

    ptr = emit_aerr_guard(ptr, size, base, 0xff, 0, offset, 1);

#ifndef __aarch64__
    if (size == 0 ||
        (size == 2 && (offset < -255 || offset > 255)) ||
        (offset < -4095 || offset > 4095))
    {
        if (offset_32bit) {
            *ptr++ = movw_immed_u16(reg_d16, offset);
            if ((offset >> 16) & 0xffff) {
                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
            }
        } else {
            *ptr++ = movw_immed_u16(reg_d16, offset);
            if (offset & 0x8000)
                *ptr++ = movt_immed_u16(reg_d16, 0xffff);
        }
    }
#endif

    switch (size)
    {
        case 4:
#ifdef __aarch64__
            if (offset == 0)
                *ptr++ = str_offset(base, reg, 0);
            else if (offset > -256 && offset < 256)
                *ptr++ = stur_offset(base, reg, offset & 0x1ff);
            else if (offset > 0 && offset < 16384 && (offset & 3) == 0)
                *ptr++ = str_offset(base, reg, offset);
            else {
                if (offset_32bit) {
                    if (offset & 0xffff) {
                        *ptr++ = movw_immed_u16(reg_d16, offset);
                        if ((offset >> 16) & 0xffff) {
                            *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                        }
                    } else {
                        if ((offset >> 16) & 0xffff) {
                            *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                        } else {
                            *ptr++ = mov_reg(reg_d16, 31);
                        }
                    }
                }
                else {
                    if (offset > 0)
                        *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                    else
                        *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                }
                *ptr++ = add_reg(reg_d16, base, reg_d16, LSL, 0);
                *ptr++ = str_offset(reg_d16, reg, 0);
            }
#else
                if (offset > -4096 && offset < 4096)
                    *ptr++ = str_offset(base, reg, offset);
                else
                    *ptr++ = str_regoffset(base, reg, reg_d16, 0);
#endif
                break;
            case 2:
#ifdef __aarch64__
                if (offset > -256 && offset < 256)
                    *ptr++ = sturh_offset(base, reg, offset & 0x1ff);
                else if (offset >= 0 && offset < 8192 && (offset & 1) == 0)
                    *ptr++ = strh_offset(base, reg, offset);
                else {
                    if (offset_32bit) {
                        if (offset & 0xffff) {
                            *ptr++ = movw_immed_u16(reg_d16, offset);
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                            }
                        } else {
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                            } else {
                                *ptr++ = mov_reg(reg_d16, 31);
                            }
                        }
                    } else {
                        if (offset > 0)
                            *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                        else
                            *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                    }
                    *ptr++ = add_reg(reg_d16, base, reg_d16, LSL, 0);
                    *ptr++ = strh_offset(reg_d16, reg, 0);
                }
#else
                if (offset > -256 && offset < 256)
                    *ptr++ = strh_offset(base, reg, offset);
                else
                    *ptr++ = strh_regoffset(base, reg, reg_d16);
#endif
                break;
            case 1:
#ifdef __aarch64__
                if (offset > -256 && offset < 256)
                    *ptr++ = sturb_offset(base, reg, offset);
                else if (offset >= 0 && offset < 4096)
                    *ptr++ = strb_offset(base, reg, offset);
                else {
                    if (offset_32bit) {
                        if (offset & 0xffff) {
                            *ptr++ = movw_immed_u16(reg_d16, offset);
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                            }
                        } else {
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                            } else {
                                *ptr++ = mov_reg(reg_d16, 31);
                        }
                    }
                    } else {
                        if (offset > 0)
                            *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                        else
                            *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                    }
                    *ptr++ = add_reg(reg_d16, base, reg_d16, LSL, 0);
                    *ptr++ = strb_offset(reg_d16, reg, 0);
                }
#else
                if (offset > -4096 && offset < 4096)
                    *ptr++ = strb_offset(base, reg, offset);
                else
                    *ptr++ = strb_regoffset(base, reg, reg_d16, 0);
#endif
                break;
            case 0:
#ifdef __aarch64__
                if (offset > -4096 && offset < 4096)
                {
                    if (offset < 0)
                        *ptr++ = sub_immed(reg, base, -offset);
                    else
                        *ptr++ = add_immed(reg, base, offset);
                }
                else
                {
                    if (offset_32bit) {
                        if (offset & 0xffff) {
                            *ptr++ = movw_immed_u16(reg_d16, offset);
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = movt_immed_u16(reg_d16, (offset >> 16) & 0xffff);
                            }
                        } else {
                            if ((offset >> 16) & 0xffff) {
                                *ptr++ = mov_immed_u16(reg_d16, (offset >> 16) & 0xffff, 1);
                            } else {
                                *ptr++ = mov_reg(reg_d16, 31);
                            }
                        }
                    } else {
                        if (offset > 0)
                            *ptr++ = mov_immed_u16(reg_d16, offset, 0);
                        else
                            *ptr++ = movn_immed_u16(reg_d16, -offset - 1, 0);
                    }
                    *ptr++ = add_reg(reg, base, reg_d16, LSL, 0);
                }
#else
                *ptr++ = add_reg(reg, base, reg_d16, LSL, 0);
#endif
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
        RA_FreeARMRegister(&ptr, reg_d16);

    if (free_base)
        RA_FreeARMRegister(&ptr, base);

    return ptr;
}

static inline __attribute__((always_inline)) uint32_t * store_reg_to_addr(uint32_t *ptr, uint8_t size, uint8_t base, uint8_t reg, uint8_t index, uint8_t shift)
{
    int free_base = 0;

    if (base == 0xff)
    {
        free_base = 1;
        base = RA_AllocARMRegister(&ptr);
        *ptr++ = mov_immed_u8(base, 0); /* A32: no zero register - upstream's mov_reg(base, 31) encodes garbage here */
    }

    ptr = emit_aerr_guard(ptr, size, base, index, shift, 0, 1);

    if (index == 0xff)
    {
        switch (size)
        {
            case 4:
                *ptr++ = str_offset(base, reg, 0);
                break;
            case 2:
                *ptr++ = strh_offset(base, reg, 0);
                break;
            case 1:
                *ptr++ = strb_offset(base, reg, 0);
                break;
            case 0:
                *ptr++ = mov_reg(reg, base);
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
    }
    else
    {
        uint8_t tmp = 0xff;
#ifdef __aarch64__
        tmp = RA_AllocARMRegister(&ptr);
        switch (size)
        {
            case 4:
                *ptr++ = add_reg(tmp, base, index, LSL, shift);
                *ptr++ = str_offset(tmp, reg, 0);
                break;
            case 2:
                *ptr++ = add_reg(tmp, base, index, LSL, shift);
                *ptr++ = strh_offset(tmp, reg, 0);
                break;
            case 1:
                *ptr++ = add_reg(tmp, base, index, LSL, shift);
                *ptr++ = strb_offset(tmp, reg, 0);
                break;
            case 0:
                *ptr++ = add_reg(reg, base, index, LSL, shift);
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
        RA_FreeARMRegister(&ptr, tmp);
#else
        switch (size)
        {
            case 4:
                *ptr++ = str_regoffset(base, reg, index, shift);
                break;
            case 2:
                if (shift) {
                    tmp = RA_AllocARMRegister(&ptr);
                    *ptr++ = lsl_immed(tmp, index, shift);
                    *ptr++ = strh_regoffset(base, reg, tmp);
                    RA_FreeARMRegister(&ptr, tmp);
                } else {
                    *ptr++ = strh_regoffset(base, reg, index);
                }
                break;
            case 1:
                *ptr++ = strb_regoffset(base, reg, index, shift);
                break;
            case 0:
                *ptr++ = add_reg(reg, base, index, LSL, shift);
                break;
            default:
                kprintf("Unknown size opcode\n");
                break;
        }
#endif
    }

    if (free_base)
        RA_FreeARMRegister(&ptr, base);

    return ptr;
}

#define M68K_EA_DA 0x8000
#define M68K_EA_REG 0x7000
#define M68K_EA_WL 0x0800
#define M68K_EA_SCALE 0x0600
#define M68K_EA_FULL 0x0100
#define M68K_EA_OFF8 0x00FF

#define M68K_EA_BS 0x0080
#define M68K_EA_IS 0x0040
#define M68K_EA_BD_SIZE 0x0030
#define M68K_EA_IIS 0x0007

/*
    Emits ARM insns to load effective address and read value from ther to specified register.

    Inputs:
        ptr     pointer to ARM instruction stream
        size    size of data for load operation, can be 4 (long), 2 (short) or 1 (byte).
                If size of 0 is specified the function does not load a value from EA into
                register but rather loads the EA into that register.
                If postincrement or predecrement modes are used and size 0 is specified, then
                the instruction translator is reponsible for increasing/decreasing the address
                register, otherwise it is done in this function!
        arm_reg ARM register to store the EA or value from EA into
        ea      EA encoded field.
        m68k_ptr pointer to m68k instruction stream past the instruction opcode itself. It may
                be increased if further bytes from m68k side are read

    Output:
        ptr     pointer to ARM instruction stream after the newly generated code
*/
uint32_t *EMIT_LoadFromEffectiveAddress(uint32_t *ptr, uint8_t size, uint8_t *arm_reg, uint8_t ea, uint16_t *m68k_ptr, uint8_t *ext_words, uint8_t read_only, int32_t *imm_offset)
{
    uint8_t mode = ea >> 3;
    uint8_t src_reg = ea & 7;

    if (imm_offset)
        *imm_offset = 0;

    if (mode == 0) /* Mode 000: Dn */
    {
        switch (size)
        {
            case 4:
            case 2:
            case 1:
                if (read_only)
                    *arm_reg = RA_MapM68kRegister(&ptr, src_reg);
                else
                    *arm_reg = RA_CopyFromM68kRegister(&ptr, src_reg);
                break;
            case 0:
#ifdef __aarch64__
                kprintf("Load form EA: Dn with wrong operand size! Opcode %04x at %08x\n", BE16(m68k_ptr[-*ext_words]), m68k_ptr - *ext_words);
#else
                *arm_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = add_immed(*arm_reg, REG_CTX, __builtin_offsetof(struct M68KState, D[src_reg]));
#endif
                break;
            default:
                kprintf("Wrong size\n");
                break;
        }
    }
    else if (mode == 1) /* Mode 001: An */
    {
        switch (size)
        {
            case 4:
            case 2:
                if (read_only)
                    *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
                else
                    *arm_reg = RA_CopyFromM68kRegister(&ptr, src_reg + 8);
                break;
            case 0:
#ifdef __aarch64__
                kprintf("Load form EA: An with wrong operand size! Opcode %04x at %08x\n", BE16(m68k_ptr[-*ext_words]), m68k_ptr - *ext_words);
                {
                    uint16_t *ptr = &m68k_ptr[-*ext_words] - 8;
                    for (int i=0; i < 16; i++)
                        kprintf("%04x ", ptr[i]);
                    kprintf("\n");
                }
#else
                *arm_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = add_immed(*arm_reg, REG_CTX, __builtin_offsetof(struct M68KState, A[src_reg]));
#endif
                break;
            default:
                kprintf("Wrong size\n");
                break;
        }
    }
    else
    {
        if (*arm_reg == 0xff)
            *arm_reg = RA_AllocARMRegister(&ptr);

        if (mode == 2) /* Mode 002: (An) */
        {
            if (size == 0) {
                if (read_only) {
                    RA_FreeARMRegister(&ptr, *arm_reg);
                    *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
                } else {
                    uint8_t tmp = RA_MapM68kRegister(&ptr, src_reg + 8);
                    *ptr++ = mov_reg(*arm_reg, tmp);
                }
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
                ptr = emit_aerr_guard(ptr, size, reg_An, 0xff, 0, 0, 0);
                switch (size)
                {
                    case 4:
                        *ptr++ = ldr_offset(reg_An, *arm_reg, 0);
                        break;
                    case 2:
                        *ptr++ = ldrh_offset(reg_An, *arm_reg, 0);
                        break;
                    case 1:
                        *ptr++ = ldrb_offset(reg_An, *arm_reg, 0);
                        break;
                    default:
                        kprintf("Unknown size opcode\n");
                        break;
                }
            }
        }
        else if (mode == 3) /* Mode 003: (An)+ */
        {
            if (size == 0) {
                RA_FreeARMRegister(&ptr, *arm_reg);
                *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);

                RA_SetDirtyM68kRegister(&ptr, 8 + src_reg);
                ptr = emit_aerr_guard(ptr, size, reg_An, 0xff, 0, 0, 0);

                /* Rare case where source and dest are the same register and size == 4 */
                if (size == 4 && reg_An == *arm_reg) {
                    *ptr++ = ldr_offset(reg_An, *arm_reg, 0);
                }
                else
                {
                    switch (size)
                    {
                        case 4:
                            *ptr++ = ldr_offset_postindex(reg_An, *arm_reg, 4);
                            break;
                        case 2:
                            *ptr++ = ldrh_offset_postindex(reg_An, *arm_reg, 2);
                            break;
                        case 1:
                            if (src_reg == 7)
                                *ptr++ = ldrb_offset_postindex(reg_An, *arm_reg, 2);
                            else
                                *ptr++ = ldrb_offset_postindex(reg_An, *arm_reg, 1);
                            break;
                        default:
                            kprintf("Unknown size opcode\n");
                            break;
                    }
                }
            }
        }
        else if (mode == 4) /* Mode 004: -(An) */
        {
            if (size == 0) {
                RA_FreeARMRegister(&ptr, *arm_reg);
                *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
                RA_SetDirtyM68kRegister(&ptr, 8 + src_reg);
                ptr = emit_aerr_guard(ptr, size, reg_An, 0xff, 0, -(int32_t)size, 0);

                /* Rare case where source and dest are the same register and size == 4 */
                if (size == 4 && reg_An == *arm_reg) {
                    *ptr++ = ldur_offset(reg_An, *arm_reg, -4);
                }
                else
                {
                    switch (size)
                    {
                        case 4:
                            *ptr++ = ldr_offset_preindex(reg_An, *arm_reg, -4);
                            break;
                        case 2:
                            *ptr++ = ldrh_offset_preindex(reg_An, *arm_reg, -2);
                            break;
                        case 1:
                            if (src_reg == 7)
                                *ptr++ = ldrb_offset_preindex(reg_An, *arm_reg, -2);
                            else
                                *ptr++ = ldrb_offset_preindex(reg_An, *arm_reg, -1);
                            break;
                        default:
                            kprintf("Unknown size opcode\n");
                            break;
                    }
                }
            }
        }
        else if (mode == 5) /* Mode 005: (d16, An) */
        {
            if (imm_offset && size == 0 && read_only)
            {
                RA_FreeARMRegister(&ptr, *arm_reg);
                *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
                *imm_offset = (int16_t)BE16(m68k_ptr[(*ext_words)++]);
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
                int16_t off16 = (int16_t)BE16(m68k_ptr[(*ext_words)++]);

                ptr = load_reg_from_addr_offset(ptr, size, reg_An, *arm_reg, off16, 0);
            }
        }
        else if (mode == 6) /* Mode 006: (d8, An, Xn.SIZE*SCALE) */
        {
            uint16_t brief = BE16(m68k_ptr[(*ext_words)++]);
            uint8_t extra_reg = (brief >> 12) & 7;

            if ((brief & 0x0100) == 0)
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
                uint8_t tmp1 = 0xff;
                uint8_t tmp2 = 0xff;
                int8_t displ = brief & 0xff;

                if (displ > 0)
                {
                    tmp1 = RA_AllocARMRegister(&ptr);
                    *ptr++ = add_immed(tmp1, reg_An, displ);
                }
                else if (displ < 0)
                {
                    tmp1 = RA_AllocARMRegister(&ptr);
                    *ptr++ = sub_immed(tmp1, reg_An, -displ);
                }
                else
                {
                    //*ptr++ = mov_reg(tmp1, reg_An);
                }


                if (brief & (1 << 11))
                {
                    if (brief & 0x8000)
                        tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                    else
                        tmp2 = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                }
                else
                {
                    if (brief & 0x8000)
                        tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                    else
                        tmp2 = RA_MapM68kRegister(&ptr, extra_reg);
                    
                    uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    *ptr++ = sxth(tmp3, tmp2);
#else
                    *ptr++ = sxth(tmp3, tmp2);
#endif
                    RA_FreeARMRegister(&ptr, tmp2);
                    tmp2 = tmp3;
                }

                ptr = load_reg_from_addr(ptr, size, displ ? tmp1 : reg_An, *arm_reg, tmp2, (brief >> 9) & 3);

                if (displ)
                    RA_FreeARMRegister(&ptr, tmp1);

                RA_FreeARMRegister(&ptr, tmp2);
            }
            else
            {
                uint8_t bd_reg = 0xff;
                uint8_t outer_reg = 0xff;
                uint8_t base_reg = 0xff;
                uint8_t index_reg = 0xff;

                /* Check if base register is suppressed */
                if (!(brief & M68K_EA_BS))
                {
                    /* Base register in use. Alloc it and load its contents */
                    base_reg = RA_MapM68kRegister(&ptr, 8 + src_reg);
                }

                /* Check if index register is in use */
                if (!(brief & M68K_EA_IS))
                {
                    /* Index register in use. Alloc it and load its contents */
                    if (brief & (1 << 11))
                    {
                        if (brief & 0x8000)
                            index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                        else
                            index_reg = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                    }
                    else
                    {
                        if (brief & 0x8000)
                            index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                        else
                            index_reg = RA_MapM68kRegister(&ptr, extra_reg);

                        uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                        *ptr++ = sxth(tmp3, index_reg);
#else
                        *ptr++ = sxth(tmp3, index_reg);
#endif
                        RA_FreeARMRegister(&ptr, index_reg);
                        index_reg = tmp3;
                    }
                }

                uint16_t lo16, hi16;

                /* Check if base displacement needs to be fetched */
                switch ((brief & M68K_EA_BD_SIZE) >> 4)
                {
                    case 2: /* Word displacement */
                        bd_reg = RA_AllocARMRegister(&ptr);
                        lo16 = BE16(m68k_ptr[(*ext_words)++]);
                        ptr = load_s16_ext32(ptr, bd_reg, lo16);
                        break;
                    case 3: /* Long displacement */
                        bd_reg = RA_AllocARMRegister(&ptr);
                        hi16 = BE16(m68k_ptr[(*ext_words)++]);
                        lo16 = BE16(m68k_ptr[(*ext_words)++]);
                        *ptr++ = movw_immed_u16(bd_reg, lo16);
                        if (hi16 != 0)
                            *ptr++ = movt_immed_u16(bd_reg, hi16);
                        break;
                }

                /* Check if outer displacement needs to be fetched */
                switch ((brief & M68K_EA_IIS) & 3)
                {
                    case 2: /* Word outer displacement */
                        outer_reg = RA_AllocARMRegister(&ptr);
                        lo16 = BE16(m68k_ptr[(*ext_words)++]);
                        ptr = load_s16_ext32(ptr, outer_reg, lo16);
                        break;
                    case 3: /* Long outer displacement */
                        outer_reg = RA_AllocARMRegister(&ptr);
                        hi16 = BE16(m68k_ptr[(*ext_words)++]);
                        lo16 = BE16(m68k_ptr[(*ext_words)++]);
                        *ptr++ = movw_immed_u16(outer_reg, lo16);
                        if (hi16 != 0)
                            *ptr++ = movt_immed_u16(outer_reg, hi16);
                        break;
                }

                if ((brief & 0x0f) == 0)
                {
                    /* Address register indirect with index mode */
                    if (base_reg != 0xff && bd_reg != 0xff)
                    {
#ifdef __aarch64__
                        *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                        *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                    }
                    else if (bd_reg == 0xff && base_reg != 0xff)
                    {
                        bd_reg = base_reg;
                    }
                    /*
                        Now, either base register or base displacement were given, if
                        index register was specified, use it.
                    */
                    ptr = load_reg_from_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                }
                else
                {
                    if (bd_reg == 0xff)
                    {
                        bd_reg = RA_AllocARMRegister(&ptr);
                        if (base_reg == 0xff)
                            *ptr++ = mov_immed_u8(bd_reg, 0); /* upstream c0f639e: mov_reg(bd_reg, 31) */
                        else
                            *ptr++ = mov_reg(bd_reg, base_reg);
                        base_reg = 0xff;
                    }

                    /* Postindexed mode */
                    if (brief & 0x04)
                    {
                        /* Fetch data from base reg */
                        if (base_reg == 0xff)
                            *ptr++ = ldr_offset(bd_reg, bd_reg, 0);
                        else
#ifdef __aarch64__
                            *ptr++ = ldr_regoffset(bd_reg, bd_reg, base_reg, UXTW, 0);
#else
                            *ptr++ = ldr_regoffset(bd_reg, bd_reg, base_reg, UXTW, 0);
#endif
                        if (outer_reg != 0xff)
#ifdef __aarch64__
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif

                        ptr = load_reg_from_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                    }
                    else /* Preindexed mode */
                    {
                        /* Fetch data from base reg with eventually applied index */
                        if (brief & M68K_EA_IS)
                        {
                            if (bd_reg == 0xff) {
                                bd_reg = RA_AllocARMRegister(&ptr);
                                *ptr++ = ldr_offset(base_reg, bd_reg, 0);
                            }
                            else
                                ptr = load_reg_from_addr(ptr, 4, base_reg, bd_reg, bd_reg, 0);
                        }
                        else
                        {
#ifdef __aarch64__
                            if (bd_reg != 0xff && base_reg != 0xff) /* upstream 88b7d9a */
                                *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                            if (bd_reg != 0xff && base_reg != 0xff) /* upstream 88b7d9a */
                                *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                            ptr = load_reg_from_addr(ptr, 4, bd_reg, bd_reg, index_reg, (brief >> 9) & 3);
                        }

                        if (outer_reg != 0xff)
#ifdef __aarch64__
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif
                        ptr = load_reg_from_addr(ptr, size, bd_reg, *arm_reg, 0xff, 0);
                    }
                }

                if (bd_reg != 0xff)
                    RA_FreeARMRegister(&ptr, bd_reg);
                if (outer_reg != 0xff)
                    RA_FreeARMRegister(&ptr, outer_reg);
                if (base_reg != 0xff)
                    RA_FreeARMRegister(&ptr, base_reg);
                if (index_reg != 0xff)
                    RA_FreeARMRegister(&ptr, index_reg);
            }
        }
        else if (mode == 7)
        {
            if (src_reg == 2) /* (d16, PC) mode */
            {
                if (imm_offset && size == 0 && read_only)
                {
                    int8_t off8 = 2 + 2*(*ext_words);
                    ptr = EMIT_GetOffsetPC(ptr, &off8);
                    RA_FreeARMRegister(&ptr, *arm_reg);
                    *arm_reg = REG_PC;
                    *imm_offset = off8 + (int16_t)BE16(m68k_ptr[(*ext_words)++]);
                }
                else
                {
                    int8_t off8 = 2 + 2*(*ext_words);
                    ptr = EMIT_GetOffsetPC(ptr, &off8);
                    int32_t off = off8 + (int16_t)(BE16(m68k_ptr[(*ext_words)++]));

                    ptr = load_reg_from_addr_offset(ptr, size, REG_PC, *arm_reg, off, 1);
                }
            }
            else if (src_reg == 3)
            {
                uint16_t brief = BE16(m68k_ptr[(*ext_words)++]);
                uint8_t extra_reg = (brief >> 12) & 7;

                if ((brief & 0x0100) == 0)
                {
                    uint8_t tmp1 = RA_AllocARMRegister(&ptr);
                    uint8_t tmp2 = 0xff;
                    if (*arm_reg == 0xff)
                        *arm_reg = RA_AllocARMRegister(&ptr);
                    int8_t displ = brief & 0xff;
                    int8_t off = 2 + 2*(*ext_words - 1);
                    int16_t full_off = 0;
                    ptr = EMIT_GetOffsetPC(ptr, &off);

                    full_off = off + displ;

                    if (full_off >= 0)
                    {
                        *ptr++ = add_immed(tmp1, REG_PC, full_off);
                    }
                    else
                    {
                        *ptr++ = sub_immed(tmp1, REG_PC, -full_off);
                    }

                    if (brief & (1 << 11))
                    {
                        if (brief & 0x8000)
                            tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                        else
                            tmp2 = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                    }
                    else
                    {
                        if (brief & 0x8000)
                            tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                        else
                            tmp2 = RA_MapM68kRegister(&ptr, extra_reg);

                        uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                        *ptr++ = sxth(tmp3, tmp2);
#else
                        *ptr++ = sxth(tmp3, tmp2);
#endif
                        RA_FreeARMRegister(&ptr, tmp2);
                        tmp2 = tmp3;
                    }

                    ptr = load_reg_from_addr(ptr, size, tmp1, *arm_reg, tmp2, (brief >> 9) & 3);

                    RA_FreeARMRegister(&ptr, tmp1);
                    RA_FreeARMRegister(&ptr, tmp2);
                }
                else
                {
                    uint8_t bd_reg = 0xff;
                    uint8_t outer_reg = 0xff;
                    uint8_t base_reg = 0xff;
                    uint8_t index_reg = 0xff;

                    /* Check if base register is suppressed */
                    if (!(brief & M68K_EA_BS))
                    {
                        /* Base register in use. Alloc it and load its contents */
                        base_reg = RA_AllocARMRegister(&ptr);
                        int8_t off = 2 + 2*(*ext_words - 1);
                        //ptr = EMIT_FlushPC(ptr);
                        ptr = EMIT_GetOffsetPC(ptr, &off);
                        if (off > 0)
                            *ptr++ = add_immed(base_reg, REG_PC, off);
                        else
                            *ptr++ = sub_immed(base_reg, REG_PC, -off);
                    }

                    /* Check if index register is in use */
                    if (!(brief & M68K_EA_IS))
                    {
                        /* Index register in use. Alloc it and load its contents */
                        if (brief & (1 << 11))
                        {
                            if (brief & 0x8000)
                                index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                            else
                                index_reg = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                        }
                        else
                        {
                            if (brief & 0x8000)
                                index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                            else
                                index_reg = RA_MapM68kRegister(&ptr, extra_reg);

                            uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                            *ptr++ = sxth(tmp3, index_reg);
#else
                            *ptr++ = sxth(tmp3, index_reg);
#endif
                            RA_FreeARMRegister(&ptr, index_reg);
                            index_reg = tmp3;
                        }
                    }

                    int8_t pc_off = 2 + (*ext_words) * 2;
                    ptr = EMIT_GetOffsetPC(ptr, &pc_off);
                    /* Check if base displacement needs to be fetched */
                    switch ((brief & M68K_EA_BD_SIZE) >> 4)
                    {
                        case 2: /* Word displacement */
                            bd_reg = RA_AllocARMRegister(&ptr);
                            *ptr++ = ldrsh_offset(REG_PC, bd_reg, pc_off);
                            (*ext_words)++;
                            break;
                        case 3: /* Long displacement */
                            bd_reg = RA_AllocARMRegister(&ptr);
                            if (pc_off & 2) {
                                *ptr++ = ldur_offset(REG_PC, bd_reg, pc_off);
                            } else {
                                *ptr++ = ldr_offset(REG_PC, bd_reg, pc_off);
                            }
                            (*ext_words) += 2;
                            break;
                    }

                    pc_off = 2 + (*ext_words) * 2;
                    ptr = EMIT_GetOffsetPC(ptr, &pc_off);

                    /* Check if outer displacement needs to be fetched */
                    switch ((brief & M68K_EA_IIS) & 3)
                    {
                        case 2: /* Word outer displacement */
                            outer_reg = RA_AllocARMRegister(&ptr);
                            *ptr++ = ldrsh_offset(REG_PC, outer_reg, pc_off);
                            (*ext_words)++;
                            break;
                        case 3: /* Long outer displacement */
                            outer_reg = RA_AllocARMRegister(&ptr);
                            if (pc_off & 2) {
                                *ptr++ = ldur_offset(REG_PC, outer_reg, pc_off);
                            }
                            else
                                *ptr++ = ldr_offset(REG_PC, outer_reg, pc_off);
                            (*ext_words) += 2;
                            break;
                    }

                    if ((brief & 0x0f) == 0)
                    {
                        /* Address register indirect with index mode */
                        if (base_reg != 0xff && bd_reg != 0xff)
                        {
#ifdef __aarch64__
                            *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                            *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                        }
                        else if (bd_reg == 0xff && base_reg != 0xff)
                        {
                            bd_reg = base_reg;
                        }
                        /*
                            Now, either base register or base displacement were given, if
                            index register was specified, use it.
                        */
                        ptr = load_reg_from_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                    }
                    else
                    {
                        if (bd_reg == 0xff)
                        {
                            bd_reg = RA_AllocARMRegister(&ptr);
                            if (base_reg == 0xff)
                                *ptr++ = mov_immed_u8(bd_reg, 0); /* upstream c0f639e: mov_reg(bd_reg, 31) */
                            else
                                *ptr++ = mov_reg(bd_reg, base_reg);
                            RA_FreeARMRegister(&ptr, base_reg);
                            base_reg = 0xff;
                        }

                        /* Postindexed mode */
                        if (brief & 0x04)
                        {
                            /* Fetch data from base reg */
                            if (base_reg == 0xff)
                                *ptr++ = ldr_offset(bd_reg, bd_reg, 0);
                            else
#ifdef __aarch64__
                                *ptr++ = ldr_regoffset(bd_reg, bd_reg, base_reg, UXTW, 0);
                            if (outer_reg != 0xff)
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                                *ptr++ = ldr_regoffset(bd_reg, bd_reg, base_reg, UXTW, 0);
                            if (outer_reg != 0xff)
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif
                            ptr = load_reg_from_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                        }
                        else /* Preindexed mode */
                        {
                            /* Fetch data from base reg with eventually applied index */
                            if (brief & M68K_EA_IS)
                            {
                                if (bd_reg == 0xff) {
                                    bd_reg = RA_AllocARMRegister(&ptr);
                                    *ptr++ = ldr_offset(base_reg, bd_reg, 0);
                                }
                                else
#ifdef __aarch64__
                                {
                                    if (base_reg == 0xff) {
                                        uint8_t t = RA_AllocARMRegister(&ptr);
                                        *ptr++ = mov_reg(t, 31);
                                        *ptr++ = ldr_regoffset(t, bd_reg, bd_reg, UXTW, 0);
                                        RA_FreeARMRegister(&ptr, t);
                                    }
                                    else {
                                        *ptr++ = ldr_regoffset(base_reg, bd_reg, bd_reg, UXTW, 0);
                                    }
                                }
                                    
#else
                                {
                                    /* upstream f3db6b0 class, A32: suppressed base -> plain [bd_reg] load */
                                    if (base_reg == 0xff)
                                        *ptr++ = ldr_offset(bd_reg, bd_reg, 0);
                                    else
                                        *ptr++ = ldr_regoffset(base_reg, bd_reg, bd_reg, UXTW, 0);
                                }
#endif
                            }
                            else
                            {
#ifdef __aarch64__
                                if (bd_reg != 0xff && base_reg != 0xff) /* upstream c0f639e */
                                    *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                                if (bd_reg != 0xff && base_reg != 0xff) /* upstream c0f639e */
                                    *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);

#endif
                                ptr = load_reg_from_addr(ptr, 4, bd_reg, bd_reg, index_reg, (brief >> 9) & 3);
                            }

                            if (outer_reg != 0xff)
#ifdef __aarch64__
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif
                            ptr = load_reg_from_addr_offset(ptr, size, bd_reg, *arm_reg, 0, 0);
                        }
                    }

                    if (bd_reg != 0xff)
                        RA_FreeARMRegister(&ptr, bd_reg);
                    if (outer_reg != 0xff)
                        RA_FreeARMRegister(&ptr, outer_reg);
                    if (base_reg != 0xff)
                        RA_FreeARMRegister(&ptr, base_reg);
                    if (index_reg != 0xff)
                        RA_FreeARMRegister(&ptr, index_reg);
                }
            }
            else if (src_reg == 0)
            {
                uint16_t lo16;
                lo16 = BE16(m68k_ptr[(*ext_words)++]);

                if (size == 0) {
                    ptr = load_s16_ext32(ptr, *arm_reg, lo16);
                }
                else
                {
                    uint8_t tmp_reg = RA_AllocARMRegister(&ptr);
                    ptr = load_s16_ext32(ptr, tmp_reg, lo16);
                    ptr = load_reg_from_addr_offset(ptr, size, tmp_reg, *arm_reg, 0, 0);
                    RA_FreeARMRegister(&ptr, tmp_reg);
                }
            }
            else if (src_reg == 1)
            {
                uint16_t hi16, lo16;
                hi16 = BE16(m68k_ptr[(*ext_words)++]);
                lo16 = BE16(m68k_ptr[(*ext_words)++]);

                if (size == 0) {
#ifdef __aarch64__
                    if (lo16 == 0 && hi16 == 0)
                    {
                        *ptr++ = mov_reg(*arm_reg, 31);
                    }
                    else if (lo16 != 0)
                    {
                        *ptr++ = movw_immed_u16(*arm_reg, lo16);
                        if (hi16 != 0 || lo16 & 0x8000)
                            *ptr++ = movt_immed_u16(*arm_reg, hi16);
                    }
                    else
                    {
                        *ptr++ = mov_immed_u16(*arm_reg, hi16, 1);
                    }
#else
                    *ptr++ = movw_immed_u16(*arm_reg, lo16);
                    if (hi16 != 0 || lo16 & 0x8000)
                        *ptr++ = movt_immed_u16(*arm_reg, hi16);
#endif
                }
#if defined(EMU68_BANK_FASTPATH) && !defined(__aarch64__)
                else if (amiga_is_fixed_bank_addr(((uint32_t)hi16 << 16) | lo16)
                         && bank_reg_ok(*arm_reg))
                {
                    ptr = emit_aerr_static(ptr, size, ((uint32_t)hi16 << 16) | lo16, 0);
                    ptr = emit_bank_load(ptr, size, ((uint32_t)hi16 << 16) | lo16, *arm_reg);
                }
#endif
                else
                {
                    uint8_t tmp_reg = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    if (lo16 == 0 && hi16 == 0)
                    {
                        *ptr++ = mov_reg(tmp_reg, 31);
                    }
                    else if (lo16 != 0)
                    {
                        *ptr++ = movw_immed_u16(tmp_reg, lo16);
                        if (hi16 != 0 || lo16 & 0x8000)
                            *ptr++ = movt_immed_u16(tmp_reg, hi16);
                    }
                    else
                    {
                        *ptr++ = mov_immed_u16(tmp_reg, hi16, 1);
                    }
#else
                    *ptr++ = movw_immed_u16(tmp_reg, lo16);
                    if (hi16 != 0 || lo16 & 0x8000)
                        *ptr++ = movt_immed_u16(tmp_reg, hi16);
#endif
                    /* absolute long: the address is a translate-time constant */
                    ptr = emit_aerr_static(ptr, size, ((uint32_t)hi16 << 16) | lo16, 0);
                    switch (size)
                    {
                        case 4:
                            *ptr++ = ldr_offset(tmp_reg, *arm_reg, 0);
                            break;
                        case 2:
                            *ptr++ = ldrh_offset(tmp_reg, *arm_reg, 0);
                            break;
                        case 1:
                            *ptr++ = ldrb_offset(tmp_reg, *arm_reg, 0);
                            break;
                    }
                    RA_FreeARMRegister(&ptr, tmp_reg);
                }
            }
            else if (src_reg == 4)
            {
                int8_t pc_off;
                uint16_t lo16, hi16, off;
                switch (size)
                {
                    case 4:
                        hi16 = BE16(m68k_ptr[(*ext_words)++]);
                        lo16 = BE16(m68k_ptr[(*ext_words)++]);
#ifdef __aarch64__
                        if (lo16 == 0 && hi16 == 0)
                        {
                            *ptr++ = mov_reg(*arm_reg, 31);
                        }
                        else if (lo16 != 0)
                        {
                            *ptr++ = movw_immed_u16(*arm_reg, lo16);
                            if (hi16 != 0 || lo16 & 0x8000)
                                *ptr++ = movt_immed_u16(*arm_reg, hi16);
                        }
                        else
                        {
                            *ptr++ = mov_immed_u16(*arm_reg, hi16, 1);
                        }
#else
                        *ptr++ = movw_immed_u16(*arm_reg, lo16);
                        if (hi16 != 0)
                            *ptr++ = movt_immed_u16(*arm_reg, hi16);
#endif
                        break;
                    case 2:
                        off = BE16(m68k_ptr[(*ext_words)++]);
                        *ptr++ = movw_immed_u16(*arm_reg, off);
                        break;
                    case 1:
                        off = BE16(m68k_ptr[(*ext_words)++]);
                        *ptr++ = mov_immed_u8(*arm_reg, off);
                        break;
                    case 0:
                        pc_off = 2 + 2*(*ext_words);
                        ptr = EMIT_GetOffsetPC(ptr, &pc_off);
                        *ptr++ = add_immed(*arm_reg, REG_PC, pc_off);
                        break;
                }
            }
        }
    }

    return ptr;
}

/*
    Emits ARM insns to load effective address and store value from specified register to the EA.

    Inputs:
        ptr     pointer to ARM instruction stream
        size    size of data for store operation, can be 4 (long), 2 (short) or 1 (byte).
                If size of 0 is specified the function does not store a value from register into
                EA but rather loads the EA into that register.
                If postincrement or predecrement modes are used and size 0 is specified, then
                the instruction translator is reponsible for increasing/decreasing the address
                register, otherwise it is done in this function!
        arm_reg ARM register to store the EA or value from EA into
        ea      EA encoded field.
        m68k_ptr pointer to m68k instruction stream past the instruction opcode itself. It may
                be increased if further bytes from m68k side are read

    Output:
        ptr     pointer to ARM instruction stream after the newly generated code
*/

uint32_t *EMIT_StoreToEffectiveAddress(uint32_t *ptr, uint8_t size, uint8_t *arm_reg, uint8_t ea, uint16_t *m68k_ptr, uint8_t *ext_words)
{
    uint8_t mode = ea >> 3;
    uint8_t src_reg = ea & 7;
    (void)ext_words;
    (void)m68k_ptr;
    if (size == 0)
        *arm_reg = RA_AllocARMRegister(&ptr);


    if (mode == 0) /* Mode 000: Dn */
    {
        uint8_t reg_dest;
        switch (size)
        {
            case 4:
#ifdef __aarch64__
                reg_dest = RA_MapM68kRegisterForWrite(&ptr, src_reg);
                *ptr++ = mov_reg(reg_dest, *arm_reg);
#else
                if (RA_IsARMRegisterMapped(*arm_reg)) {
                    reg_dest = RA_MapM68kRegisterForWrite(&ptr, src_reg);
                    *ptr++ = mov_reg(reg_dest, *arm_reg);
                }
                else
                {
                    RA_AssignM68kRegister(&ptr, src_reg, *arm_reg);
                }
#endif
                break;
            case 2:
                reg_dest = RA_MapM68kRegister(&ptr, src_reg);
                RA_SetDirtyM68kRegister(&ptr, src_reg);
                *ptr++ = bfi(reg_dest, *arm_reg, 0, 16);
                break;
            case 1:
                reg_dest = RA_MapM68kRegister(&ptr, src_reg);
                RA_SetDirtyM68kRegister(&ptr, src_reg);
                *ptr++ = bfi(reg_dest, *arm_reg, 0, 8);
                break;
            case 0:
#ifdef __aarch64__
                kprintf("Store to EA with wrong operand size 0\n");
#else
                *ptr++ = add_immed(*arm_reg, REG_CTX, __builtin_offsetof(struct M68KState, D[src_reg]));
#endif
                break;
            default:
                kprintf("Wrong size\n");
                break;
        }
    }
    else if (mode == 1) /* Mode 001: An */
    {
        uint8_t reg_dest;
        switch (size)
        {
            case 4:
#ifdef __aarch64__
                reg_dest = RA_MapM68kRegisterForWrite(&ptr, 8 + src_reg);
                *ptr++ = mov_reg(reg_dest, *arm_reg);
#else
                if (RA_IsARMRegisterMapped(*arm_reg)) {
                    reg_dest = RA_MapM68kRegisterForWrite(&ptr, 8 + src_reg);
                    *ptr++ = mov_reg(reg_dest, *arm_reg);
                }
                else
                {
                    RA_AssignM68kRegister(&ptr, 8 + src_reg, *arm_reg);
                }
#endif
                break;
            case 2:
                reg_dest = RA_MapM68kRegister(&ptr, 8 + src_reg);
                RA_SetDirtyM68kRegister(&ptr, src_reg + 8);
                *ptr++ = bfi(reg_dest, *arm_reg, 0, 16);
                break;
                ;
            case 0:
#ifdef __aarch64__
                kprintf("Store to EA with wrong operand size 0\n");
#else
                *ptr++ = add_immed(*arm_reg, REG_CTX, __builtin_offsetof(struct M68KState, A[src_reg]));
#endif
                break;
            default:
                kprintf("Wrong size\n");
                break;
        }
    }
    else
    {
        if (mode == 2) /* Mode 002: (An) */
        {
            if (size == 0) {
                uint8_t tmp = RA_MapM68kRegister(&ptr, src_reg + 8);
                *ptr++ = mov_reg(*arm_reg, tmp);
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
                ptr = store_reg_to_addr_offset(ptr, size, reg_An, *arm_reg, 0, 0);
            }
        }
        else if (mode == 3) /* Mode 003: (An)+ */
        {
            if (size == 0) {
                RA_FreeARMRegister(&ptr, *arm_reg);
                *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);

                RA_SetDirtyM68kRegister(&ptr, 8 + src_reg);
                ptr = emit_aerr_guard(ptr, size, reg_An, 0xff, 0, 0, 1);

                switch (size)
                {
                case 4:
                    *ptr++ = str_offset_postindex(reg_An, *arm_reg, 4);
                    break;
                case 2:
                    *ptr++ = strh_offset_postindex(reg_An, *arm_reg, 2);
                    break;
                case 1:
                    if (src_reg == 7)
                        *ptr++ = strb_offset_postindex(reg_An, *arm_reg, 2);
                    else
                        *ptr++ = strb_offset_postindex(reg_An, *arm_reg, 1);
                    break;
                default:
                    kprintf("Unknown size opcode\n");
                    break;
                }
            }
        }
        else if (mode == 4) /* Mode 004: -(An) */
        {
            if (size == 0) {
                RA_FreeARMRegister(&ptr, *arm_reg);
                *arm_reg = RA_MapM68kRegister(&ptr, src_reg + 8);
            }
            else
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);

                RA_SetDirtyM68kRegister(&ptr, 8 + src_reg);
                ptr = emit_aerr_guard(ptr, size, reg_An, 0xff, 0, -(int32_t)size, 1);

                switch (size)
                {
                case 4:
                    *ptr++ = str_offset_preindex(reg_An, *arm_reg, -4);
                    break;
                case 2:
                    *ptr++ = strh_offset_preindex(reg_An, *arm_reg, -2);
                    break;
                case 1:
                    if (src_reg == 7)
                        *ptr++ = strb_offset_preindex(reg_An, *arm_reg, -2);
                    else
                        *ptr++ = strb_offset_preindex(reg_An, *arm_reg, -1);
                    break;
                default:
                    kprintf("Unknown size opcode\n");
                    break;
                }
            }
        }
        else if (mode == 5) /* Mode 005: (d16, An) */
        {
            uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
            int16_t off16 = (int16_t)BE16(m68k_ptr[(*ext_words)++]);

            ptr = store_reg_to_addr_offset(ptr, size, reg_An, *arm_reg, off16, 0);
        }
        else if (mode == 6) /* Mode 006: (d8, An, Xn.SIZE*SCALE) */
        {
            uint16_t brief = BE16(m68k_ptr[(*ext_words)++]);
            uint8_t extra_reg = (brief >> 12) & 7;

            if ((brief & 0x0100) == 0)
            {
                uint8_t reg_An = RA_MapM68kRegister(&ptr, src_reg + 8);
                uint8_t tmp1 = 0xff;
                uint8_t tmp2 = 0xff;
                int8_t displ = brief & 0xff;

                if (displ > 0)
                {
                    tmp1 = RA_AllocARMRegister(&ptr);
                    *ptr++ = add_immed(tmp1, reg_An, displ);
                }
                else if (displ < 0)
                {
                    tmp1 = RA_AllocARMRegister(&ptr);
                    *ptr++ = sub_immed(tmp1, reg_An, -displ);
                }

                if (brief & (1 << 11))
                {
                    if (brief & 0x8000)
                        tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                    else
                        tmp2 = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                }
                else
                {
                    if (brief & 0x8000)
                        tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                    else
                        tmp2 = RA_MapM68kRegister(&ptr, extra_reg);

                    uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    *ptr++ = sxth(tmp3, tmp2);
#else
                    *ptr++ = sxth(tmp3, tmp2);
#endif
                    RA_FreeARMRegister(&ptr, tmp2);
                    tmp2 = tmp3;
                }

                ptr = store_reg_to_addr(ptr, size, displ ? tmp1 : reg_An, *arm_reg, tmp2, (brief >> 9) & 3);

                if (displ) RA_FreeARMRegister(&ptr, tmp1);
                RA_FreeARMRegister(&ptr, tmp2);
            }
            else
            {
                uint8_t bd_reg = 0xff;
                uint8_t outer_reg = 0xff;
                uint8_t base_reg = 0xff;
                uint8_t index_reg = 0xff;

                /* Check if base register is suppressed */
                if (!(brief & M68K_EA_BS))
                {
                    /* Base register in use. Alloc it and load its contents */
                    base_reg = RA_MapM68kRegister(&ptr, 8 + src_reg);
                }

                /* Check if index register is in use */
                if (!(brief & M68K_EA_IS))
                {
                    /* Index register in use. Alloc it and load its contents */
                    if (brief & (1 << 11))
                    {
                        if (brief & 0x8000)
                            index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                        else
                            index_reg = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                    }
                    else
                    {
                        if (brief & 0x8000)
                            index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                        else
                            index_reg = RA_MapM68kRegister(&ptr, extra_reg);

                        uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                        *ptr++ = sxth(tmp3, index_reg);
#else
                        *ptr++ = sxth(tmp3, index_reg);
#endif
                        RA_FreeARMRegister(&ptr, index_reg);
                        index_reg = tmp3;
                    }
                }
                uint16_t lo16, hi16;

                /* Check if base displacement needs to be fetched */
                switch ((brief & M68K_EA_BD_SIZE) >> 4)
                {
                case 2: /* Word displacement */
                    bd_reg = RA_AllocARMRegister(&ptr);
                    lo16 = BE16(m68k_ptr[(*ext_words)++]);
                    ptr = load_s16_ext32(ptr, bd_reg, lo16);
                    break;
                case 3: /* Long displacement */
                    bd_reg = RA_AllocARMRegister(&ptr);
                    hi16 = BE16(m68k_ptr[(*ext_words)++]);
                    lo16 = BE16(m68k_ptr[(*ext_words)++]);
                    *ptr++ = movw_immed_u16(bd_reg, lo16);
                    if (hi16)
                        *ptr++ = movt_immed_u16(bd_reg, hi16);
                    break;
                }

                /* Check if outer displacement needs to be fetched */
                switch ((brief & M68K_EA_IIS) & 3)
                {
                case 2: /* Word outer displacement */
                    outer_reg = RA_AllocARMRegister(&ptr);
                    lo16 = BE16(m68k_ptr[(*ext_words)++]);
                    ptr = load_s16_ext32(ptr, outer_reg, lo16);
                    break;
                case 3: /* Long outer displacement */
                    outer_reg = RA_AllocARMRegister(&ptr);
                    hi16 = BE16(m68k_ptr[(*ext_words)++]);
                    lo16 = BE16(m68k_ptr[(*ext_words)++]);
                    *ptr++ = movw_immed_u16(outer_reg, lo16);
                    if (hi16)
                        *ptr++ = movt_immed_u16(outer_reg, hi16);
                    break;
                }

                if ((brief & 0x0f) == 0)
                {
                    /* Address register indirect with index mode */
                    if (base_reg != 0xff && bd_reg != 0xff)
                    {
#ifdef __aarch64__
                        *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                        *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                    }
                    else if (bd_reg == 0xff && base_reg != 0xff)
                    {
                        bd_reg = base_reg;
                    }
                    /*
                        Now, either base register or base displacement were given, if
                        index register was specified, use it.
                    */

                    ptr = store_reg_to_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                }
                else
                {
                    if (bd_reg == 0xff)
                    {
                        bd_reg = RA_AllocARMRegister(&ptr);
                        if (base_reg == 0xff)
                            *ptr++ = mov_immed_u8(bd_reg, 0); /* upstream c0f639e: mov_reg(bd_reg, 31) */
                        else
                            *ptr++ = mov_reg(bd_reg, base_reg);
                        base_reg = 0xff;
                    }

                    /* Postindexed mode */
                    if (brief & 0x04)
                    {
                        /* Fetch data from base reg */
                        if (base_reg == 0xff)
                            *ptr++ = ldr_offset(bd_reg, bd_reg, 0);
                        else
#ifdef __aarch64__
                            *ptr++ = ldr_regoffset(bd_reg, bd_reg, base_reg, UXTW, 0);
                        if (outer_reg != 0xff)
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                            *ptr++ = ldr_regoffset(bd_reg, bd_reg, base_reg, UXTW, 0);
                        if (outer_reg != 0xff)
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif
                        ptr = store_reg_to_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                    }
                    else /* Preindexed mode */
                    {
                        /* Fetch data from base reg with eventually applied index */
                        if (brief & M68K_EA_IS)
                        {
                            if (bd_reg == 0xff)
                            {
                                bd_reg = RA_AllocARMRegister(&ptr);
                                *ptr++ = ldr_offset(base_reg, bd_reg, 0);
                            }
                            else
#ifdef __aarch64__
                            {
                                if (base_reg == 0xff) {
                                    uint8_t t = RA_AllocARMRegister(&ptr);
                                    *ptr++ = mov_reg(t, 31);
                                    *ptr++ = ldr_regoffset(t, bd_reg, bd_reg, UXTW, 0);
                                    RA_FreeARMRegister(&ptr, t);
                                }
                                else
                                    *ptr++ = ldr_regoffset(base_reg, bd_reg, bd_reg, UXTW, 0);
                            }
#else
                            {
                                /* upstream f3db6b0, A32: suppressed base -> plain [bd_reg] load */
                                if (base_reg == 0xff)
                                    *ptr++ = ldr_offset(bd_reg, bd_reg, 0);
                                else
                                    *ptr++ = ldr_regoffset(base_reg, bd_reg, bd_reg, UXTW, 0);
                            }
#endif
                        }
                        else
                        {
                            if (bd_reg != 0xff && base_reg != 0xff) /* upstream f3db6b0 */
#ifdef __aarch64__
                                *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                                *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                            ptr = load_reg_from_addr(ptr, 4, bd_reg, bd_reg, index_reg, (brief >> 9) & 3);
                        }

                        if (outer_reg != 0xff)
#ifdef __aarch64__
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                            *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif

                        ptr = store_reg_to_addr(ptr, size, bd_reg, *arm_reg, 0xff, 0);
                    }
                }

                if (bd_reg != 0xff)
                    RA_FreeARMRegister(&ptr, bd_reg);
                if (outer_reg != 0xff)
                    RA_FreeARMRegister(&ptr, outer_reg);
                if (base_reg != 0xff)
                    RA_FreeARMRegister(&ptr, base_reg);
                if (index_reg != 0xff)
                    RA_FreeARMRegister(&ptr, index_reg);
            }
        }
        else if (mode == 7)
        {
            if (src_reg == 2) /* (d16, PC) mode */
            {
                int8_t off = 2;
                int32_t off32 = (int16_t)BE16(m68k_ptr[(*ext_words)++]);
                ptr = EMIT_GetOffsetPC(ptr, &off);
                off32 += off;

                ptr = store_reg_to_addr_offset(ptr, size, REG_PC, *arm_reg, off32, 1);
            }
            else if (src_reg == 3)
            {
                uint16_t brief = BE16(m68k_ptr[(*ext_words)++]);
                uint8_t extra_reg = (brief >> 12) & 7;

                if ((brief & 0x0100) == 0)
                {
                    uint8_t tmp1 = RA_AllocARMRegister(&ptr);
                    uint8_t tmp2 = 0xff;
                    int8_t displ = brief & 0xff;
                    int8_t off = 2;
                    int16_t full_off = 0;
                    ptr = EMIT_GetOffsetPC(ptr, &off);

                    full_off = off + displ;

                    if (full_off >= 0)
                    {
                        *ptr++ = add_immed(tmp1, REG_PC, full_off);
                    }
                    else
                    {
                        *ptr++ = sub_immed(tmp1, REG_PC, -full_off);
                    }

                    if (brief & (1 << 11))
                    {
                        if (brief & 0x8000)
                            tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                        else
                            tmp2 = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                    }
                    else
                    {
                        if (brief & 0x8000)
                            tmp2 = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                        else
                            tmp2 = RA_MapM68kRegister(&ptr, extra_reg);

                        uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                        *ptr++ = sxth(tmp3, tmp2);
#else
                        *ptr++ = sxth(tmp3, tmp2);
#endif
                        RA_FreeARMRegister(&ptr, tmp2);
                        tmp2 = tmp3;
                    }

                    ptr = store_reg_to_addr(ptr, size, tmp1, *arm_reg, tmp2, (brief >> 9) & 3);

                    RA_FreeARMRegister(&ptr, tmp1);
                    RA_FreeARMRegister(&ptr, tmp2);
                }
                else
                {
                    uint8_t bd_reg = 0xff;
                    uint8_t outer_reg = 0xff;
                    uint8_t base_reg = 0xff;
                    uint8_t index_reg = 0xff;

                    /* Check if base register is suppressed */
                    if (!(brief & M68K_EA_BS))
                    {
                        /* Base register in use. Alloc it and load its contents */
                        base_reg = RA_AllocARMRegister(&ptr);
                        int8_t off = 2;
                        //ptr = EMIT_FlushPC(ptr);
                        ptr = EMIT_GetOffsetPC(ptr, &off);
                        if (off > 0)
                            *ptr++ = add_immed(base_reg, REG_PC, off);
                        else
                            *ptr++ = sub_immed(base_reg, REG_PC, -off);

                        //*ptr++ = add_immed(base_reg, REG_PC, 2);
                    }

                    /* Check if index register is in use */
                    if (!(brief & M68K_EA_IS))
                    {
                        /* Index register in use. Alloc it and load its contents */
                        if (brief & (1 << 11))
                        {
                            if (brief & 0x8000)
                                index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg); // RA_CopyFromM68kRegister(&ptr, 8 + extra_reg);
                            else
                                index_reg = RA_MapM68kRegister(&ptr, extra_reg); // RA_CopyFromM68kRegister(&ptr, extra_reg);
                        }
                        else
                        {
                            if (brief & 0x8000)
                                index_reg = RA_MapM68kRegister(&ptr, 8 + extra_reg);
                            else
                                index_reg = RA_MapM68kRegister(&ptr, extra_reg);

                            uint8_t tmp3 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                            *ptr++ = sxth(tmp3, index_reg);
#else
                            *ptr++ = sxth(tmp3, index_reg);
#endif
                            RA_FreeARMRegister(&ptr, index_reg);
                            index_reg = tmp3;
                        }
                    }

                    uint16_t lo16, hi16;

                    /* Check if base displacement needs to be fetched */
                    switch ((brief & M68K_EA_BD_SIZE) >> 4)
                    {
                        case 2: /* Word displacement */
                            bd_reg = RA_AllocARMRegister(&ptr);
                            lo16 = BE16(m68k_ptr[(*ext_words)++]);
                            ptr = load_s16_ext32(ptr, bd_reg, lo16);
                            break;
                        case 3: /* Long displacement */
                            bd_reg = RA_AllocARMRegister(&ptr);
                            hi16 = BE16(m68k_ptr[(*ext_words)++]);
                            lo16 = BE16(m68k_ptr[(*ext_words)++]);
                            *ptr++ = movw_immed_u16(bd_reg, lo16);
                            if (hi16)
                                *ptr++ = movt_immed_u16(bd_reg, hi16);
                            break;
                    }

                    /* Check if outer displacement needs to be fetched */
                    switch ((brief & M68K_EA_IIS) & 3)
                    {
                        case 2: /* Word outer displacement */
                            outer_reg = RA_AllocARMRegister(&ptr);
                            lo16 = BE16(m68k_ptr[(*ext_words)++]);
                            ptr = load_s16_ext32(ptr, outer_reg, lo16);
                            break;
                        case 3: /* Long outer displacement */
                            outer_reg = RA_AllocARMRegister(&ptr);
                            hi16 = BE16(m68k_ptr[(*ext_words)++]);
                            lo16 = BE16(m68k_ptr[(*ext_words)++]);
                            *ptr++ = movw_immed_u16(outer_reg, lo16);
                            if (hi16)
                                *ptr++ = movt_immed_u16(outer_reg, hi16);
                            break;
                    }

                    if ((brief & 0x0f) == 0)
                    {
                        /* Address register indirect with index mode */
                        if (base_reg != 0xff && bd_reg != 0xff)
                        {
#ifdef __aarch64__
                            *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                            *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                        }
                        else if (bd_reg == 0xff && base_reg != 0xff)
                        {
                            bd_reg = base_reg;
                        }
                        /*
                            Now, either base register or base displacement were given, if
                            index register was specified, use it.
                        */
                        ptr = store_reg_to_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                    }
                    else
                    {
                        if (bd_reg == 0xff)
                        {
                            bd_reg = RA_AllocARMRegister(&ptr);
                            if (base_reg == 0xff)
                                *ptr++ = mov_immed_u8(bd_reg, 0); /* upstream c0f639e: mov_reg(bd_reg, 31) */
                            else
                                *ptr++ = mov_reg(bd_reg, base_reg);
                            base_reg = 0xff;
                        }

                        /* Postindexed mode */
                        if (brief & 0x04)
                        {
                            /* Fetch data from base reg */
                            if (base_reg == 0xff)
                                *ptr++ = ldr_offset(bd_reg, bd_reg, 0);
                            else
                                ptr = load_reg_from_addr(ptr, 4, bd_reg, bd_reg, base_reg, 0);

                            if (outer_reg != 0xff)
#ifdef __aarch64__
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif

                            ptr = store_reg_to_addr(ptr, size, bd_reg, *arm_reg, index_reg, (brief >> 9) & 3);
                        }
                        else /* Preindexed mode */
                        {
                            /* Fetch data from base reg with eventually applied index */
                            if (brief & M68K_EA_IS)
                            {
                                if (bd_reg == 0xff) {
                                    bd_reg = RA_AllocARMRegister(&ptr);
                                    *ptr++ = ldr_offset(base_reg, bd_reg, 0);
                                }
                                else
                                    ptr = load_reg_from_addr(ptr, 4, base_reg, bd_reg, bd_reg, 0);
                            }
                            else
                            {
                                if (bd_reg != 0xff && base_reg != 0xff) /* upstream 88b7d9a class */
#ifdef __aarch64__
                                    *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#else
                                    *ptr++ = add_reg(bd_reg, base_reg, bd_reg, LSL, 0);
#endif
                                ptr = load_reg_from_addr(ptr, 4, bd_reg, bd_reg, index_reg, (brief >> 9) & 3);
                            }

                            if (outer_reg != 0xff)
#ifdef __aarch64__
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#else
                                *ptr++ = add_reg(bd_reg, bd_reg, outer_reg, LSL, 0);
#endif
                            ptr = store_reg_to_addr(ptr, size, bd_reg, *arm_reg, 0xff, 0);
                        }
                    }

                    if (bd_reg != 0xff)
                        RA_FreeARMRegister(&ptr, bd_reg);
                    if (outer_reg != 0xff)
                        RA_FreeARMRegister(&ptr, outer_reg);
                    if (base_reg != 0xff)
                        RA_FreeARMRegister(&ptr, base_reg);
                    if (index_reg != 0xff)
                        RA_FreeARMRegister(&ptr, index_reg);
                }
            }
            else if (src_reg == 0)
            {
                uint16_t lo16;
                lo16 = BE16(m68k_ptr[(*ext_words)++]);

                if (size == 0) {
                    ptr = load_s16_ext32(ptr, *arm_reg, lo16);
                }
                else
                {
                    uint8_t tmp_reg = RA_AllocARMRegister(&ptr);
                    ptr = load_s16_ext32(ptr, tmp_reg, lo16);
                    ptr = store_reg_to_addr(ptr, size, tmp_reg, *arm_reg, 0xff, 0);
                    RA_FreeARMRegister(&ptr, tmp_reg);
                }
            }
            else if (src_reg == 1)
            {
                uint16_t lo16, hi16;
                hi16 = BE16(m68k_ptr[(*ext_words)++]);
                lo16 = BE16(m68k_ptr[(*ext_words)++]);

                if (size == 0) {
#ifdef __aarch64__
                    if (lo16 == 0 && hi16 == 0)
                    {
                        *ptr++ = mov_reg(*arm_reg, 31);
                    }
                    else if (lo16 != 0)
                    {
                        *ptr++ = movw_immed_u16(*arm_reg, lo16);
                        if (hi16 != 0 || lo16 & 0x8000)
                            *ptr++ = movt_immed_u16(*arm_reg, hi16);
                    }
                    else
                    {
                        *ptr++ = mov_immed_u16(*arm_reg, hi16, 1);
                    }
#else
                    *ptr++ = movw_immed_u16(*arm_reg, lo16);
                    if (hi16 != 0 || lo16 & 0x8000)
                        *ptr++ = movt_immed_u16(*arm_reg, hi16);
#endif
                //    *ptr++ = ldr_offset(REG_PC, *arm_reg, pc_off);
                }
#if defined(EMU68_BANK_FASTPATH) && !defined(__aarch64__)
                else if (amiga_is_fixed_bank_addr(((uint32_t)hi16 << 16) | lo16)
                         && bank_reg_ok(*arm_reg))
                {
                    ptr = emit_aerr_static(ptr, size, ((uint32_t)hi16 << 16) | lo16, 1);
                    ptr = emit_bank_store(ptr, size, ((uint32_t)hi16 << 16) | lo16, *arm_reg);
                }
#endif
                else
                {
                    uint8_t tmp_reg = RA_AllocARMRegister(&ptr);
                    //*ptr++ = ldr_offset(REG_PC, tmp_reg, pc_off);
#ifdef __aarch64__
                    if (lo16 == 0 && hi16 == 0)
                    {
                        *ptr++ = mov_reg(tmp_reg, 31);
                    }
                    else if (lo16 != 0)
                    {
                        *ptr++ = movw_immed_u16(tmp_reg, lo16);
                        if (hi16 != 0 || lo16 & 0x8000)
                            *ptr++ = movt_immed_u16(tmp_reg, hi16);
                    }
                    else
                    {
                        *ptr++ = mov_immed_u16(tmp_reg, hi16, 1);
                    }
#else
                    *ptr++ = movw_immed_u16(tmp_reg, lo16);
                    if (hi16 != 0 || lo16 & 0x8000)
                        *ptr++ = movt_immed_u16(tmp_reg, hi16);
#endif
                    ptr = store_reg_to_addr(ptr, size, tmp_reg, *arm_reg, 0xff, 0);

                    RA_FreeARMRegister(&ptr, tmp_reg);
                }
            }
        }
    }

    return ptr;
}
