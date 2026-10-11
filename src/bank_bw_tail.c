/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The blitter-wait option on the bank fast path's register writes. */

#include <stdint.h>

#if defined(EMU68_BANK_FASTPATH) && !defined(__aarch64__)

extern volatile int g_blitwait;
extern void jit_blitwait_spin(void);            /* jit_levers.c: spin on DMACONR.BBUSY, bounded, counted */
extern void ps_write_8(unsigned int address, unsigned int data);
extern void ps_write_16(unsigned int address, unsigned int data);
extern void ps_write_32(unsigned int address, unsigned int data);

#if defined(__arm__) && !defined(__APPLE__)
#define BW_TEXT __attribute__((section(".bw_tail_text"), noinline))
#else
#define BW_TEXT __attribute__((noinline))
#endif

/* the router's test (vectors.c): dfar >= $DFF040 && dfar <= $DFF076 */
static inline __attribute__((always_inline)) void bw_wait(unsigned int address)
{
    if (__builtin_expect(g_blitwait != 0, 0) && address - 0x00DFF040u <= 0x36u)
        jit_blitwait_spin();
}

BW_TEXT void emu68_bank_bw_w8(unsigned int address, unsigned int data)  { bw_wait(address); ps_write_8(address, data); }
BW_TEXT void emu68_bank_bw_w16(unsigned int address, unsigned int data) { bw_wait(address); ps_write_16(address, data); }
BW_TEXT void emu68_bank_bw_w32(unsigned int address, unsigned int data) { bw_wait(address); ps_write_32(address, data); }

#endif /* EMU68_BANK_FASTPATH && !__aarch64__ */
