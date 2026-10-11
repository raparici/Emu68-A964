/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: MiniGL's deferred geometry batches. */

#include "a3d_mgl.h"

a3d_mgl_stats_t a3d_mcnt A3D_LATE_BSS;

A3D_LATE_CALL int32_t a3d_mgl_tris(a3d_ctx *c, uint32_t a1, uint32_t flags, uint32_t *binned)
{
    (void)flags;
    *binned = 0;
    if (!a3d_g_ok(a1, A3D_MGL_BLK_SIZE, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    const uint8_t *p = a3d_g_ptr(a1);
    if (a3d_be32(p) != A3D_MGL_MAGIC || a3d_be32(p + 4) != 1u)
        return W3D_ILLEGALINPUT;
    const uint32_t vtx = a3d_be32(p + 8), ntri = a3d_be32(p + 12);
    if (ntri > A3D_MGL_MAXTRI)
        return W3D_ILLEGALINPUT;
    if (ntri == 0)
        return W3D_SUCCESS;
    if (!a3d_g_ok(vtx, ntri * 3u * A3D_MGL_REC_SIZE, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    mgl_geom g;
    for (uint32_t i = 0; i < 16u; i++)
        g.mvp[i] = a3d_bef32(p + 28u + 4u * i);
    g.vp_x = (int32_t)a3d_be32(p + 92);
    g.vp_y = (int32_t)a3d_be32(p + 96);
    g.vp_w = (int32_t)a3d_be32(p + 100);
    g.vp_h = (int32_t)a3d_be32(p + 104);
    g.height = (int32_t)a3d_be32(p + 108);
    g.depth_n = a3d_bef32(p + 112);
    g.depth_f = a3d_bef32(p + 116);
    g.zoffset = a3d_bef32(p + 120);
    g.flags = a3d_be32(p + 124);
    return a3d_draw_mgl(c, &g, a3d_g_ptr(vtx), ntri, binned);
}

uint64_t a3d_mglc_oncall(a3d_ctx *c, uint32_t id, uint32_t d0, uint32_t a1);
A3D_LATE_CALL uint64_t a3d_oncall_more(a3d_ctx *c, uint32_t id, uint32_t d0, uint32_t a1)
{
    if (id != A3D_MGL_TRIS)
    {
        return a3d_mglc_oncall(c, id, d0, a1);
    }
    uint32_t r1 = 0;
    a3d_prof_begin(A3D_PROF_DRAW);
    const int32_t r = a3d_mgl_tris(c, a1, d0, &r1);
    a3d_prof_end(A3D_PROF_DRAW, &a3d_cnt.prof_draw);
    return (uint32_t)r | ((uint64_t)r1 << 32);
}
