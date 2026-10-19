/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The bank fast path's register writes: the blitter-wait option and the copper-stop guard on DMACON. */

#include <stdint.h>

#if !defined(__aarch64__)
extern unsigned int ps_read_16(unsigned int address);
#define COPGUARD_MAX_READS 600u
#if defined(__arm__) && !defined(__APPLE__)
#define CG_TEXT __attribute__((section(".bw_tail_text"), noinline))
#else
#define CG_TEXT __attribute__((noinline))
#endif
CG_TEXT void emu68_copper_stop_guard(unsigned int address, unsigned int data, unsigned int size)
{
    unsigned int v;
    if (size == 2u && address == 0x00DFF096u)
        v = data & 0xFFFFu;
    else if (size == 4u && address == 0x00DFF094u)
        v = data & 0xFFFFu;
    else if (size == 4u && address == 0x00DFF096u)
        v = data >> 16;
    else
        return;
    if ((v & 0x8000u) || !(v & 0x0280u))       /* a SET, or neither DMAEN (bit 9) nor COPEN (bit 7) cleared */
        return;
    unsigned int n = 0;
    for (; n < COPGUARD_MAX_READS; n++) {
        const unsigned int hi = ps_read_16(0x00DFF004u) & 1u;        /* VPOSR bit 0 = V8 */
        const unsigned int vp = ps_read_16(0x00DFF006u) >> 8;        /* VHPOSR [15:8] = V7..V0 */
        if (hi || vp >= 2u)
            break;
    }
}
extern volatile int g_blitwait;
extern void jit_blitwait_spin(void);
CG_TEXT void emu68_store_gate(unsigned int address, unsigned int data, unsigned int size)
{
    if (g_blitwait != 0 && address - 0x00DFF040u <= 0x36u)
        jit_blitwait_spin();
    if (address - 0x00DFF094u <= 2u)
        emu68_copper_stop_guard(address, data, size);
}
#endif

#if defined(EMU68_BANK_FASTPATH) && !defined(__aarch64__)

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
BW_TEXT void emu68_bank_bw_w16(unsigned int address, unsigned int data)
{
    bw_wait(address);
    if (__builtin_expect(address - 0x00DFF094u <= 2u, 0))
        emu68_copper_stop_guard(address, data, 2u);
    ps_write_16(address, data);
}
BW_TEXT void emu68_bank_bw_w32(unsigned int address, unsigned int data)
{
    bw_wait(address);
    if (__builtin_expect(address - 0x00DFF094u <= 2u, 0))
        emu68_copper_stop_guard(address, data, 4u);
    ps_write_32(address, data);
}

#endif /* EMU68_BANK_FASTPATH && !__aarch64__ */
