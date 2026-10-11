/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* uaesnd: the board's autoconfig and register window. */

#include <stdint.h>
#include <boards.h>
#include <support.h>
#include "amiga_memmap.h"
#include "uaesnd/uaesnd_card.h"

uint32_t g_uaesnd_win;

static const uint8_t uaesnd_acrom[64] = {
    [0x00] = 0xC0, [0x02] = 0x10,   /* er_Type: Z2, 64 KB, no memlist        */
    [0x04] = 0xF0, [0x06] = 0xD0,   /* product 0x02                          */
    [0x08] = 0xF0, [0x0A] = 0xF0,   /* er_Flags 0                            */
    [0x0C] = 0xF0, [0x0E] = 0xF0,
    [0x10] = 0xE0, [0x12] = 0x60,   /* manufacturer 0x1966 = 6502 decimal    */
    [0x14] = 0x90, [0x16] = 0x90,
    [0x18] = 0xF0, [0x1A] = 0xF0,   /* serial 0                              */
    [0x1C] = 0xF0, [0x1E] = 0xF0,
    [0x20] = 0xF0, [0x22] = 0xF0,
    [0x24] = 0xF0, [0x26] = 0xF0,
    [0x28] = 0xF0, [0x2A] = 0xF0,   /* no init-diag ROM                      */
    [0x2C] = 0xF0, [0x2E] = 0xF0,
    [0x30] = 0xF0, [0x32] = 0xF0, [0x34] = 0xF0, [0x36] = 0xF0,
    [0x38] = 0xF0, [0x3A] = 0xF0, [0x3C] = 0xF0, [0x3E] = 0xF0,
};

static void map(struct ExpansionBoard *board)
{
    uint32_t base = board->map_base;
    if (base < 0x00E90000u || base >= 0x00F00000u) {
        kprintf("[uaesnd] REJECT base %08x (outside the Z2 I/O region)\n", base);
        return;
    }
    g_uaesnd_win = base;
    g_uaesnd.base = base;
    uaesnd_card_init(&g_uaesnd);
    kprintf("[uaesnd] board at %08x (64 KB, mfg 6502 / product 2), %u streams\n",
            base, (unsigned)UAESND_STREAMS);
}

/* The data-abort entry point (vectors.c), serving the 64 KB register window. */
uint32_t uaesnd_mmio(uint32_t off, int is_load, unsigned size, uint32_t wval)
{
    if (is_load)
        return uaesnd_card_read(&g_uaesnd, off, size);
    uaesnd_card_write(&g_uaesnd, off, wval, size);
    return 0;
}

static struct ExpansionBoard board = {
    uaesnd_acrom,
    sizeof(uaesnd_acrom),
    0,
    0,      /* Zorro II  */
    1,      /* enabled   */
    map
};

static void * __attribute__((used, section(".boards.z2"))) _uaesnd = &board;

volatile uint32_t g_uaesnd_off = 0;
void uaesnd_board_enable(int on)
{
    board.enabled = on ? 1u : 0u;
    g_uaesnd_off  = on ? 0u : 1u;
}
