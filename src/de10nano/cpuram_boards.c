/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The Zorro II and Zorro III Fast RAM autoconfig boards. */

#include <boards.h>
#include <support.h>
#include "amiga_memmap.h"

extern uint32_t mmu_table[4096];
extern void arm_flush_cache(uintptr_t addr, uint32_t length);

static void map(struct ExpansionBoard *board)
{
    uint32_t base = board->map_base;
    uint32_t mb   = board->rom_size >> 20;
    uint32_t phys = board->is_z3 ? Z3FAST_DDR_BASE : FASTRAM_DDR_BASE;

    if (base & 0x000FFFFFu) {
        kprintf("[BOARD] %s map_base $%08x not 1MB-aligned -- NOT mapped\n",
                board->is_z3 ? "Z3" : "Z2", base);
        return;
    }
    for (uint32_t i = 0; i < mb; i++)
        mmu_table[(base >> 20) + i] = (phys + (i << 20)) | MMU_FASTRAM;
    arm_flush_cache((uintptr_t)&mmu_table[base >> 20], mb * 4);
    asm volatile("dsb" ::: "memory");
    asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));   /* TLBIALL */
    asm volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));   /* BPIALL  */
    asm volatile("dsb; isb" ::: "memory");

    kprintf("[BOARD] Emu68-A9 CPU RAM (%s) MAPPED by AmigaOS: m68k $%08x (%u MB) -> phys %08x\n",
            board->is_z3 ? "Z3" : "Z2", base, mb, phys);
}

/* ===================== Z2 : 8 MB @ $200000 (donor: src/boards/z2ram.c) ============= */
#define Z2_PRODUCT_ID   0x10
#define Z2_MANUF_ID     0x6d73
#define Z2_SERIAL       0x1e0aeb68
static uint16_t z2_rom[32] = {
    0xe000, 0x0000,                                   /* er_Type: Z2, link to memlist, 8 MB */
    (uint16_t)~(Z2_PRODUCT_ID << 8) & 0xf000, (uint16_t)~(Z2_PRODUCT_ID << 12) & 0xf000,
    (uint16_t)~0x8fff, (uint16_t)~0x0fff,             /* ERT_MEMSPACE in the Z2 region      */
    (uint16_t)~0x0fff, (uint16_t)~0x0fff,
    (uint16_t)~(Z2_MANUF_ID) & 0xf000, (uint16_t)~(Z2_MANUF_ID << 4) & 0xf000,
    (uint16_t)~(Z2_MANUF_ID << 8) & 0xf000, (uint16_t)~(Z2_MANUF_ID << 12) & 0xf000,
    (uint16_t)~(Z2_SERIAL >> 16) & 0xf000, (uint16_t)~(Z2_SERIAL >> 12) & 0xf000,
    (uint16_t)~(Z2_SERIAL >> 8) & 0xf000, (uint16_t)~(Z2_SERIAL >> 4) & 0xf000,
    (uint16_t)~(Z2_SERIAL) & 0xf000, ~(uint16_t)((uint16_t)Z2_SERIAL << 4) & 0xf000,
    (uint16_t)~((uint16_t)Z2_SERIAL << 8) & 0xf000, ~(uint16_t)((uint16_t)Z2_SERIAL << 12) & 0xf000,
    (uint16_t)~0x0fff, (uint16_t)~0x0fff, (uint16_t)~0x0fff, (uint16_t)~0x0fff,
    (uint16_t)~0x0fff, (uint16_t)~0x0fff, (uint16_t)~0x0fff, (uint16_t)~0x0fff,
    (uint16_t)~0x0fff, (uint16_t)~0x0fff, (uint16_t)~0x0fff, (uint16_t)~0x0fff,
};
#ifndef KS_FASTRAM_ENABLED
#define KS_FASTRAM_ENABLED 1
#endif
static struct ExpansionBoard z2_board = { z2_rom, 8u * 1024 * 1024, 0, /*is_z3*/0, /*enabled*/KS_FASTRAM_ENABLED, map };
static void * __attribute__((used, section(".boards.z2"))) _z2 = &z2_board;

static uint16_t z3_rom[32] = {
    0xa000,   /* reg0  $E80000 er_Type: Zorro-III, add mem, no ROM (literal 0xA)          */
    0x4000,   /* reg1  $E80002 256 MB extended size (literal 0x4; 0x3 = 128 MB)           */
    0xe000,   /* reg2  $E80004 ProductID hi -> inv 0x1  (product 0x10)                    */
    0xf000,   /* reg3  $E80006 (default 0xF) -> inv 0x0 (product lo)                      */
    0x0000,   /* reg4  $E80008 mem card, not silenceable, extended size -> inv 0xF        */
    0xf000,   /* reg5  $E8000A logical size == physical -> inv 0x0                        */
    0xf000, 0xf000,                                   /* reg6/7 default -> 0x0            */
    0xe000,   /* reg8  $E80010 Manufacturer 0x139C -> inv 0x1                             */
    0xc000,   /* reg9  $E80012                     -> inv 0x3                             */
    0x6000,   /* regA  $E80014                     -> inv 0x9                             */
    0x3000,   /* regB  $E80016                     -> inv 0xC                             */
    0xf000, 0xf000, 0xf000, 0xf000,                   /* regC..F default                 */
    0xf000, 0xf000, 0xf000,                           /* reg10..12 default               */
    0xe000,   /* reg13 $E80026 serial = 1 (RTL {11,~mc1,mc1}=1110 for 256MB) -> inv 0x1   */
    0xf000, 0xf000, 0xf000, 0xf000,                   /* reg14..17 default               */
    0xf000, 0xf000, 0xf000, 0xf000,                   /* reg18..1B default               */
};
static struct ExpansionBoard z3_board = { z3_rom, 256u * 1024 * 1024, 0, /*is_z3*/1, /*enabled*/KS_FASTRAM_ENABLED, map };
static void * __attribute__((used, section(".boards.z3"))) _z3 = &z3_board;

void cpuram_boards_reset(void)
{
    extern int board_idx;
    extern struct ExpansionBoard **board;

#ifdef EMU68_BIGMIGHD
    { extern void bigmighd_board_unmap(void); bigmighd_board_unmap(); }
#endif
#ifdef EMU68_ZZ9000
    { extern void zz9000_board_unmap(void); zz9000_board_unmap(); }
#endif
#ifdef EMU68_DEVICETREE
    { extern void dtree_board_unmap(void); dtree_board_unmap(); }
#endif
#ifdef EMU68_UAESND
    { extern void uaesnd_stage_off(void); uaesnd_stage_off(); }
#endif
#ifdef EMU68_FP_RESIDENT
    { extern void fpresident_board_unmap(void); fpresident_board_unmap(); }
#endif
#ifdef EMU68_A3D
    { extern void a3d_fw_guest_reset(void); a3d_fw_guest_reset(); }
#endif

    if (board) {
        for (int i = 0; board[i]; i++) {
            struct ExpansionBoard *b = board[i];
            if (!b->map_base) continue;
            uint32_t mb = b->rom_size >> 20;
            for (uint32_t k = 0; k < mb; k++)
                mmu_table[(b->map_base >> 20) + k] = 0;          /* back to unmapped = open bus */
            arm_flush_cache((uintptr_t)&mmu_table[b->map_base >> 20], mb * 4);
            kprintf("[BOARD] reset: UNMAPPED %s window at $%08x (%u MB)\n",
                    b->is_z3 ? "Z3" : "Z2", b->map_base, mb);
            b->map_base = 0;
        }
        asm volatile("dsb" ::: "memory");
        asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));   /* TLBIALL */
        asm volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));   /* BPIALL  */
        asm volatile("dsb; isb" ::: "memory");
    }
    board_idx = 0;                                          /* re-arm the ConfigChain walk */
}
