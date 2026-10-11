/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: page protection of render targets behind the lazy waits. */

#include <stdint.h>
#include "support.h"
#include "a3d_fw_prot.h"

extern uint32_t mmu_table[4096];

#define AP_KEYS3D       A3D_FW_PROT_KEYS
#define AP_QKEYS        A3D_FW_PROT_QKEYS
#define AP_KEYS         (AP_KEYS3D + AP_QKEYS)
#define AP_TABLES       16u     /* split sections: 16 MB of protected targets at most              */
#define AP_WINDOWS      2u
#define AP_NOACCESS     0x230u  /* small page AP[2] (bit 9) | AP[1:0] (bits 5:4): cleared = no access */

#define AP_TOUCH_FAULT  1u
#define AP_TOUCH_FENCE  2u
#define AP_TOUCH_SHARED 0x100u  /* or'd into a fault's why: outside every target's own bytes          */

typedef struct
{
    uint32_t base, lo, hi;      /* hi == 0: unused.  [lo, hi) protected; [base, hi) is what fences match */
    uint32_t armed;
    uint32_t pending;
} ap_key;
typedef struct { uint32_t used, sec, l1_orig, l1_mine; } ap_split;

static ap_key   g_key[AP_KEYS];
static ap_split g_split[AP_TABLES];
static uint32_t g_l2[AP_TABLES][256] __attribute__((aligned(1024)));
static uint32_t g_l2_orig[AP_TABLES][256];
static uint32_t g_bits[AP_TABLES][8];
static struct { uint32_t lo, hi; } g_win[AP_WINDOWS];
static uint32_t g_hold;                                           /* every page accessible       */
uint64_t g_a3d_vstack[8192] __attribute__((aligned(16)));

volatile uint32_t g_a3d_prot_on;
volatile uint32_t g_a3d_agent_busy;
uint32_t g_ap_faults;                   /* every protection fault served here                             */
uint32_t g_ap_idle;
uint32_t g_ap_shared;                   /* of them outside every key's own bytes (a page shared)          */
uint32_t g_ap_self, g_ap_stale, g_ap_nested;   /* firmware touched a protected page / no key / nested   */
uint32_t g_ap_applies, g_ap_pages, g_ap_tlball, g_ap_tlbpages;   /* MMU work: applies, pages, flushes    */
uint32_t (*g_ap_touch)(uint32_t lo, uint32_t hi, uint32_t why);  /* a3d_fw.c: a3d_fw_touch_c   */
uint32_t (*g_ap_fence_try)(uint32_t fence, uint32_t a, uint32_t b);
uint32_t (*g_ap_fence_wait)(uint32_t fence, uint32_t a, uint32_t b);
uint32_t (*g_ap_qdrain)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
extern uint32_t emu68_fastbank_call4(void *fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
static uint32_t g_qfence;               /* the 3D fence the covered ops wait for                          */
static uint32_t g_qcovered;             /* the queue's keys are pending (WaitBlitter answered "idle")      */
uint32_t g_ap_qcovers, g_ap_qfaults, g_ap_qdma, g_ap_qrefused;   /* covers, CPU faults on them, DMA fences, refusals */

uint32_t a3d_fw_vfp_call(uint32_t (*fn)(uint32_t, uint32_t, uint32_t), uint32_t a, uint32_t b, uint32_t c);
#define A3D_VFP_SAVE    "       vpush   {d0-d15}                        \n" \
                        "       vpush   {d16-d31}                       \n"
#define A3D_VFP_RESTORE "       vpop    {d16-d31}                       \n" \
                        "       vpop    {d0-d15}                        \n"
asm(
"       .section .text                          \n"
"       .balign 4                               \n"
"       .fpu    neon                            \n"   /* this TU is -mgeneral-regs-only: enable the bank for the assembler */
"       .globl  a3d_fw_vfp_call                 \n"
"       .type   a3d_fw_vfp_call,%function       \n"
"a3d_fw_vfp_call:                               \n"
"       push    {r4, r5, r6, lr}                \n"
"       mov     r4, sp                          \n"
"       ldr     r5, =g_a3d_vstack + 65536       \n"
"       mov     sp, r5                          \n"
A3D_VFP_SAVE
"       vmrs    r6, fpscr                       \n"
"       mov     r5, #0                          \n"
"       vmsr    fpscr, r5                       \n"
"       mov     ip, r0                          \n"
"       mov     r0, r1                          \n"
"       mov     r1, r2                          \n"
"       mov     r2, r3                          \n"
"       blx     ip                              \n"
"       vmsr    fpscr, r6                       \n"
A3D_VFP_RESTORE
"       mov     sp, r4                          \n"
"       pop     {r4, r5, r6, pc}                \n"
"       .ltorg                                  \n"
);

/* ---- kernel statics: VA <-> PA (one delta, published in TTBR0: zz9000_board.c's zz_kva_to_pa) ---- */
static uint32_t ap_delta(void)
{
    uint32_t ttbr0;
    asm volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr0));
    return (ttbr0 & 0xFFFFC000u) - (uint32_t)(uintptr_t)mmu_table;
}

static void ap_clean(const void *p, uint32_t len)
{
    arm_flush_cache((uintptr_t)p, len);
}

static void ap_tlb_all(void)
{
    asm volatile("dsb" ::: "memory");
    asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));
    asm volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));      /* BPIALL                    */
    asm volatile("dsb; isb" ::: "memory");
    g_ap_tlball++;
}

static int ap_find(uint32_t sec)
{
    for (uint32_t i = 0; i < AP_TABLES; i++)
        if (g_split[i].used && g_split[i].sec == sec)
            return (int)i;
    return -1;
}

static uint32_t ap_free_tables(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < AP_TABLES; i++)
        n += !g_split[i].used;
    return n;
}

static int ap_splittable(uint32_t d)
{
    if ((d & 3u) == 2u)
        return !(d & (1u << 18));
    if ((d & 3u) == 1u)
    {
        const uint32_t va = (d & 0xFFFFFC00u) - ap_delta();
        return va >= 0xFF800000u;
    }
    return 0;
}

static int ap_split_new(uint32_t sec)
{
    uint32_t i = 0;
    while (i < AP_TABLES && g_split[i].used)
        i++;
    if (i == AP_TABLES)
        return -1;
    const uint32_t d = mmu_table[sec];
    uint32_t mine;
    const uint32_t l2pa = (uint32_t)(uintptr_t)g_l2[i] + ap_delta();
    if ((d & 3u) == 2u && !(d & (1u << 18)))
    {
        const uint32_t small = 0x2u | ((d >> 4) & 1u) | (d & 0x0Cu) | (((d >> 10) & 3u) << 4)
                             | (((d >> 12) & 7u) << 6) | (((d >> 15) & 1u) << 9) | (((d >> 16) & 1u) << 10)
                             | (((d >> 17) & 1u) << 11);
        const uint32_t pbase = d & 0xFFF00000u;
        for (uint32_t k = 0; k < 256u; k++)
            g_l2_orig[i][k] = (pbase + (k << 12)) | small;
        mine = (l2pa & 0xFFFFFC00u) | (d & 0x1E0u) | (((d >> 19) & 1u) << 3) | 0x1u;
    }
    else if ((d & 3u) == 1u)
    {
        const uint32_t va = (d & 0xFFFFFC00u) - ap_delta();
        if (va < 0xFF800000u)
            return -1;
        const uint32_t *src = (const uint32_t *)(uintptr_t)va;
        for (uint32_t k = 0; k < 256u; k++)
            g_l2_orig[i][k] = src[k];
        mine = (l2pa & 0xFFFFFC00u) | (d & 0x3FCu) | 0x1u;
    }
    else
        return -1;
    for (uint32_t k = 0; k < 256u; k++)
        g_l2[i][k] = g_l2_orig[i][k];
    for (uint32_t k = 0; k < 8u; k++)
        g_bits[i][k] = 0;
    ap_clean(g_l2[i], sizeof(g_l2[i]));
    asm volatile("dsb" ::: "memory");
    g_split[i].used = 1;
    g_split[i].sec = sec;
    g_split[i].l1_orig = d;
    g_split[i].l1_mine = mine;
    mmu_table[sec] = mine;
    ap_clean(&mmu_table[sec], 4);
    return (int)i;
}

static void ap_split_free(uint32_t i)
{
    ap_split *s = &g_split[i];
    if (mmu_table[s->sec] == s->l1_mine)
    {
        mmu_table[s->sec] = s->l1_orig;
        ap_clean(&mmu_table[s->sec], 4);
    }
    s->used = 0;
}

/* the pages of section `sec` that [lo, hi) touches */
static void ap_range_bits(uint32_t sec, uint32_t lo, uint32_t hi, uint32_t bits[8])
{
    const uint32_t s0 = sec << 20, s1 = s0 + 0x100000u;
    if (hi <= s0 || lo >= s1 || hi <= lo)
        return;
    const uint32_t a = lo > s0 ? lo : s0, b = hi < s1 ? hi : s1;
    for (uint32_t p = (a - s0) >> 12; p <= ((b - 1u) - s0) >> 12; p++)
        bits[p >> 5] |= 1u << (p & 31u);
}

#define AP_TLB_PAGES_MAX 32u    /* more changed pages than this in one apply: one TLBIALL instead */

static int ap_apply(void)
{
    int ok = 1, l1_changed = 0;
    uint32_t tlb_n = 0, tlb_va[AP_TLB_PAGES_MAX];
    g_ap_applies++;
    /* 1. a split for every section a registered key touches */
    for (uint32_t k = 0; k < AP_KEYS; k++)
    {
        if (!g_key[k].hi)
            continue;
        for (uint32_t sec = g_key[k].lo >> 20; sec <= (g_key[k].hi - 1u) >> 20; sec++)
            if (ap_find(sec) < 0)
            {
                if (ap_split_new(sec) < 0)
                    ok = 0;
                else
                    l1_changed = 1;
            }
    }
    /* 2. every split's pages: protected where an ARMED key says so */
    uint32_t pages = 0;
    for (uint32_t i = 0; i < AP_TABLES; i++)
    {
        ap_split *s = &g_split[i];
        if (!s->used)
            continue;
        uint32_t want[8], keep = 0;
        for (uint32_t w = 0; w < 8u; w++)
            want[w] = 0;
        for (uint32_t k = 0; k < AP_KEYS; k++)
        {
            if (!g_key[k].hi)
                continue;
            uint32_t touch[8];
            for (uint32_t w = 0; w < 8u; w++)
                touch[w] = 0;
            ap_range_bits(s->sec, g_key[k].lo, g_key[k].hi, touch);
            for (uint32_t w = 0; w < 8u; w++)
            {
                keep |= touch[w];
                if (g_key[k].armed && !g_hold)
                    want[w] |= touch[w];
            }
        }
        uint32_t dirty = 0;
        for (uint32_t w = 0; w < 8u; w++)
        {
            const uint32_t diff = want[w] ^ g_bits[i][w];
            if (diff)
            {
                for (uint32_t b = 0; b < 32u; b++)
                    if (diff & (1u << b))
                    {
                        const uint32_t p = w * 32u + b;
                        g_l2[i][p] = (want[w] & (1u << b)) ? (g_l2_orig[i][p] & ~AP_NOACCESS) : g_l2_orig[i][p];
                        if (tlb_n < AP_TLB_PAGES_MAX)
                            tlb_va[tlb_n] = (s->sec << 20) | (p << 12);
                        tlb_n++;
                    }
                g_bits[i][w] = want[w];
                dirty = 1;
            }
            for (uint32_t v = want[w]; v; v &= v - 1u)    /* no libgcc here: no __popcountsi2 */
                pages++;
        }
        if (dirty)
            ap_clean(g_l2[i], sizeof(g_l2[i]));
        /* 3. a split no registered key touches any more goes back to its section */
        if (!keep)
        {
            ap_split_free(i);
            l1_changed = 1;
        }
    }
    if (l1_changed || tlb_n > AP_TLB_PAGES_MAX)
        ap_tlb_all();
    else if (tlb_n)
    {
        asm volatile("dsb" ::: "memory");
        for (uint32_t t = 0; t < tlb_n; t++)
            asm volatile("mcr p15,0,%0,c8,c7,1" :: "r"(tlb_va[t]));   /* TLBIMVA: that page only */
        asm volatile("dsb; isb" ::: "memory");
        g_ap_tlbpages += tlb_n;
    }
    g_ap_pages = pages;
    g_a3d_prot_on = pages != 0;
    return ok;
}

void a3d_fw_prot_window(uint32_t idx, uint32_t lo, uint32_t bytes)
{
    if (idx >= AP_WINDOWS)
        return;
    g_win[idx].lo = lo;
    g_win[idx].hi = bytes ? lo + bytes : 0;
}

static int ap_in_window(uint32_t lo, uint32_t hi)
{
    for (uint32_t w = 0; w < AP_WINDOWS; w++)
        if (g_win[w].hi && lo >= g_win[w].lo && hi <= g_win[w].hi)
            return 1;
    return 0;
}

int a3d_fw_prot_set(uint32_t key, uint32_t base, uint32_t lo, uint32_t bytes)
{
    if (key >= AP_KEYS3D)
        return 0;
    if (bytes == 0)
    {
        if (g_key[key].hi)
        {
            g_key[key].hi = 0;
            g_key[key].armed = g_key[key].pending = 0;
            ap_apply();
        }
        return 1;
    }
    const uint32_t hi = lo + bytes;
    if (hi < lo || !ap_in_window(lo, hi))
        return 0;
    uint32_t need = 0;
    for (uint32_t sec = lo >> 20; sec <= (hi - 1u) >> 20; sec++)
    {
        if (ap_find(sec) >= 0)
            continue;
        if (!ap_splittable(mmu_table[sec]))
            return 0;
        need++;
    }
    if (need > ap_free_tables())
        return 0;
    g_key[key].base = base <= lo ? base : lo;
    g_key[key].lo = lo;
    g_key[key].hi = hi;
    g_key[key].armed = 1;
    g_key[key].pending = 1;
    if (!ap_apply())
    {
        g_key[key].hi = 0;
        g_key[key].armed = g_key[key].pending = 0;
        ap_apply();
        return 0;
    }
    return 1;
}

int a3d_fw_prot_pending(uint32_t key, uint32_t on)
{
    if (key >= AP_KEYS3D || !g_key[key].hi)
        return 0;
    g_key[key].pending = on ? 1u : 0u;
    if (on && !g_key[key].armed)
    {
        g_key[key].armed = 1;
        ap_apply();
    }
    return 1;
}

void a3d_fw_prot_hold(uint32_t on)
{
    if (g_hold == on)
        return;
    g_hold = on;
    ap_apply();
}

uint32_t a3d_fw_prot_held(void)
{
    return g_hold;
}

/* a guest reset: every key and window forgotten, every split restored (before the boards unmap) */
void a3d_fw_prot_reset(void)
{
    for (uint32_t k = 0; k < AP_KEYS; k++)
    {
        g_key[k].hi = 0;
        g_key[k].armed = g_key[k].pending = 0;
    }
    g_hold = 0;
    g_qcovered = 0;
    g_qfence = 0;
    ap_apply();
    for (uint32_t w = 0; w < AP_WINDOWS; w++)
        g_win[w].hi = 0;
}

static void ap_qfinish(void)
{
    if (g_qfence)
        a3d_fw_fence_wait(g_qfence);
    if (g_ap_qdrain)
        (void)emu68_fastbank_call4((void *)g_ap_qdrain, 0, 0, 0, 0);
}

int a3d_fw_prot_qcover(const uint32_t *lo, const uint32_t *hi, uint32_t n, uint32_t fence)
{
    if (n == 0 || n > AP_QKEYS)
        return 0;
    for (uint32_t i = 0; i < n; i++)
        if (hi[i] <= lo[i] || !ap_in_window(lo[i], hi[i]))
        {
            g_ap_qrefused++;
            return 0;
        }
    uint32_t same = 1;
    for (uint32_t i = 0; i < AP_QKEYS; i++)
    {
        const ap_key *k = &g_key[AP_KEYS3D + i];
        if (i < n ? (k->lo != lo[i] || k->hi != hi[i]) : k->hi != 0)
            same = 0;
    }
    if (same)
    {
        /* the same memory as the last cover: pending again; re-armed where a fault had disarmed it */
        uint32_t rearm = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            ap_key *k = &g_key[AP_KEYS3D + i];
            k->pending = 1;
            if (!k->armed)
            {
                k->armed = 1;
                rearm = 1;
            }
        }
        if (rearm && !ap_apply())
        {
            a3d_fw_prot_qdrop();
            g_ap_qrefused++;
            return 0;
        }
    }
    else
    {
        uint32_t need = 0;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t sec = lo[i] >> 20; sec <= (hi[i] - 1u) >> 20; sec++)
            {
                if (ap_find(sec) >= 0)
                    continue;
                if (!ap_splittable(mmu_table[sec]))
                {
                    g_ap_qrefused++;
                    return 0;
                }
                need++;
            }
        if (need > ap_free_tables())
        {
            g_ap_qrefused++;
            return 0;
        }
        for (uint32_t i = 0; i < AP_QKEYS; i++)
        {
            ap_key *k = &g_key[AP_KEYS3D + i];
            if (i < n)
            {
                k->base = lo[i];
                k->lo = lo[i];
                k->hi = hi[i];
                k->armed = 1;
                k->pending = 1;
            }
            else
            {
                k->hi = 0;
                k->armed = k->pending = 0;
            }
        }
        if (!ap_apply())
        {
            a3d_fw_prot_qdrop();
            g_ap_qrefused++;
            return 0;
        }
    }
    g_qfence = fence;
    g_qcovered = 1;
    g_ap_qcovers++;
    return 1;
}

void a3d_fw_prot_quncover(void)
{
    for (uint32_t i = 0; i < AP_QKEYS; i++)
        g_key[AP_KEYS3D + i].pending = 0;
    g_qcovered = 0;
    g_qfence = 0;
}

/* forget the queue keys (the card was re-initialised: a mode change or a guest reset) */
void a3d_fw_prot_qdrop(void)
{
    uint32_t any = 0;
    for (uint32_t i = 0; i < AP_QKEYS; i++)
    {
        ap_key *k = &g_key[AP_KEYS3D + i];
        any |= k->hi != 0;
        k->hi = 0;
        k->armed = k->pending = 0;
    }
    g_qcovered = 0;
    g_qfence = 0;
    if (any)
        ap_apply();
}

uint32_t a3d_fw_prot_qcovered(void)
{
    return g_qcovered;
}

static int ap_page_protected(uint32_t a)
{
    const int i = ap_find(a >> 20);
    if (i < 0)
        return 0;
    const uint32_t p = (a >> 12) & 0xFFu;
    return (g_bits[i][p >> 5] >> (p & 31u)) & 1u;
}

/* the registered keys covering the page of `a` (their ranges rounded out to pages) */
static int ap_key_on_page(const ap_key *k, uint32_t page)
{
    return k->hi && page < ((k->hi + 0xFFFu) & ~0xFFFu) && page + 0x1000u > (k->lo & ~0xFFFu);
}

int a3d_fw_prot_fault(uint32_t dfar)
{
    if (!ap_page_protected(dfar))
        return 0;
    g_ap_faults++;
    uint32_t spsr;
    asm volatile("mrs %0, spsr" : "=r"(spsr));
    if ((spsr & 0x1Fu) == 0x17u || g_a3d_agent_busy)
    {
        if ((spsr & 0x1Fu) == 0x17u)
            g_ap_nested++;
        else
            g_ap_self++;
        a3d_fw_prot_hold(1);
        return 1;
    }
    const uint32_t page = dfar & ~0xFFFu;
    uint32_t pending = 0, qpending = 0, inside = 0, any = 0;
    for (uint32_t k = 0; k < AP_KEYS; k++)
    {
        if (!g_key[k].armed || !ap_key_on_page(&g_key[k], page))
            continue;
        any = 1;
        if (k < AP_KEYS3D)
            pending |= g_key[k].pending;
        else
            qpending |= g_key[k].pending;
        if (dfar >= g_key[k].lo && dfar < g_key[k].hi)
            inside = 1;
    }
    if (!inside)
        g_ap_shared++;
    if (pending && g_ap_touch)
    {
        g_a3d_agent_busy = 1;
        a3d_fw_vfp_call(g_ap_touch, page, page + 0x1000u, AP_TOUCH_FAULT | (inside ? 0u : AP_TOUCH_SHARED));
        g_a3d_agent_busy = 0;
    }
    if (qpending)
    {
        g_ap_qfaults++;
        ap_qfinish();
    }
    else if (!pending)
    {
        if (any)
            g_ap_idle++;                /* the work was already finished: nothing to run */
        else
            g_ap_stale++;               /* a protected page no key claims: cannot happen; released below */
    }
    for (uint32_t k = 0; k < AP_KEYS; k++)
        if (ap_key_on_page(&g_key[k], page))
            g_key[k].armed = 0;
    ap_apply();
    if (ap_page_protected(dfar))
    {
        g_ap_stale++;
        a3d_fw_prot_hold(1);
    }
    return 1;
}

void a3d_fw_fence(uint32_t lo, uint32_t hi)
{
    if (!g_a3d_prot_on || g_a3d_agent_busy || !g_ap_touch)
        return;
    uint32_t hit = 0;
    for (uint32_t k = 0; k < AP_KEYS3D; k++)      /* the 3D targets only: the card orders its own queue */
        if (g_key[k].hi && g_key[k].pending && lo < g_key[k].hi && hi > g_key[k].base)
            hit = 1;
    if (!hit)
        return;
    g_a3d_agent_busy = 1;
    a3d_fw_vfp_call(g_ap_touch, lo, hi, AP_TOUCH_FENCE);
    g_a3d_agent_busy = 0;
}

void a3d_fw_fence_dma(uint32_t lo, uint32_t hi)
{
    a3d_fw_fence(lo, hi);
    if (!g_qcovered || g_a3d_agent_busy)
        return;
    uint32_t hit = 0;
    for (uint32_t i = 0; i < AP_QKEYS; i++)
    {
        const ap_key *k = &g_key[AP_KEYS3D + i];
        if (k->hi && k->pending && lo < k->hi && hi > k->lo)
            hit = 1;
    }
    if (!hit)
        return;
    g_ap_qdma++;
    ap_qfinish();
}

extern uint32_t a3d_fence_of(uint32_t lo, uint32_t hi);
extern uint32_t a3d_fence_presented(uint32_t fence);

uint32_t a3d_fw_fence_of(uint32_t lo, uint32_t hi)
{
    if (!g_a3d_prot_on)
        return 0;
    uint32_t hit = 0;
    for (uint32_t k = 0; k < AP_KEYS3D; k++)
        if (g_key[k].hi && g_key[k].pending && lo < g_key[k].hi && hi > g_key[k].base)
            hit = 1;
    return hit ? a3d_fence_of(lo, hi) : 0;
}

static uint32_t ap_fence_try(uint32_t fence, uint32_t every);
uint32_t a3d_fw_fence_try(uint32_t fence)
{
    return ap_fence_try(fence, 8u);
}

uint32_t a3d_fw_fence_kick(uint32_t fence)
{
    return ap_fence_try(fence, 1u);
}

static uint32_t ap_fence_try(uint32_t fence, uint32_t every)
{
    if (fence == 0 || a3d_fence_presented(fence))
        return 1;
    if (g_a3d_agent_busy || !g_ap_fence_try)
        return 0;
    {
        static uint32_t n_ask;
        if (++n_ask % every)
            return 0;
    }
    g_a3d_agent_busy = 1;
    const uint32_t ready = a3d_fw_vfp_call(g_ap_fence_try, fence, 0, 0);
    g_a3d_agent_busy = 0;
    return ready;
}

void a3d_fw_fence_wait(uint32_t fence)
{
    if (fence == 0 || a3d_fence_presented(fence) || g_a3d_agent_busy || !g_ap_fence_wait)
        return;
    g_a3d_agent_busy = 1;
    a3d_fw_vfp_call(g_ap_fence_wait, fence, 0, 0);
    g_a3d_agent_busy = 0;
}
