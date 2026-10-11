/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The Cyclone V HPS CPU clock, decoded from the clock manager. */

#ifndef HPS_CLOCK_H
#define HPS_CLOCK_H

#include <stdint.h>

#define HPS_CLKMGR_PHYS          0xFFD04000u   /* Cyclone V clock manager                        */
#define HPS_CLKMGR_BYPASS        0x04u         /* bit0: main PLL bypassed (mpu_clk = osc1)        */
#define HPS_CLKMGR_STAT          0x14u         /* bit0: busy (a PLL / divider change in flight)   */
#define HPS_CLKMGR_MAINPLL_VCO   0x40u         /* [15:3] numer, [21:16] denom                     */
#define HPS_CLKMGR_MAINPLL_MPU   0x48u         /* [8:0] mpuclk counter                            */
#define HPS_CLKMGR_ALTR_MPU      0xE0u         /* [8:0] alteragrp mpuclk divider                  */
#define HPS_OSC1_KHZ             25000u        /* EOSC1 on the DE10-Nano (DTB: 0x17d7840 Hz)       */
#define HPS_MPU_KHZ_MIN          100000u
#define HPS_MPU_KHZ_MAX          2000000u

static inline uint32_t hps_mpu_hz(uint32_t bypass, uint32_t stat, uint32_t vco,
                                  uint32_t mpu_cnt, uint32_t altr_mpu)
{
    uint32_t numer = (vco >> 3) & 0x1FFFu;
    uint32_t denom = (vco >> 16) & 0x3Fu;
    uint32_t khz;

    if (bypass & 1u) return 0;                 /* a cpufreq step is relocking the PLL             */
    if (stat & 1u)   return 0;                 /* the clock manager is mid-change                 */
    khz  = HPS_OSC1_KHZ * (numer + 1u);
    khz /= denom + 1u;
    khz /= (altr_mpu & 0x1FFu) + 1u;
    khz /= (mpu_cnt & 0x1FFu) + 1u;
    if (khz < HPS_MPU_KHZ_MIN || khz > HPS_MPU_KHZ_MAX) return 0;
    return khz * 1000u;
}

static inline uint32_t hps_cntfrq_hz(uint32_t mpu_hz, uint32_t pmcr)
{
    return (pmcr & 8u) ? (mpu_hz >> 6) : mpu_hz;
}

#endif /* HPS_CLOCK_H */
