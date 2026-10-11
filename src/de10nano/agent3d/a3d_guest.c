/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the guest memory range table. */

#include "a3d_guest.h"

static a3d_grange g_ranges[A3D_GR_MAX];
static uint32_t g_nranges;
static uint32_t g_overlap;
const a3d_grange *g_a3d_glast;

void a3d_guest_reset(void)
{
    memset(g_ranges, 0, sizeof(g_ranges));
    g_nranges = 0;
    g_overlap = 0;
    g_a3d_glast = 0;
}

int a3d_guest_add(uint32_t lo, uint32_t size, uint8_t *host, uint32_t phys, uint32_t flags)
{
    if (g_nranges >= A3D_GR_MAX || size == 0 || host == 0)
        return 0;
    const uint32_t hi = lo + size;
    if (hi < lo || hi == 0)
        return 0;
    for (uint32_t i = 0; i < g_nranges; i++)
        if (lo < g_ranges[i].hi && g_ranges[i].lo < hi)
            g_overlap = 1;
    g_a3d_glast = 0;
    a3d_grange *r = &g_ranges[g_nranges++];
    r->lo = lo;
    r->hi = hi;
    r->host = host;
    r->flags = flags;
    r->phys = phys;
    return 1;
}

const a3d_grange *a3d_g_find(uint32_t addr, uint32_t len)
{
    if (len == 0)
        len = 1;
    const uint32_t last = addr + len - 1u;
    if (last < addr)                    /* the span wraps past 4 GB */
        return 0;
    for (uint32_t i = 0; i < g_nranges; i++)
    {
        const a3d_grange *r = &g_ranges[i];
        if (addr >= r->lo && last < r->hi)
        {
            if (!g_overlap)
                g_a3d_glast = r;        /* unique: the next lookup inside it needs no walk (a3d_guest.h) */
            return r;
        }
    }
    return 0;
}

uint32_t a3d_g_phys(uint32_t addr)
{
    const a3d_grange *r = a3d_g_find(addr, 1);
    return (r && r->phys) ? r->phys + (addr - r->lo) : 0u;
}

int a3d_g_target_ok(uint32_t addr, uint32_t bprow, uint32_t rows)
{
    if (bprow == 0 || rows == 0 || bprow > 0x10000u || rows > 0x1000u)
        return 0;
    return a3d_g_ok(addr, bprow * rows, A3D_GR_READ | A3D_GR_WRITE | A3D_GR_TARGET);
}

const a3d_grange *a3d_guest_ranges(uint32_t *n)
{
    *n = g_nranges;
    return g_ranges;
}
