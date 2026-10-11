/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: page flips that do not wait for the blit queue. */

#include <stdint.h>
#include "zz_aprime.h"

#ifdef EMU68_A3D

#if defined(__arm__) && !defined(__APPLE__)
#define ZZ_AP_BSS  __attribute__((section(".bss.~zz_ap")))
#define ZZ_AP_LATE __attribute__((section(".a3d_late2_text"), noinline))
#else
#define ZZ_AP_BSS
#define ZZ_AP_LATE __attribute__((noinline))
#endif

/* the card's side (zz_card.c) */
extern uint32_t zz_blitq_newest_flip(uint32_t *nflips, uint32_t shows[2], uint32_t replaces[2]);
extern int zz_blitq_recover(void);

#include <support.h>
extern uint32_t g_zz9000_regbase;
extern uint32_t zz9000_display_guard(uint32_t glo, uint32_t ghi);
extern uint32_t (*g_ap_qdrain)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
extern void a3d_fw_fence_wait(uint32_t fence);
extern uint32_t emu68_fastbank_call4(void *fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
static uint32_t zz_ap_plat_guard(uint32_t lo, uint32_t hi)
{
    return emu68_fastbank_call4((void *)zz9000_display_guard, g_zz9000_regbase + lo, g_zz9000_regbase + hi, 0, 0);
}
static void zz_ap_plat_finish(uint32_t fence)
{
    a3d_fw_fence_wait(fence);
    if (g_ap_qdrain)
        (void)emu68_fastbank_call4((void *)g_ap_qdrain, 0, 0, 0, 0);
}
static uint32_t zz_ap_plat_ticks(void)
{
    uint32_t c;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(c));     /* PMCCNTR */
    return c;
}

#define ZZ_AP_FREE 4u           /* the buffers declared free a CPU access is held on (round robin) */

struct zz_ap {
    uint32_t phase;             /* the synthetic blank's reads still to answer: 2 "in blank", then 1 "out of blank" */
    uint32_t off;
    uint32_t seq;               /* the last flip decided: its push number + 1 (zz_blitq_newest_flip)               */
    uint32_t nhist;             /* flips recorded since the card's queue (re)started, up to 2                      */
    uint32_t hist[2][2];        /* the buffers the last two flips show ([0] the newest), BOARD [lo, hi)            */
    uint32_t free_lo[ZZ_AP_FREE], free_hi[ZZ_AP_FREE];  /* buffers an answer declared free, until a hold saw them off */
    uint32_t free_next;
    uint32_t why;
    uint32_t cnt[4];
};
static struct zz_ap g_ap ZZ_AP_BSS;

/* [a0, a1) and [b0, b1) share a byte */
static int zz_ap_ov(const uint32_t *a, const uint32_t *b)
{
    return a[0] < b[1] && b[0] < a[1];
}

__attribute__((noinline)) int zz_ap_vblank(void)
{
    struct zz_ap *s = &g_ap;
    if (s->phase) {
        s->phase--;
        if (!s->off)
            return s->phase ? 1 : 0;
        s->phase = 0;
    }
    uint32_t n, sh[2], rp[2];
    const uint32_t seq = zz_blitq_newest_flip(&n, sh, rp);
    if (!seq || seq == s->seq)
        return -1;                              /* no flip pending, or this one decided: the beam */
    if (seq < s->seq)
        s->nhist = 0;                           /* the queue's count restarted: a re-initialised card */
    s->seq = seq;
    uint32_t why = ZZ_AP_WHY_PAIR;
    if (n != 1u)
        why = ZZ_AP_WHY_NOT_ALONE;
    else if (s->nhist < 2u)
        why = ZZ_AP_WHY_LINEAGE;
    else if (!zz_ap_ov(rp, s->hist[0]) || zz_ap_ov(sh, s->hist[0]) || zz_ap_ov(sh, s->hist[1])
             || zz_ap_ov(s->hist[0], s->hist[1]))
        why = ZZ_AP_WHY_TWO_BUFFERS;            /* not three buffers in turn (or a flip this file never saw) */
    s->hist[1][0] = s->hist[0][0];              /* the lineage: every flip, A' on or off */
    s->hist[1][1] = s->hist[0][1];
    s->hist[0][0] = sh[0];
    s->hist[0][1] = sh[1];
    if (s->nhist < 2u)
        s->nhist++;
    if (s->off)
        return -1;
    if (why == ZZ_AP_WHY_PAIR && !zz_blitq_recover())
        why = ZZ_AP_WHY_NOT_COVERED;
    s->why = why;
    if (why != ZZ_AP_WHY_PAIR) {
        s->cnt[1]++;
        return -1;
    }
    const uint32_t k = s->free_next++ % ZZ_AP_FREE;
    s->free_lo[k] = rp[0];
    s->free_hi[k] = rp[1];
    s->phase = 2;
    s->cnt[0]++;
    return 1;                                   /* "in blank": the pan's WaitVerticalSync(bi, 0) returns */
}

ZZ_AP_LATE int zz_ap_hold(uint32_t off)
{
    struct zz_ap *s = &g_ap;
    for (uint32_t i = 0; i < ZZ_AP_FREE; i++) {
        if (off < s->free_lo[i] || off >= s->free_hi[i])
            continue;
        const uint32_t page = off & ~0xFFFu, t0 = zz_ap_plat_ticks();
        for (uint32_t n = 0; n < 8u; n++) {
            const uint32_t f = zz_ap_plat_guard(page, page + 0x1000u);
            if (!f)
                break;
            zz_ap_plat_finish(f);               /* a queued flip still replaces it: its frame, then the queue */
        }
        s->cnt[3] += (zz_ap_plat_ticks() - t0) >> 6;
        s->free_hi[i] = 0;
        s->cnt[2]++;
        return 1;
    }
    return 0;
}

uint32_t zz9000_mmio_ap(uint32_t off, int is_load, unsigned size, uint32_t wval)
{
    extern uint32_t zz9000_mmio(uint32_t off, int is_load, unsigned size, uint32_t wval);
    const uint32_t v = zz9000_mmio(off, is_load, size, wval);
    if (off == 0x4Cu && size == 2u && is_load) {
        const int a = zz_ap_vblank();
        if (a >= 0)
            return (uint32_t)a;
    }
    return v;
}

ZZ_AP_LATE int a3d_fw_prot_fault_ap(uint32_t dfar)
{
    extern int a3d_fw_prot_fault(uint32_t dfar);
    extern volatile uint32_t g_a3d_agent_busy;
    uint32_t spsr;
    __asm__ volatile("mrs %0, spsr" : "=r"(spsr));
    if (!a3d_fw_prot_fault(dfar))
        return 0;
    if (g_zz9000_regbase && (spsr & 0x1Fu) != 0x17u && !g_a3d_agent_busy)
        (void)zz_ap_hold(dfar - g_zz9000_regbase);
    return 1;
}

#endif /* EMU68_A3D */
