/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The Zorro III autoconfig board that delivers bigmigHD.device, and its doorbell. */

#include <stdint.h>
#include <boards.h>
#include <support.h>
#include "amiga_memmap.h"

__attribute__((aligned(4)))
#include "./bigmighd_rom.h"

extern uint32_t mmu_table[4096];
extern void arm_flush_cache(uintptr_t addr, uint32_t length);

uint32_t g_bigmighd_doorbell_base = 0;

uint32_t g_bigmighd_rom_base = 0;

static void map(struct ExpansionBoard *board)
{
    uint32_t base = board->map_base;

    if (base & 0x000FFFFFu) {
        kprintf("[BIGMIGHD] map_base $%08x is not 1MB-aligned -- board NOT mapped\n", base);
        return;
    }

    /* Stage the ROM into an aligned megabyte so a section can point at it. */
    {
        const uint8_t *src = (const uint8_t *)bigmighd_bin;
        uint8_t *dst = (uint8_t *)BIGMIGHD_ROM_DDR_BASE;

        mmu_table[BIGMIGHD_ROM_DDR_BASE >> 20] = BIGMIGHD_ROM_DDR_BASE | MMU_DEVICE;
        arm_flush_cache((uintptr_t)&mmu_table[BIGMIGHD_ROM_DDR_BASE >> 20], 4);
        asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0) : "memory");
        asm volatile("dsb; isb" ::: "memory");

        for (uint32_t i = 0; i < sizeof(bigmighd_bin); i++)
            dst[i] = src[i];
        arm_flush_cache((uintptr_t)dst, sizeof(bigmighd_bin));

        {
            mmu_table[BIGMIGHD_ROM_DDR_BASE >> 20] = BIGMIGHD_ROM_DDR_BASE | MMU_DEVICE | 0x8000u;
            arm_flush_cache((uintptr_t)&mmu_table[BIGMIGHD_ROM_DDR_BASE >> 20], 4);
            asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0) : "memory");
            asm volatile("dsb; isb" ::: "memory");
            kprintf("[BIGMIGHD] ROM staging sealed read-only at phys %08x\n",
                    (uint32_t)BIGMIGHD_ROM_DDR_BASE);
        }
    }

    mmu_table[base >> 20]       = BIGMIGHD_ROM_DDR_BASE | MMU_CACHED_RO;
    mmu_table[(base >> 20) + 1] = 0;               /* translation fault = the doorbell */
    arm_flush_cache((uintptr_t)&mmu_table[base >> 20], 8);
    asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0) : "memory");  /* TLBIALL */
    asm volatile("dsb ish; isb" ::: "memory");

    g_bigmighd_rom_base      = base;
    g_bigmighd_doorbell_base = base + 0x00100000u;

    kprintf("[BIGMIGHD] ROM at $%08x (%u bytes), doorbell megabyte at $%08x\n",
            base, (unsigned)sizeof(bigmighd_bin), g_bigmighd_doorbell_base);
}

void bigmighd_board_unmap(void)
{
    if (!g_bigmighd_rom_base) return;

    mmu_table[g_bigmighd_rom_base >> 20]       = 0;
    mmu_table[(g_bigmighd_rom_base >> 20) + 1] = 0;
    arm_flush_cache((uintptr_t)&mmu_table[g_bigmighd_rom_base >> 20], 8);
    asm volatile("dsb" ::: "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0u));   /* TLBIALL */
    asm volatile("dsb; isb" ::: "memory");

    kprintf("[BIGMIGHD] reset: UNMAPPED the board window at $%08x (2 MB)\n",
            g_bigmighd_rom_base);

    g_bigmighd_rom_base      = 0;
    g_bigmighd_doorbell_base = 0;       /* the doorbell must stop answering too */
}

static struct ExpansionBoard board = {
    bigmighd_bin,               /* rom_file: the $E80000 nibbles are its first 64 bytes  */
    sizeof(bigmighd_bin),
    0,                          /* map_base, filled in by the autoconfig responder       */
    1,                          /* is_z3                                                 */
    1,                          /* enabled - the whole file is behind EMU68_BIGMIGHD     */
    map
};

static void * __attribute__((used, section(".boards.z3"))) _board = &board;
