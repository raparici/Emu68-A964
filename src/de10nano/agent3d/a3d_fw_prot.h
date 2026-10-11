/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: page protection of render targets behind the lazy waits. */

#ifndef A3D_FW_PROT_H
#define A3D_FW_PROT_H

#include <stdint.h>

#define A3D_FW_PROT_KEYS    24u
#define A3D_FW_PROT_QKEYS   4u

void a3d_fw_prot_window(uint32_t idx, uint32_t lo, uint32_t bytes);
int a3d_fw_prot_set(uint32_t key, uint32_t base, uint32_t lo, uint32_t bytes);
int a3d_fw_prot_pending(uint32_t key, uint32_t on);
void a3d_fw_prot_hold(uint32_t on);         /* every page accessible while held                    */
uint32_t a3d_fw_prot_held(void);
void a3d_fw_prot_reset(void);               /* a guest reset: every key, split and window forgotten */
int a3d_fw_prot_fault(uint32_t dfar);       /* the data-abort entry (src/armhf/vectors.c)          */
void a3d_fw_fence(uint32_t lo, uint32_t hi);    /* a ZZ9000 2D op or flip on [lo, hi)               */
uint32_t a3d_fw_fence_of(uint32_t lo, uint32_t hi);
uint32_t a3d_fw_fence_try(uint32_t fence);
uint32_t a3d_fw_fence_kick(uint32_t fence);
void a3d_fw_fence_wait(uint32_t fence);
int a3d_fw_prot_qcover(const uint32_t *lo, const uint32_t *hi, uint32_t n, uint32_t fence);  /* 1 = protected  */
void a3d_fw_prot_quncover(void);                        /* the covered ops ran: the queue keys go idle   */
void a3d_fw_prot_qdrop(void);                           /* the card was re-initialised: forget them      */
uint32_t a3d_fw_prot_qcovered(void);
void a3d_fw_fence_dma(uint32_t lo, uint32_t hi);        /* a DMA hand-off: the 3D promised + covered ops */
extern uint32_t (*g_ap_qdrain)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);   /* zz9000_board.c registers */
extern uint32_t g_ap_qcovers, g_ap_qfaults, g_ap_qdma, g_ap_qrefused;

extern volatile uint32_t g_a3d_prot_on;
extern volatile uint32_t g_a3d_agent_busy;
extern uint32_t g_ap_faults, g_ap_idle, g_ap_shared, g_ap_self, g_ap_stale, g_ap_nested;
extern uint32_t g_ap_applies, g_ap_pages, g_ap_tlball, g_ap_tlbpages;
extern uint32_t (*g_ap_touch)(uint32_t lo, uint32_t hi, uint32_t why);
extern uint32_t (*g_ap_fence_try)(uint32_t fence, uint32_t a, uint32_t b);
extern uint32_t (*g_ap_fence_wait)(uint32_t fence, uint32_t a, uint32_t b);

#endif
