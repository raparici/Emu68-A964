/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the NULL backend (every submission completes at once). */

#include "a3d_agent.h"

static uint32_t g_null_done;

static int a3d_null_submit(const a3d_listset *ls)
{
    for (uint32_t b = 0; b < ls->bands && b < A3D_MAX_BANDS; b++)
    {
        a3d_walk w;
        const int ok = a3d_walk_begin(&w, ls, b);
        if (ok == 0)
            continue;
        if (ok < 0)
        {
            a3d_cnt.list_errors++;
            continue;
        }
        uint32_t res = 0;
        a3d_cmd c;
        int r;
        while ((r = a3d_walk_next(&w, &c)) > 0)
        {
            a3d_cnt.null_cmds++;
            const uint32_t kind = c.op & RIX_OP_MASK;
            if (kind == RIX_OP_RENDER_CONFIG && (c.op & 0x0FFFFFFFu) == RIX_REG_RENDER_RESOLUTION)
                res = rix_get32(c.payload);
            if (kind == RIX_OP_FRAMEBUFFER
                && ((c.op >> RIX_FB_SIZE_SHIFT) & RIX_FB_SIZE_MASK) != (res & 0x7FFu) * ((res >> 16) & 0x7FFu))
                a3d_cnt.list_errors++;
        }
        if (r < 0)
            a3d_cnt.list_errors++;
    }
    g_null_done = ls->fence;
    return 0;
}

static uint32_t a3d_null_progress(uint32_t budget)
{
    (void)budget;
    return g_null_done;
}

static uint32_t a3d_null_completed(void)
{
    return g_null_done;
}

static void a3d_null_reset(void)
{
    g_null_done = 0;
}

static uint32_t a3d_null_units(void)
{
    return 0;
}

static uint32_t a3d_null_orders(void)
{
    return A3D_ORD_BE;          /* as SOFT, so a target binds alike */
}

const a3d_backend a3d_be_null = {
    "NULL", A3D_BE_NULL, a3d_null_submit, a3d_null_progress, a3d_null_completed, a3d_null_reset,
    1u,     /* async: every submission is complete when submit returns */
    0u,     /* its colour order does not matter (it draws nothing): the SOFT one, so a target binds alike */
    a3d_null_units,
    a3d_null_orders
};
