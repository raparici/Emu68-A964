/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The Zorro III board that delivers the FPU math resident (the FPU ROM). */

#ifdef EMU68_FP_RESIDENT

#include <stdint.h>
#include <boards.h>
#include <support.h>
#include "amiga_memmap.h"

__attribute__((aligned(4)))
#include "./fpresident/fpresident_rom.h"

extern uint32_t mmu_table[4096];
extern void arm_flush_cache(uintptr_t addr, uint32_t length);

/* Build-time checksum of the ROM image, exported so the earliest check can use it. */
const uint32_t g_fpresident_bin_sum = FPRESIDENT_BIN_SUM;
uint32_t g_fpresident_rom_base = 0;

void fpresident_board_enable(int on);

static uint32_t rotadd_sum(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++)
        s = (s << 1) + (s >> 31) + p[i];
    return s;
}

static void map(struct ExpansionBoard *board)
{
    uint32_t base = board->map_base;

    if (base & 0x000FFFFFu) {
        kprintf("[FPRES] map_base $%08x not 1MB-aligned -- board NOT mapped\n", base);
        return;
    }

    {
        const uint8_t *src = (const uint8_t *)fpresident_bin;
        uint8_t *dst = (uint8_t *)FPRESIDENT_ROM_DDR_BASE;
        uint32_t sum;

        mmu_table[FPRESIDENT_ROM_DDR_BASE >> 20] = FPRESIDENT_ROM_DDR_BASE | MMU_DEVICE;
        arm_flush_cache((uintptr_t)&mmu_table[FPRESIDENT_ROM_DDR_BASE >> 20], 4);
        asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0) : "memory");   /* TLBIALL */
        asm volatile("dsb; isb" ::: "memory");

        for (uint32_t i = 0; i < sizeof(fpresident_bin); i++)
            dst[i] = src[i];
        arm_flush_cache((uintptr_t)dst, sizeof(fpresident_bin));

        sum = rotadd_sum(src, sizeof(fpresident_bin));
        if (sum != g_fpresident_bin_sum)
            kprintf("[FPRES] WARNING image sum %08x != built %08x\n", sum, (uint32_t)g_fpresident_bin_sum);

        mmu_table[FPRESIDENT_ROM_DDR_BASE >> 20] = FPRESIDENT_ROM_DDR_BASE | MMU_DEVICE | 0x8000u;
        arm_flush_cache((uintptr_t)&mmu_table[FPRESIDENT_ROM_DDR_BASE >> 20], 4);
        asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0) : "memory");
        asm volatile("dsb; isb" ::: "memory");
    }

    mmu_table[base >> 20]       = FPRESIDENT_ROM_DDR_BASE | MMU_CACHED_RO;
    mmu_table[(base >> 20) + 1] = 0;
    arm_flush_cache((uintptr_t)&mmu_table[base >> 20], 8);
    asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0) : "memory");
    asm volatile("dsb ish; isb" ::: "memory");

    g_fpresident_rom_base = base;

    kprintf("[FPRES] ROM at $%08x (%u bytes), first longword $%08x\n",
            base, (unsigned)sizeof(fpresident_bin), *(volatile uint32_t *)base);
}

static struct ExpansionBoard board = {
    fpresident_bin,             /* rom_file: the $E80000 nibbles are its leading bytes    */
    sizeof(fpresident_bin),
    0,                          /* map_base, filled in by the autoconfig responder        */
    1,                          /* is_z3                                                  */
    0,
    map
};

static void * __attribute__((used, section(".boards.z3"))) _board = &board;

void fpresident_board_enable(int on)
{
    board.enabled = on ? 1u : 0u;
}

void fpresident_board_unmap(void)
{
    if (!g_fpresident_rom_base) return;
    mmu_table[g_fpresident_rom_base >> 20]       = 0;
    mmu_table[(g_fpresident_rom_base >> 20) + 1] = 0;
    arm_flush_cache((uintptr_t)&mmu_table[g_fpresident_rom_base >> 20], 8);
    asm volatile("dsb" ::: "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0u));   /* TLBIALL */
    asm volatile("dsb; isb" ::: "memory");
    kprintf("[FPRES] reset: UNMAPPED the board window at $%08x (2 MB)\n", g_fpresident_rom_base);
    g_fpresident_rom_base = 0;
}

#endif /* EMU68_FP_RESIDENT */
