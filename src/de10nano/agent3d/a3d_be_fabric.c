/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the FABRIC backend (the GPU in the FPGA). */

#include "a3d_agent.h"

#define FAB_ID          0x000u
#define FAB_VERSION     0x004u
#define FAB_CAPS        0x008u
#define FAB_CTRL        0x00Cu
#define FAB_STATUS      0x010u
#define FAB_ERR         0x014u
#define FAB_FE_FORCE    0x018u
#define FAB_SUBMIT_DIR  0x020u
#define FAB_SUBMIT_BANDS 0x024u
#define FAB_SUBMIT_SEQ  0x028u
#define FAB_DOORBELL    0x02Cu
#define FAB_FENCE_DONE  0x030u
#define FAB_KICK_CLOCKS 0x034u
#define FAB_L2_INV      0x038u

#define FAB_ID_BGPU     0x42475055u
#define FAB_CTRL_ENABLE 0x01u
#define FAB_CTRL_SWAP   0x02u
#define FAB_CTRL_ERRCLR 0x10u
#define FAB_CTRL_CSWAP  0x20u
#define FAB_STATUS_ACTIVE 0x01u
#define FAB_CAPS_UNITS(c)   ((c) & 0xFu)
#define FAB_CAPS_TV_RAW     (1u << 8)
#define FAB_CAPS_LINK       (1u << 9)
#define FAB_CAPS_CSWAP      (1u << 14)

#define A3D_FAB_SPIN_US     1000u
#define A3D_FAB_HANG_MS     2000u       /* no fence for this long: the set is given up on                 */
#define A3D_FAB_POLL_NS     150u
#define FAB_Q               4u

uint32_t a3d_fab_fe_force;
#define FAB_NEG(x) 0u

typedef struct fab_set
{
    a3d_listset ls;
    uint32_t seq;
    uint32_t state;                     /* 0 free, 1 queued, 2 running */
} fab_set;

static fab_set g_q[FAB_Q];
static uint32_t g_found;
static uint32_t g_units;
static uint32_t g_seq;                  /* the last SUBMIT_SEQ written (never reset)                       */
static uint32_t g_done;
static uint32_t g_dead;                 /* given up after a hang: submissions complete at once             */
static uint32_t g_t0;
static uint32_t g_polls;                /* FENCE_DONE reads since that doorbell (the clock if there is none) */
static uint32_t g_cswap;
static uint32_t g_ctrl;

#define FAB_NEG_NOCSWAP 0u
static uint32_t fab_ctrl_bits(uint32_t cswap)
{
    return (FAB_NEG(a3d_fab_neg_noswap) ? 0u : FAB_CTRL_SWAP) | FAB_CTRL_ENABLE
           | (cswap && g_cswap && !FAB_NEG_NOCSWAP ? FAB_CTRL_CSWAP : 0u);
}

static uint32_t fab_rd(uint32_t off)
{
    return a3d_plat_gpu_rd(off);
}

static void fab_wr(uint32_t off, uint32_t v)
{
    if (g_found)
        a3d_plat_gpu_wr(off, v);
}

int a3d_fabric_probe(void)
{
    g_found = 0;
    g_units = 0;
    g_cswap = 0;
    if (!a3d_plat_gpu_map())
        return 0;
    if (fab_rd(FAB_ID) != FAB_ID_BGPU)
        return 0;
    const uint32_t ver = fab_rd(FAB_VERSION), caps = fab_rd(FAB_CAPS);
    if ((ver >> 16) != 1u || FAB_CAPS_UNITS(caps) == 0
        || (caps & (FAB_CAPS_TV_RAW | FAB_CAPS_LINK)) != (FAB_CAPS_TV_RAW | FAB_CAPS_LINK))
        return 0;
    g_found = 1;
    g_units = FAB_CAPS_UNITS(caps);
    g_cswap = (caps & FAB_CAPS_CSWAP) ? 1u : 0u;
    return 1;
}

static uint32_t fab_ticks_us(uint32_t us)
{
    const uint32_t per_ms = a3d_plat_cycles_per_ms();
    return per_ms ? (uint32_t)(((uint64_t)per_ms * us) / 1000u) : 0u;
}

static int fab_elapsed(uint32_t t0, uint32_t polls, uint32_t us)
{
    const uint32_t lim = fab_ticks_us(us);
    return lim ? a3d_plat_cycles() - t0 >= lim : polls >= us * (1000u / A3D_FAB_POLL_NS);
}

static fab_set *fab_running(void)
{
    for (uint32_t i = 0; i < FAB_Q; i++)
        if (g_q[i].state == 2u)
            return &g_q[i];
    return 0;
}

/* the oldest queued set: kicked when nothing runs */
static void fab_kick(void)
{
    if (fab_running())
        return;
    fab_set *n = 0;
    for (uint32_t i = 0; i < FAB_Q; i++)
        if (g_q[i].state == 1u && (n == 0 || g_q[i].ls.fence < n->ls.fence))
            n = &g_q[i];
    if (n == 0)
        return;
    if (a3d_tex_l2_dirty && !FAB_NEG(a3d_fab_neg_nol2inv))
    {
        fab_wr(FAB_L2_INV, 1u);         /* texels the ARM wrote: the shared cache must not hold their old lines */
        a3d_cnt.fab_l2inv++;
    }
    a3d_tex_l2_dirty = 0;
    {
        const uint32_t ctrl = fab_ctrl_bits(n->ls.color_be);
        if (ctrl != g_ctrl)
        {
            fab_wr(FAB_CTRL, ctrl);
            g_ctrl = ctrl;
        }
        if (ctrl & FAB_CTRL_CSWAP)
            a3d_cnt.fab_cswap_sets++;
    }
    n->seq = ++g_seq;
    fab_wr(FAB_SUBMIT_DIR, n->ls.dir_phys + 32u);
    fab_wr(FAB_SUBMIT_BANDS, n->ls.bands);
    fab_wr(FAB_SUBMIT_SEQ, n->seq);
    fab_wr(FAB_DOORBELL, 1u);
    n->state = 2u;
    g_t0 = a3d_plat_cycles();
    g_polls = 0;
    a3d_cnt.fab_kicks++;
}

static void fab_complete(fab_set *r)
{
    g_done = r->ls.fence;
    r->state = 0;
}

/* the running set's fence came (or the backend gave up): its accounting, then the next set */
static void fab_retire(fab_set *r)
{
    const uint32_t kc = fab_rd(FAB_KICK_CLOCKS);
    a3d_cnt.fab_kick_clocks += kc;
    if (kc > a3d_cnt.fab_kick_max)
        a3d_cnt.fab_kick_max = kc;
    const uint32_t err = fab_rd(FAB_ERR);
    if (err)
    {
        a3d_cnt.fab_errors++;
        a3d_cnt.fab_err_last = err;
        fab_wr(FAB_CTRL, g_ctrl | FAB_CTRL_ERRCLR);
    }
    fab_complete(r);
    fab_kick();
}

static void fab_give_up(void)
{
    a3d_cnt.fab_hangs++;
    a3d_cnt.backend |= 0x200u;
    g_dead = 1;
    a3d_plat_log("[A3D] FABRIC: no fence for %u ms (STATUS %08x ERR %08x FENCE_DONE %08x seq %08x): giving up -- the "
                 "GPU needs a core reload; until it is idle again the 3D engine completes work without drawing\n",
                 A3D_FAB_HANG_MS, fab_rd(FAB_STATUS), fab_rd(FAB_ERR), fab_rd(FAB_FENCE_DONE), g_seq);
    for (;;)
    {
        fab_set *o = 0;
        for (uint32_t i = 0; i < FAB_Q; i++)
            if (g_q[i].state && (o == 0 || g_q[i].ls.fence < o->ls.fence))
                o = &g_q[i];
        if (o == 0)
            break;
        fab_complete(o);
    }
}

/* one look at the running set's fence: 1 = it completed (and the next was kicked) */
static int fab_poll(void)
{
    fab_set *r = fab_running();
    if (r == 0)
        return 0;
    g_polls++;
    if (FAB_NEG(a3d_fab_neg_nofence) || fab_rd(FAB_FENCE_DONE) == r->seq)
    {
        fab_retire(r);
        return 1;
    }
    return 0;
}

static int a3d_fab_submit(const a3d_listset *ls)
{
    if (g_dead || !g_found)
    {
        g_done = ls->fence;             /* nothing will draw it: complete at once (counted at the give-up) */
        return 0;
    }
    for (uint32_t i = 0; i < FAB_Q; i++)
        if (g_q[i].state == 0)
        {
            g_q[i].ls = *ls;
            g_q[i].state = 1u;
            fab_kick();
            return 0;
        }
    return W3D_QUEUEFAILED;
}

/* the running set has had no fence for A3D_FAB_HANG_MS since its doorbell */
static int fab_overdue(void)
{
    return fab_running() != 0 && fab_elapsed(g_t0, g_polls, A3D_FAB_HANG_MS * 1000u);
}

static uint32_t a3d_fab_progress(uint32_t budget)
{
    if (fab_poll() || fab_running() == 0)
    {
        return g_done;
    }
    if (budget < 65536u)
    {
        if (fab_overdue())
            fab_give_up();
        return g_done;
    }
    const int drain = budget == 0xFFFFFFFFu;
    const uint32_t t = a3d_plat_cycles();
    uint32_t polls = 0;
    a3d_prof_begin(A3D_PROF_WAIT);
    for (;;)
    {
        a3d_plat_gpu_relax();
        polls++;
        if (fab_poll())
            break;
        if (fab_overdue())
        {
            fab_give_up();
            break;
        }
        if (!drain && fab_elapsed(t, polls, A3D_FAB_SPIN_US))
            break;
    }
    a3d_prof_end(A3D_PROF_WAIT, &a3d_cnt.prof_wait);
    a3d_cnt.fab_wait_polls += polls;
    return g_done;
}

__attribute__((noinline)) static uint32_t a3d_fab_completed(void)
{
    fab_poll();
    return g_done;
}

static void a3d_fab_reset(void)
{
    memset(g_q, 0, sizeof(g_q));
    g_done = 0;
    if (!g_found)
    {
        g_dead = 0;
        return;
    }
    if (g_dead)
    {
        if (fab_rd(FAB_STATUS) & FAB_STATUS_ACTIVE)
        {
            a3d_cnt.backend |= 0x200u;
            return;
        }
        g_dead = 0;
    }
    else
    {
        const uint32_t t0 = a3d_plat_cycles();
        uint32_t polls = 0;
        while ((fab_rd(FAB_STATUS) & FAB_STATUS_ACTIVE) && !fab_elapsed(t0, ++polls, A3D_FAB_HANG_MS * 1000u))
            a3d_plat_gpu_relax();
        if (fab_rd(FAB_STATUS) & FAB_STATUS_ACTIVE)
        {
            fab_give_up();
            return;
        }
    }
    const uint32_t ctrl = fab_ctrl_bits(0);
    fab_wr(FAB_CTRL, ctrl | FAB_CTRL_ERRCLR);
    g_ctrl = ctrl;
    fab_wr(FAB_FE_FORCE, a3d_fab_fe_force);
    a3d_tex_l2_dirty = 1;               /* whatever the cache holds is from before: the first kick invalidates it */
}

static uint32_t a3d_fab_units(void)
{
    return g_units;
}

static uint32_t a3d_fab_orders(void)
{
    return A3D_ORD_LE | (g_cswap ? A3D_ORD_BE : 0u);
}

#define A3D_FAB_LOOK_US 100u
#if defined(__arm__) && !defined(__APPLE__)
#define A3D_R9_TEXT __attribute__((section(".r9_tail_text"), noinline))
#define A3D_R9_BSS  __attribute__((section(".bss.~r9")))
#else
#define A3D_R9_TEXT __attribute__((noinline))
#define A3D_R9_BSS
#endif
uint32_t a3d_fab_look_off A3D_R9_BSS;
uint32_t a3d_fab_looks_read A3D_R9_BSS, a3d_fab_looks_skipped A3D_R9_BSS;  /* register reads it made / answers it gave without */
static uint32_t g_look_t A3D_R9_BSS, g_look_valid A3D_R9_BSS, g_look_ticks A3D_R9_BSS;

A3D_R9_TEXT static uint32_t a3d_fab_completed_r9(void)
{
    if (a3d_fab_look_off || fab_running() == 0)
        return a3d_fab_completed();     /* nothing runs: no register read anyway (fab_poll) */
    if (!g_look_ticks)
        g_look_ticks = fab_ticks_us(A3D_FAB_LOOK_US);   /* (until the clock is known -- 0: every look reads) */
    const uint32_t now = a3d_plat_cycles();
    if (g_look_valid && g_look_ticks && now - g_look_t < g_look_ticks)
    {
        a3d_fab_looks_skipped++;
        return g_done;
    }
    g_look_t = now;
    g_look_valid = 1;
    a3d_fab_looks_read++;
    return a3d_fab_completed();
}

const a3d_backend a3d_be_fabric = {
    "FABRIC", A3D_BE_FABRIC, a3d_fab_submit, a3d_fab_progress, a3d_fab_completed_r9, a3d_fab_reset,
    1u,     /* async: the GPU completes by itself */
    1u,
    a3d_fab_units,
    a3d_fab_orders
};
