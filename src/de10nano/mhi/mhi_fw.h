/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* MHI: the firmware side of the MP3 / MP2 decoder. */

#ifndef MHI_FW_H
#define MHI_FW_H

#include <stdint.h>

extern volatile uint32_t g_mhi_active;

uint32_t mhi_fw_mmio(uint32_t rel_off, int is_load, unsigned size, uint32_t wval);

void mhi_fw_tick(void);

void mhi_fw_mix_frame(int32_t *l, int32_t *r);

void mhi_fw_reset(void);

extern volatile uint32_t g_mhi_off;
void mhi_fw_enable(int on);

#endif /* MHI_FW_H */
