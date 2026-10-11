/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* ZZ9000: the Ethernet pump while the guest idles in STOP. */

#include <stddef.h>
#include <stdint.h>
#include <M68k.h>
#include "amiga_memmap.h"

#ifdef EMU68_ZZNET
extern struct M68KState *__m68k_state;      /* src/armhf/glue.c: start_emu's context (M68K_SetStatePtr) */
extern volatile uint32_t g_zznet_active;    /* zz_card.c: the card is armed and the guest enabled its Ethernet interrupt */
extern void zz_eth_pump_global(void);       /* zz9000_board.c: the C-pass pump */
extern int chipset_pending_ipl(void);       /* the chipset backend's IPL read */

__attribute__((section(".zznet_tail_text"), noinline))
int zz_idle_pending_ipl(void)
{
    if (__builtin_expect(g_zznet_active != 0u, 0) && __m68k_state->STOP_REQ != 0u &&
        ((volatile uint32_t *)ZZNET_CTRL_DDR)[ZZNET_CTRL_MAIN_UP / 4] != 0u)
        zz_eth_pump_global();
    return chipset_pending_ipl();
}
#endif
