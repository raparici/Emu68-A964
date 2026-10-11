/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* ARMv7 exception vectors and the data-abort handler that routes guest accesses to the chipset and the Zorro boards. */

#include <stdint.h>
#include "support.h"
#include "M68k.h"
#include "amiga_memmap.h"

uint32_t emu68_fastbank_call4(void *fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void     kprintf_fb(const char *fmt, ...);

#if !defined(CHIPSET_AXI)
volatile unsigned int emu68_health_scratch[0x1800];
#endif

#ifndef LE32
#define LE32(x) __builtin_bswap32(x)   /* BE8 host: convert stored-LE insn word */
#endif

extern unsigned int ps_read_8(unsigned int address);
extern unsigned int ps_read_16(unsigned int address);
extern unsigned int ps_read_32(unsigned int address);
extern void ps_write_8(unsigned int address, unsigned int data);
extern void ps_write_16(unsigned int address, unsigned int data);
extern void ps_write_32(unsigned int address, unsigned int data);

extern void arm_flush_cache(uintptr_t addr, uint32_t length);      /* DCCIMVAC: clean+inval */
extern void arm_dcache_invalidate(uintptr_t addr, uint32_t length); /* DCIMVAC: inval ONLY  */

volatile uint32_t emu68_dabt_frame_ext[2];
volatile uint32_t emu68_dabt_banked_valid;

__attribute__((naked)) void emu68_dabt_read_banked(uint32_t *out)
{
    asm volatile(
    "   mrs     r2, spsr                \n"
    "   and     r2, r2, #0x1f           \n"
    "   cmp     r2, #0x10               \n" /* USER? -> SYS bank        */
    "   orreq   r2, r2, #0x0f           \n"
    "   mrs     r3, cpsr                \n"
    "   bic     r12, r3, #0x1f          \n"
    "   orr     r12, r12, r2            \n"
    "   msr     cpsr_c, r12             \n" /* -> faulting mode         */
    "   mov     r1, sp                  \n"
    "   mov     r2, lr                  \n"
    "   msr     cpsr_c, r3              \n" /* -> back to ABT           */
    "   stmia   r0, {r1, r2}            \n"
    "   bx      lr                      \n");
}
__attribute__((naked)) void emu68_dabt_write_banked(const uint32_t *in)
{
    asm volatile(
    "   ldmia   r0, {r1, r2}            \n"
    "   mrs     r3, spsr                \n"
    "   and     r3, r3, #0x1f           \n"
    "   cmp     r3, #0x10               \n"
    "   orreq   r3, r3, #0x0f           \n"
    "   mrs     r12, cpsr               \n"
    "   bic     r0, r12, #0x1f          \n"
    "   orr     r0, r0, r3              \n"
    "   msr     cpsr_c, r0              \n" /* -> faulting mode         */
    "   mov     sp, r1                  \n"
    "   mov     lr, r2                  \n"
    "   msr     cpsr_c, r12             \n" /* -> back to ABT           */
    "   bx      lr                      \n");
}

static inline uint32_t dabt_reg_get(uint32_t *regs, uint32_t r)
{
    if (r <= 12) return regs[r];
    if (!emu68_dabt_banked_valid) {
        emu68_dabt_read_banked((uint32_t *)emu68_dabt_frame_ext);
        emu68_dabt_banked_valid = 1;
    }
    return emu68_dabt_frame_ext[r - 13];
}
static inline void dabt_reg_set(uint32_t *regs, uint32_t r, uint32_t v)
{
    if (r <= 12) { regs[r] = v; return; }
    if (!emu68_dabt_banked_valid) {
        emu68_dabt_read_banked((uint32_t *)emu68_dabt_frame_ext);
        emu68_dabt_banked_valid = 1;
    }
    emu68_dabt_frame_ext[r - 13] = v;
    emu68_dabt_write_banked((const uint32_t *)emu68_dabt_frame_ext);
}

#if defined(CHIPSET_AXI)
#include <boards.h>
int board_idx = 0;
struct ExpansionBoard **board = NULL;
extern struct ExpansionBoard *__boards_start;
#ifdef EMU68_BIGMIGHD
extern uint32_t g_bigmighd_doorbell_base;
extern uint32_t g_bigmighd_rom_base;      /* the RO driver-ROM megabyte; 0 until map() runs */
static uint32_t g_bigmighd_pending, g_bigmighd_pbuf, g_bigmighd_plen, g_bigmighd_pcmd;
#ifdef EMU68_SHARE_DOORBELL
static uint32_t g_share_db_pending, g_share_db_buf, g_share_db_len, g_share_db_cmd;
#endif
#endif
#ifdef EMU68_ZZ9000
extern uint32_t g_zz9000_regbase;
extern uint32_t zz9000_mmio(uint32_t off, int is_load, unsigned size,
                            uint32_t wval);
#ifdef EMU68_A3D
extern uint32_t zz9000_mmio_ap(uint32_t off, int is_load, unsigned size, uint32_t wval);
#define ZZ9000_MMIO_READ zz9000_mmio_ap
#else
#define ZZ9000_MMIO_READ zz9000_mmio
#endif
extern int zz9000_vram_dma(uint32_t guest, uint32_t len, uint32_t *phys, uintptr_t *maint);
extern int zz9000_vram_window(uint32_t *lo, uint32_t *hi);
#endif
#ifdef EMU68_UAESND
extern uint32_t g_uaesnd_win;
extern uint32_t uaesnd_mmio(uint32_t off, int is_load, unsigned size,
                            uint32_t wval);
#endif
#else
int board_idx = 0;
void **board = 0;
#endif

#define IRQ_VECTOR_SYM "__arm_unhandled_irq"
#ifdef EMU68_OMMX
#define OMMX_DABT_SAVE    "       vpush   {d16-d31}                       \n"
#define OMMX_DABT_RESTORE "       vpop    {d16-d31}                       \n"
#else
#define OMMX_DABT_SAVE    ""
#define OMMX_DABT_RESTORE ""
#endif
asm(
"       .section .vectors                       \n"
"       .balign 0x20                            \n"
"       .globl __vectors_start                  \n"
"__vectors_start:                               \n"
"       ldr     pc, .L_reset                    \n" /* 0x00 Reset            */
"       ldr     pc, .L_undef                    \n" /* 0x04 Undefined        */
"       ldr     pc, .L_svc                      \n" /* 0x08 Supervisor Call  */
"       ldr     pc, .L_pabt                     \n" /* 0x0C Prefetch Abort   */
"       ldr     pc, .L_dabt                     \n" /* 0x10 Data Abort       */
"       nop                                     \n" /* 0x14 (reserved)       */
"       ldr     pc, .L_irq                      \n" /* 0x18 IRQ              */
"       ldr     pc, .L_fiq                      \n" /* 0x1C FIQ              */
".L_reset: .word __arm_unhandled_reset          \n"
".L_undef: .word __arm_undef_entry              \n"
".L_svc:   .word __arm_unhandled_svc            \n"
".L_pabt:  .word __arm_unhandled_pabt           \n"
".L_dabt:  .word __arm_data_abort_entry         \n"
".L_irq:   .word " IRQ_VECTOR_SYM "             \n"
".L_fiq:   .word __arm_unhandled_fiq            \n"
"                                               \n"
"       .section .text                          \n"
"       .balign 4                               \n"
"       .globl __arm_data_abort_entry           \n"
"       .type  __arm_data_abort_entry,%function \n"
"__arm_data_abort_entry:                        \n"
"       sub     lr, lr, #4                      \n"
"       push    {r0-r12, lr}                    \n"
"       mov     r0, sp                          \n" /* r0 -> saved-reg array    */
"       bl      SYSDataAbortHandler             \n"
"       pop     {r0-r12, lr}                    \n"
"       movs    pc, lr                          \n"
"                                               \n"
"       .globl __arm_unhandled_undef            \n"
"__arm_unhandled_undef:  mov r0, #1 ; b __arm_panic_common \n"
"       .globl __arm_undef_entry                \n"
"       .type  __arm_undef_entry,%function      \n"
"__arm_undef_entry:                             \n" /* UNDEF: full frame so the  */
"       push    {r0-r12, lr}                    \n" /* handler can identify the  */
"       mov     r0, sp                          \n" /* udf word + m68k PC (r12)  */
"       bl      SYSUndefHandler                 \n"
"       .globl __arm_unhandled_pabt             \n"
"__arm_unhandled_pabt:   sub lr, lr, #4 ; mov r0, #3 ; b __arm_panic_common \n"
"       .globl __arm_unhandled_reset            \n"
"__arm_unhandled_reset:  mov r0, #0 ; b __arm_panic_common \n"
"       .globl __arm_unhandled_svc              \n"
"__arm_unhandled_svc:    mov r0, #2 ; b __arm_panic_common \n"
"       .globl __arm_unhandled_irq              \n"
"__arm_unhandled_irq:    sub lr, lr, #4 ; mov r0, #6 ; b __arm_panic_common \n"
"       .globl __arm_unhandled_fiq              \n"
"__arm_unhandled_fiq:    sub lr, lr, #4 ; mov r0, #7 ; b __arm_panic_common \n"
"__arm_panic_common:                             \n"
"       mov     r1, lr ; mrs r2, spsr ; b SYSUnhandledException \n"
"       .ltorg                                  \n"
);

asm(
"       .section .text                          \n"
"       .balign 4                               \n"
"       .fpu    neon                            \n"   /* this TU is -mgeneral-regs-only: enable the bank for the assembler */
"       .globl emu68_fastbank_call4             \n"
"       .type  emu68_fastbank_call4,%function   \n"
"emu68_fastbank_call4:                          \n"
"       push    {r4, lr}                        \n"
OMMX_DABT_SAVE
"       mov     ip, r0                          \n"
"       mov     r0, r1                          \n"
"       mov     r1, r2                          \n"
"       mov     r2, r3                          \n"
#ifdef EMU68_OMMX
"       ldr     r3, [sp, #136]                  \n"
#else
"       ldr     r3, [sp, #8]                    \n"
#endif
"       blx     ip                              \n"
OMMX_DABT_RESTORE
"       pop     {r4, pc}                        \n"
);
uint32_t emu68_fastbank_call4(void *fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

/* kprintf with the fast bank saved: the formatter (int_itoa/int_ftoa) is NEON code. */
#include <stdarg.h>
__attribute__((noinline)) void kprintf_fb(const char *fmt, ...)
{
#ifdef EMU68_OMMX
    uint64_t fb[16] __attribute__((aligned(8)));
    asm volatile(".fpu neon\n vstmia %0, {d16-d31}" :: "r"(fb) : "memory");
#endif
    va_list ap;
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
#ifdef EMU68_OMMX
    asm volatile(".fpu neon\n vldmia %0, {d16-d31}" :: "r"(fb) : "memory");
#endif
}

static const char *const exc_names[8] = {
    "Reset", "Undefined", "SupervisorCall", "PrefetchAbort",
    "DataAbort", "(reserved)", "IRQ", "FIQ"
};

static void __attribute__((noreturn)) emu68_park(uint32_t kind_fs, uint32_t a,
                                                 uint32_t b, uint32_t c, uint32_t d)
{
    (void)kind_fs; (void)a; (void)b; (void)c; (void)d;
    for (;;) { asm volatile("wfe"); }
}

void __attribute__((noreturn)) SYSUndefHandler(uint32_t *regs)
{
    uint32_t lr = regs[13];
    uint32_t spsr;
    asm volatile("mrs %0, spsr" : "=r"(spsr));
    uint32_t insn = 0;
    if (lr >= 0x1000)                     /* guarded: don't re-fault      */
        insn = LE32(*(volatile uint32_t *)(lr - 4));
    emu68_park(0x11u, insn, lr - 4, regs[12], spsr);
}

void SYSUnhandledException(uint32_t vector, uint32_t lr, uint32_t spsr)
{
    extern struct M68KState *__m68k_state;
    kprintf_fb("\n[JIT:EXC] *** Unhandled ARM exception: %s ***\n",
            (vector < 8) ? exc_names[vector] : "?");
    kprintf_fb("[JIT:EXC]   return-LR=%08x  SPSR=%08x\n", lr, spsr);
    if (__m68k_state) {
        /* PC is stored big-endian; on this BE8 host a plain read IS the value. */
        kprintf_fb("[JIT:EXC]   m68k PC=%08x  (faulting ARM insn @ %08x = %08x)\n",
                (uint32_t)__m68k_state->PC, lr - 4, LE32(*(volatile uint32_t *)(lr - 4)));
    }
    if (vector == 3) {  /* prefetch abort: report IFAR/IFSR */
        uint32_t ifar, ifsr;
        asm volatile("mrc p15,0,%0,c6,c0,2" : "=r"(ifar));
        asm volatile("mrc p15,0,%0,c5,c0,1" : "=r"(ifsr));
        kprintf_fb("[JIT:EXC]   IFAR=%08x  IFSR=%08x\n", ifar, ifsr);
    }
    for (;;) { asm volatile("wfe"); }
}

void SYSDataAbortHandler(uint32_t *regs);

static inline int is_chipset_region(uint32_t far)
{
    return amiga_is_seam_addr(far);
}

static int emu68_input_shim = 1;

void SYSDataAbortHandler(uint32_t *regs)
{
    uint32_t dfar, dfsr;
    emu68_dabt_banked_valid = 0;    /* lazy r13/r14 cache: fresh per abort */
    asm volatile("mrc p15,0,%0,c6,c0,0" : "=r"(dfar));   /* DFAR */
    asm volatile("mrc p15,0,%0,c5,c0,0" : "=r"(dfsr));   /* DFSR */

    uint32_t pc_ret = regs[13];                 /* next instruction          */
    uint32_t insn   = LE32(*(volatile uint32_t *)(pc_ret - 4));

#ifdef EMU68_UAESND
    {
        extern uint32_t g_uaesnd_staging, g_uaesnd_active;
        static uint32_t stage_lifeline_decim;
        if (g_uaesnd_staging && g_uaesnd_active &&
            ((++stage_lifeline_decim & 63u) == 0u)) {
            extern struct uaesnd_card g_uaesnd;
            extern void uaesnd_stage_tick(struct uaesnd_card *c);
            extern volatile uint32_t g_uaesnd_stage_in_trap;
            g_uaesnd_stage_in_trap = 1u;
            uaesnd_stage_tick(&g_uaesnd);
            g_uaesnd_stage_in_trap = 0u;
        }
    }
#endif

    uint32_t fs = (dfsr & 0xf) | ((dfsr >> 6) & 0x10);

#ifdef EMU68_BIGMIGHD
#endif

    if ((fs == 0x05 || fs == 0x07 || fs == 0x01) && dfar >= 0x01000000u && dfar < 0x28000000u)
        dfar &= 0x00FFFFFFu;

    int chipset   = is_chipset_region(dfar) && (fs == 0x05 || fs == 0x07 || fs == 0x01);
    int no_memory = (fs == 0x05 || fs == 0x07 || fs == 0x08 || fs == 0x01);
#ifdef EMU68_CHIP_WINDOW
    if (dfar < 0x00200000u) {
        if ((fs == 0x0D || fs == 0x0F) && (dfsr & (1u << 11))) chipset = 1;
        if (fs == 0x08) chipset = 1;
    }
#endif

    int rom_write = ((dfar >> 20) == SEC_ROM) &&
                    (fs == 0x0D || fs == 0x0F) && (dfsr & (1u << 11));
#ifdef EMU68_BIGMIGHD
    if (!rom_write && g_bigmighd_rom_base &&
        (dfar >> 20) == (g_bigmighd_rom_base >> 20) &&
        (fs == 0x0D || fs == 0x0F) && (dfsr & (1u << 11))) {
        rom_write = 1;
    }
#endif
    const int z3ro = 0;
    if (!chipset && !no_memory && !rom_write && !z3ro) {
#ifdef EMU68_A3D
        {
            extern volatile uint32_t g_a3d_prot_on;
#ifdef EMU68_ZZ9000
            extern int a3d_fw_prot_fault_ap(uint32_t dfar);
#define a3d_fw_prot_fault a3d_fw_prot_fault_ap
#else
            extern int a3d_fw_prot_fault(uint32_t dfar);
#endif
            if (g_a3d_prot_on && (fs == 0x0D || fs == 0x0F) && a3d_fw_prot_fault(dfar)) {
                regs[13] = pc_ret - 4;
                return;
            }
        }
#ifdef EMU68_ZZ9000
#undef a3d_fw_prot_fault
#endif
#endif
        emu68_park(1u | (fs << 8), dfar, insn, pc_ret - 4, regs[12]);
    }

    int      is_load, size, sign_ext, rt, rn, p_idx, u_add, wback;
    uint32_t wb_offset = 0;                     /* byte magnitude for writeback */

    if ((insn & 0x0c000000) == 0x04000000) {
        /* Single data transfer: LDR/STR/LDRB/STRB (A1 encoding). */
        int imm_form = ((insn >> 25) & 1) == 0; /* I bit: 0 = immediate offset */
        p_idx   = (insn >> 24) & 1;             /* P */
        u_add   = (insn >> 23) & 1;             /* U */
        int b   = (insn >> 22) & 1;             /* B: byte/word */
        wback   = (insn >> 21) & 1;
        is_load = (insn >> 20) & 1;             /* L */
        rn      = (insn >> 16) & 0xf;
        rt      = (insn >> 12) & 0xf;
        size    = b ? 1 : 4;
        sign_ext = 0;
        if (imm_form)
            wb_offset = insn & 0xfff;
        else if (!p_idx || wback)
            goto unhandled;
    }
    else if ((insn & 0x0e000000) == 0x00000000 &&
             (insn & 0x00000090) == 0x00000090) {
        /* Extra load/store: LDRH/STRH/LDRSB/LDRSH (A1 encoding, bit7=bit4=1). */
        int s   = (insn >> 6) & 1;              /* S: signed                  */
        int h   = (insn >> 5) & 1;              /* H: halfword                */
        if (s == 0 && h == 0) goto unhandled;   /* 00 = SWP/mul, not a transfer */
        p_idx   = (insn >> 24) & 1;
        u_add   = (insn >> 23) & 1;
        int imm_form = (insn >> 22) & 1;        /* bit22: 1 = immediate       */
        wback   = (insn >> 21) & 1;
        is_load = (insn >> 20) & 1;
        if (!is_load && s) goto unhandled;
        rn      = (insn >> 16) & 0xf;
        rt      = (insn >> 12) & 0xf;
        size    = h ? 2 : 1;
        sign_ext = s;                           /* LDRSB/LDRSH                 */
        if (imm_form)
            wb_offset = ((insn >> 8) & 0xf) << 4 | (insn & 0xf);
    }
    else {
unhandled:
        emu68_park(2u | ((dfsr & 0xff) << 8), dfar, insn, pc_ret - 4, regs[12]);
    }

    if (rt == 15 || rn == 15) {
        emu68_park(3u | (rt << 8) | (rn << 16), dfar, insn, pc_ret - 4, regs[12]);
    }

    if (rom_write && is_load) {
        emu68_park(4u, dfar, insn, pc_ret - 4, regs[12]);
    }

    if (!chipset) {
#if defined(CHIPSET_AXI)
        if (dfar >= 0xe80000 && dfar <= 0xe8ffff && board != NULL) {
            if (is_load) {
                uint32_t v = 0xffu;
                if (size == 1) {
                    while (board[board_idx] && !board[board_idx]->enabled)
                        board_idx++;
                    if (board[board_idx])
                        v = ((const uint8_t *)board[board_idx]->rom_file)[dfar - 0xe80000];
                }
                dabt_reg_set(regs, rt, v);
            } else if (board[board_idx]) {
                uint32_t v = dabt_reg_get(regs, rt);
                if (board[board_idx]->is_z3) {
                    if (dfar == 0xe80044) {
                        board[board_idx]->map_base = (v & 0xffff) << 16;
                        (void)emu68_fastbank_call4((void *)board[board_idx]->map, (uint32_t)(uintptr_t)board[board_idx], 0, 0, 0);
                        board_idx++;
                    }
                } else if (dfar == 0xe80048) {
                    board[board_idx]->map_base = (v & 0xff) << 16;
                    (void)emu68_fastbank_call4((void *)board[board_idx]->map, (uint32_t)(uintptr_t)board[board_idx], 0, 0, 0);
                    board_idx++;
                }
                if (dfar == 0xe8004c || dfar == 0xe8004e)
                    board_idx++;
            }
        } else
#endif
#ifdef EMU68_ZZ9000
        if (g_zz9000_regbase && dfar - g_zz9000_regbase < 0x10000u) {
            uint32_t zoff = dfar - g_zz9000_regbase;
            if (is_load) {
                uint32_t v = emu68_fastbank_call4((void *)ZZ9000_MMIO_READ, zoff, 1, size, 0);
                if (sign_ext) {
                    if (size == 1)      v = (uint32_t)(int32_t)(int8_t)v;
                    else if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                }
                dabt_reg_set(regs, rt, v);
            } else {
                (void)emu68_fastbank_call4((void *)zz9000_mmio, zoff, 0, size, dabt_reg_get(regs, rt));
            }
        } else
#endif
#ifdef EMU68_UAESND
        if (g_uaesnd_win && dfar - g_uaesnd_win < 0x10000u) {
            uint32_t uoff = dfar - g_uaesnd_win;
            if (is_load) {
                uint32_t v = uaesnd_mmio(uoff, 1, size, 0);
                if (sign_ext) {
                    if (size == 1)      v = (uint32_t)(int32_t)(int8_t)v;
                    else if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                }
                dabt_reg_set(regs, rt, v);
            } else {
                uaesnd_mmio(uoff, 0, size, dabt_reg_get(regs, rt));
            }
        } else
#endif
#ifdef EMU68_BIGMIGHD
        if (g_bigmighd_doorbell_base &&
            dfar >= g_bigmighd_doorbell_base &&
            dfar <  g_bigmighd_doorbell_base + 0x00100000u) {
            volatile uint32_t *box = (volatile uint32_t *)HDFBOX_FW_PTR;

            if (!is_load) {
                uint32_t cmd_addr = dabt_reg_get(regs, rt);
                volatile uint32_t *c = (volatile uint32_t *)cmd_addr;

                g_bigmighd_pending = 0;

                if (cmd_addr < 0x00200000u || (cmd_addr & 3) ||
                    c[0] != HDFBOX_MAGIC || (c[1] >> 16) != 1u) {
                } else {
                    uint32_t command = c[1] & 0xFFFFu;
                    uint32_t buf     = c[5];
                    uint32_t xshift  = ((c[2] >> 16) >= 4u) ? 11u : 9u;
                    uint32_t len     = c[4] << xshift;
                    uint32_t phys    = 0;
                    uintptr_t maint  = buf;
                    int ok = 1;

                    if (command == 1u || command == 2u) {
                        if (buf >= 0x40000000u && (uint64_t)buf + len <= 0x50000000u)
                            phys = Z3FAST_DDR_BASE + (buf - 0x40000000u);
                        else if (buf >= 0x00200000u && (uint64_t)buf + len <= 0x00A00000u)
                            phys = FASTRAM_DDR_BASE + (buf - 0x00200000u);
#ifdef EMU68_ZZ9000
                        else if (zz9000_vram_dma(buf, len, &phys, &maint)) {
                        }
#endif
                        else { ok = 0; }

                        if (ok && (command == 1u || command == 2u))
                            arm_flush_cache(maint, len);
                    }

                    if (ok) {
                        box[3] = command;  box[4] = c[2] >> 16;   /* command, unit  */
                        box[5] = c[3];     box[6] = c[4];         /* block, count   */
                        box[7] = phys;
                        box[8] = 0xFFFFu;  box[9] = 0;            /* status, actual */

                        asm volatile("dsb" ::: "memory");
                        box[1] = box[1] + 1u;
                        asm volatile("dsb" ::: "memory");

                        g_bigmighd_pending = cmd_addr;
                        g_bigmighd_pbuf    = (uint32_t)maint;
                        g_bigmighd_plen    = len;
                        g_bigmighd_pcmd    = command;
                    }
                }
            } else {
                uint32_t st = 0xFFFFu;                  /* busy */

                if (g_bigmighd_pending)

                if (g_bigmighd_pending && box[2] == box[1]) {
                    volatile uint32_t *c = (volatile uint32_t *)g_bigmighd_pending;

                    if (g_bigmighd_pcmd == 1u && g_bigmighd_plen)
                        arm_dcache_invalidate((uintptr_t)g_bigmighd_pbuf, g_bigmighd_plen);

                    st   = box[8];
                    c[6] = box[9];                              /* hc_Actual        */

                    if (g_bigmighd_pcmd == 0u) {                /* IDENTIFY geometry */
                        uint32_t n = 0;
                        for (int i = 0; i < 4; i++) {
                            c[8 + i]  = box[10 + i];            /* hc_UnitBlocks[]  */
                            c[12 + i] = box[14 + i];            /* hc_UnitFlags[]   */
                            if (box[10 + i]) n++;
                        }
                        c[7] = n;                               /* hc_UnitCount     */
                        for (int i = 0; i < 13; i++)
                            c[16 + i] = box[18 + i];
#ifdef EMU68_ZZ9000
                        {
                            uint32_t vlo = 0, vhi = 0;
                            if (zz9000_vram_window(&vlo, &vhi)) {
                                c[3] = vlo;
                                c[4] = vhi;
                                c[5] = HDF_VRAM_CAP;
                            }
                        }
#endif
                    }

                    c[2] = (c[2] & 0xFFFF0000u) | st;           /* hc_Status        */
                    g_bigmighd_pending = 0;
                }

                dabt_reg_set(regs, rt, st);
            }
        } else
#endif
        if (dfar >= SHARE_M68K_BASE && dfar < SHARE_M68K_TOP
           ) {
            volatile uint8_t *sh = (volatile uint8_t *)SHARE_FW_PTR;
            uint32_t off = dfar - SHARE_M68K_BASE;
#ifdef EMU68_SHARE_DOORBELL
            if (off == (SHARE_DB_M68K - SHARE_M68K_BASE)) {
                volatile uint32_t *mb = (volatile uint32_t *)SHARE_MBOX_FW_PTR;
                if (!is_load) {
                    /* SUBMIT: validate, translate, CLEAN the buffer, relay to Main. */
                    uint32_t cmd_addr = dabt_reg_get(regs, rt);
                    volatile uint32_t *c = (volatile uint32_t *)cmd_addr;
                    g_share_db_pending = 0;
                    if (cmd_addr >= 0x00200000u && !(cmd_addr & 3) &&
                        c[0] == SHARE_DB_MAGIC && (c[1] >> 16) == 1u) {
                        uint32_t command = c[1] & 0xFFFFu;
                        uint32_t buf     = c[5];
                        uint32_t len     = c[4];
                        uint32_t phys    = 0;
                        uintptr_t maint  = buf;
                        if (buf >= 0x40000000u && (uint64_t)buf + len <= 0x50000000u)
                            phys = Z3FAST_DDR_BASE + (buf - 0x40000000u);
                        else if (buf >= 0x00200000u && (uint64_t)buf + len <= 0x00A00000u)
                            phys = FASTRAM_DDR_BASE + (buf - 0x00200000u);
#ifdef EMU68_ZZ9000
                        else
                        {
                            (void)zz9000_vram_dma(buf, len, &phys, &maint);   /* phys stays 0 if not VRAM */
                        }
#endif
                        if (phys && (command == SHARE_DB_READ || command == SHARE_DB_WRITE)) {
                            arm_flush_cache(maint, len);
                            mb[3] = command;  mb[4] = c[2];   /* command, fp_key   */
                            mb[5] = c[3];     mb[6] = len;    /* offset, length    */
                            mb[7] = phys;     mb[8] = 0xFFFFu; mb[9] = 0;  /* status, actual */
                            asm volatile("dsb" ::: "memory");
                            mb[1] = mb[1] + 1u;               /* seq_req LAST      */
                            asm volatile("dsb" ::: "memory");
                            g_share_db_pending = cmd_addr;
                            g_share_db_buf = (uint32_t)maint; g_share_db_len = len; g_share_db_cmd = command;
                        }
                    }
                } else {
                    uint32_t st = 0xFFFFu;
                    if (g_share_db_pending && mb[2] == mb[1]) {
                        volatile uint32_t *c = (volatile uint32_t *)g_share_db_pending;
                        if (g_share_db_cmd == SHARE_DB_READ && g_share_db_len)
                            arm_dcache_invalidate((uintptr_t)g_share_db_buf, g_share_db_len);
                        st   = mb[8];
                        c[6] = mb[8];                        /* status  */
                        c[7] = mb[9];                        /* actual  */
                        g_share_db_pending = 0;
                    }
                    dabt_reg_set(regs, rt, st);
                }
                asm volatile("dsb" ::: "memory");
            } else
#endif
            if (is_load) {
                uint32_t v = 0;
                for (unsigned i = 0; i < (unsigned)size; i++)
                    v = (v << 8) | sh[off + i];
                if (sign_ext) {                       /* LDRSB/LDRSH sign-extend */
                    if (size == 1)      v = (uint32_t)(int32_t)(int8_t)v;
                    else if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                }
                dabt_reg_set(regs, rt, v);
            } else {
                uint32_t v = dabt_reg_get(regs, rt);
                for (unsigned i = 0; i < (unsigned)size; i++)
                    sh[off + i] = (uint8_t)(v >> (8u * ((unsigned)size - 1u - i)));
            }
            asm volatile("dsb" ::: "memory");
        } else
        {
            static int ob_count = 0;
            if (ob_count < 24) {
                kprintf_fb("[JIT:OB] %s %d @ %08x%s\n", is_load ? "rd" : "wr", size, dfar,
                        (++ob_count == 24) ? "  (further open-bus accesses silenced)" : "");
            }
            if (is_load) {
                uint32_t v = (size == 1) ? 0xffu : (size == 2) ? 0xffffu : 0xffffffffu;
                if (sign_ext) v = 0xffffffffu;      /* all-ones sign-extends to all-ones */
                dabt_reg_set(regs, rt, v);
            }
            /* writes vanish */
        }
    }
    else if (is_load) {
        uint32_t v;
        switch (size) {
            case 1:  v = ps_read_8(dfar);  break;
            case 2:  v = ps_read_16(dfar); break;
            default: v = ps_read_32(dfar); break;
        }
        if (__builtin_expect(emu68_input_shim, 1)) {
            if (dfar == 0xBFE001u && size == 1) {
                v |= 0xC0u;
            }
            else if (dfar == 0xDFF016u) {
                v |= (size == 2) ? 0xFF00u : 0xFFu;
            }
        }
        if (sign_ext) {
            if (size == 1) v = (uint32_t)(int32_t)(int8_t)v;
            else           v = (uint32_t)(int32_t)(int16_t)v;
        }
        dabt_reg_set(regs, rt, v);
    } else {
        uint32_t v = dabt_reg_get(regs, rt);
        if (__builtin_expect(emu68_input_shim, 0) &&
            (dfar == 0xDFF080u || dfar == 0xDFF088u)) {
            emu68_input_shim = 0;
        }
        {
            extern volatile int g_blitwait;
            extern void jit_blitwait_spin(void);
            if (__builtin_expect(g_blitwait != 0, 0) && dfar >= 0x00DFF040u && dfar <= 0x00DFF076u)
                jit_blitwait_spin();
        }
        switch (size) {
            case 1:  ps_write_8(dfar, v & 0xff);    break;
            case 2:  ps_write_16(dfar, v & 0xffff); break;
            default: ps_write_32(dfar, v);          break;
        }
    }

    if (!p_idx)                             /* post-index: Rn = Rn +/- offset    */
        dabt_reg_set(regs, rn, dabt_reg_get(regs, rn) + (u_add ? wb_offset : -wb_offset));
    else if (wback)                         /* pre-index writeback: Rn = DFAR  */
        dabt_reg_set(regs, rn, dfar);
}

static void init_pmu_cycle_counter(void)
{
    uint32_t v;
    asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(v));
    v |= 1u;                                    /* PMCR.E - enable */
    asm volatile("mcr p15, 0, %0, c9, c12, 0" :: "r"(v));
    v = 0x80000000u;                            /* PMCNTENSET.C - cycle counter on */
    asm volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(v));
}

extern uint32_t __vectors_start;

static uint32_t abort_stack[2048] __attribute__((aligned(8)));
static uint32_t undef_stack[1024] __attribute__((aligned(8)));

void init_vectors(void)
{
    uint32_t *abort_top = &abort_stack[2048];
    uint32_t *undef_top = &undef_stack[1024];

    init_pmu_cycle_counter();

    /* Per-mode stacks for Abort (0x17) and Undefined (0x1b); return to SVC. */
    asm volatile(
        "   cps     #0x17        \n"   /* Abort mode      */
        "   mov     sp, %0       \n"
        "   cps     #0x1b        \n"   /* Undefined mode  */
        "   mov     sp, %1       \n"
        "   cps     #0x13        \n"   /* back to SVC     */
        :: "r"(abort_top), "r"(undef_top) : "memory");

    asm volatile("dsb");
    asm volatile("mcr p15,0,%0,c12,c0,0" :: "r"((uint32_t)&__vectors_start));
    asm volatile("isb");

    uint32_t sctlr;
    asm volatile("mrc p15,0,%0,c1,c0,0" : "=r"(sctlr));
    sctlr &= ~(1u << 13);
    asm volatile("mcr p15,0,%0,c1,c0,0" :: "r"(sctlr));
    asm volatile("isb");

#if defined(CHIPSET_AXI)
    board = &__boards_start;
#ifdef EMU68_BIGMIGHD
#endif
#endif
    board_idx = 0;

    kprintf_fb("[BOOT] ARMv7 exception vectors installed at %08x (boards @ %p)\n",
            (uint32_t)&__vectors_start, (void *)board);
}
