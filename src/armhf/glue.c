/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* Symbols the AArch64 build provides and the ARMv7 build needs: the M68KState pointer, CRC32, assert. */

#include <stdint.h>
#include <stddef.h>
#include "M68k.h"

struct M68KState *__m68k_state = NULL;

void M68K_SetStatePtr(struct M68KState *s)
{
    __m68k_state = s;
}

extern int kprintf(const char *fmt, ...);

void M68K_PrintContext(void *m68k_)
{
    struct M68KState *m68k = (struct M68KState *)m68k_;
    if (!m68k) return;

    kprintf("\nM68K Context (armhf-port):\n");
    for (int i = 0; i < 8; i++)
        kprintf("  D%d=%08x", i, (unsigned)__builtin_bswap32(m68k->D[i].u32));
    kprintf("\n");
    for (int i = 0; i < 8; i++)
        kprintf("  A%d=%08x", i, (unsigned)__builtin_bswap32(m68k->A[i].u32));
    kprintf("\n  PC=%08x  SR=%04x\n",
            (unsigned)__builtin_bswap32(m68k->PC),
            (unsigned)__builtin_bswap16(m68k->SR));
}

static uint32_t crc_tab[4][256];
static int crc_tab_ready = 0;

static void crc_tab_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
        crc_tab[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++) {
        crc_tab[1][i] = (crc_tab[0][i] >> 8) ^ crc_tab[0][crc_tab[0][i] & 0xFFu];
        crc_tab[2][i] = (crc_tab[1][i] >> 8) ^ crc_tab[0][crc_tab[1][i] & 0xFFu];
        crc_tab[3][i] = (crc_tab[2][i] >> 8) ^ crc_tab[0][crc_tab[2][i] & 0xFFu];
    }
    crc_tab_ready = 1;
}

uint32_t CalcCRC32(void *_start, void *_end)
{
    const uint8_t *p = (const uint8_t *)_start;
    const uint8_t *e = (const uint8_t *)_end;
    uint32_t crc = 0xffffffffu;

    if (__builtin_expect(!crc_tab_ready, 0))
        crc_tab_init();
    while (e - p >= 4) {
        /* the four bytes as the little-endian word the reflected CRC consumes, loaded bytewise */
        crc ^= (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        crc = crc_tab[3][crc & 0xFFu] ^ crc_tab[2][(crc >> 8) & 0xFFu]
            ^ crc_tab[1][(crc >> 16) & 0xFFu] ^ crc_tab[0][crc >> 24];
        p += 4;
    }
    while (p < e)
        crc = (crc >> 8) ^ crc_tab[0][(crc ^ *p++) & 0xFFu];
    return crc ^ 0xffffffffu;
}

uint32_t CalcCRC32_bitwise(void *_start, void *_end)
{
    const uint8_t *p = (const uint8_t *)_start;
    const uint8_t *e = (const uint8_t *)_end;
    uint32_t crc = 0xffffffffu;

    while (p < e) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return crc ^ 0xffffffffu;
}

void __assert_func(const char *file, int line, const char *func,
                   const char *expr)
{
    (void)file; (void)line; (void)func; (void)expr;
    /* Hang the CPU - a debugger can inspect LR to find the assertion site */
    __builtin_trap();
    for (;;) {}
}
