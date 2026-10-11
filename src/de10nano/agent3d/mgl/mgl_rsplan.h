/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the Warp3D state a raster state needs sent. */

#ifndef MGL_RSPLAN_H
#define MGL_RSPLAN_H

#include <stdint.h>
#include "mgl_core.h"

/* the groups of a push (each one Warp3D call, ZBUF two) */
#define MGL_RP_TEXMAP   0x00001u
#define MGL_RP_BIND     0x00002u
#define MGL_RP_FILTER   0x00004u
#define MGL_RP_WRAP     0x00008u
#define MGL_RP_ENV      0x00010u
#define MGL_RP_BLEND    0x00020u
#define MGL_RP_BLENDFN  0x00040u            /* W3D_SetBlendMode                                           */
#define MGL_RP_ZBUF     0x00080u
#define MGL_RP_ZFUNC    0x00100u            /* W3D_SetZCompareMode                                        */
#define MGL_RP_ALPHA    0x00200u
#define MGL_RP_ALPHAFN  0x00400u            /* W3D_SetAlphaMode                                           */
#define MGL_RP_FOG      0x00800u
#define MGL_RP_FOGP     0x01000u            /* W3D_SetFogParams                                           */
#define MGL_RP_SCISSOR  0x02000u
#define MGL_RP_SCBOX    0x04000u            /* W3D_SetScissor                                             */
#define MGL_RP_CMASK    0x08000u            /* W3D_SetColorMask                                           */
#define MGL_RP_DITHER   0x10000u
#define MGL_RP_PERSP    0x20000u
#define MGL_RP_ALL      0x3FFFFu
#define MGL_RP_TEXOBJ   (MGL_RP_FILTER | MGL_RP_WRAP | MGL_RP_ENV)   /* a texture object's own                   */

#define MGL_RP_TEXN     256u                /* the per-texture cache: direct-mapped by handle (a collision costs a push) */

typedef struct mgl_rptex
{
    uint32_t h;                             /* the handle these are for, 0 = nothing known                */
    uint32_t min_filter, mag_filter, wrap_s, wrap_t, texenv;
    float    envcolor[4];
} mgl_rptex;

typedef struct mgl_rplan
{
    uint32_t  known;                        /* the context-wide MGL_RP_* groups whose values below Warp3D has */
    uint32_t  texmap, bound;
    uint32_t  blend, blend_src, blend_dst;
    uint32_t  ztest, zmask, zfunc;
    uint32_t  atest, afunc;
    float     aref;
    uint32_t  fog, fog_mode;
    float     fog_ws, fog_we, fog_density, fog_col[3];
    uint32_t  scissor;
    int32_t   sc[4];
    uint32_t  cmask, dither, persp;
    mgl_rptex tex[MGL_RP_TEXN];
    uint32_t  st_plans, st_groups, st_full;  /* plans, groups sent, plans that sent everything (statistics) */
} mgl_rplan;

void     mgl_rp_reset(mgl_rplan *p);                    /* a new Warp3D context: nothing known, textures included */
void     mgl_rp_frame(mgl_rplan *p);                    /* a frame / draw-region boundary: the context-wide groups  */
void     mgl_rp_tex_forget(mgl_rplan *p, uint32_t h);   /* a texture object created or freed under this handle      */
uint32_t mgl_rp_plan(mgl_rplan *p, const mgl_raster *r);    /* the groups to send for r; then r is what Warp3D has  */

#endif
