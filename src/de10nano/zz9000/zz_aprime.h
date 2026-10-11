/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: page flips that do not wait for the blit queue. */

#ifndef ZZ_APRIME_H
#define ZZ_APRIME_H

#include <stdint.h>

/* the last decision (zz_ap_why) */
#define ZZ_AP_WHY_PAIR          0u  /* answered: a synthetic blank ("in blank" x 2, "out of blank")           */
#define ZZ_AP_WHY_NOT_ALONE     1u  /* another flip still pending: the beam                                 */
#define ZZ_AP_WHY_LINEAGE       2u  /* fewer than two flips recorded before it: the beam                    */
#define ZZ_AP_WHY_TWO_BUFFERS   3u  /* not three buffers in turn: the beam                                  */
#define ZZ_AP_WHY_NOT_COVERED   4u  /* the protection refused it, or its 3D frame is done: the beam          */

int zz_ap_vblank(void);             /* 0x4C: 1 / 0 = the answer, -1 = read the beam                          */
int zz_ap_hold(uint32_t off);       /* a served protection fault at BOARD offset `off`: held until off screen  */
void zz_ap_set_off(uint32_t off);
void zz_ap_counts(uint32_t out[4]); /* flips answered, flips that kept the beam, holds, hold ticks >> 6     */
uint32_t zz_ap_why(void);

#endif /* ZZ_APRIME_H */
