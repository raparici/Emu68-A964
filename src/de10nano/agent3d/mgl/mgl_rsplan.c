/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the Warp3D state a raster state needs sent. */

#include "mgl_rsplan.h"

/* floats compared as bits: a value is "the same" only if Warp3D would get the same bits */
static uint32_t fbits(float f)
{
    union { float f; uint32_t u; } x;
    x.f = f;
    return x.u;
}
static int fne(float a, float b) { return fbits(a) != fbits(b); }

static mgl_rptex *slot(mgl_rplan *p, uint32_t h)
{
    return &p->tex[(h ^ (h >> 3) ^ (h >> 11)) % MGL_RP_TEXN];
}

void mgl_rp_reset(mgl_rplan *p)
{
    p->known = 0;
    for (uint32_t i = 0; i < MGL_RP_TEXN; i++)
        p->tex[i].h = 0;
}

void mgl_rp_frame(mgl_rplan *p)
{
    p->known = 0;                           /* the texture objects keep their own (p->tex) */
}

void mgl_rp_tex_forget(mgl_rplan *p, uint32_t h)
{
    if (!h)
        return;
    mgl_rptex *t = slot(p, h);
    if (t->h == h)
        t->h = 0;
    if (p->bound == h)
        p->known &= ~MGL_RP_BIND;           /* a freed object may be bound still, and a new one may get its address */
}

uint32_t mgl_rp_plan(mgl_rplan *p, const mgl_raster *r)
{
    const uint32_t k = p->known;
    uint32_t m = 0;
#define NEED(bit, changed) do { if (!(k & (bit)) || (changed)) m |= (bit); } while (0)
    const uint32_t tm = r->tex != 0;
    NEED(MGL_RP_TEXMAP, p->texmap != tm);
    if (r->tex)
    {
        NEED(MGL_RP_BIND, p->bound != r->tex);
        mgl_rptex *t = slot(p, r->tex);
        if (t->h != r->tex)
            m |= MGL_RP_TEXOBJ;
        else
        {
            if (t->min_filter != r->min_filter || t->mag_filter != r->mag_filter)
                m |= MGL_RP_FILTER;
            if (t->wrap_s != r->wrap_s || t->wrap_t != r->wrap_t)
                m |= MGL_RP_WRAP;
            if (t->texenv != r->texenv || fne(t->envcolor[0], r->envcolor[0]) || fne(t->envcolor[1], r->envcolor[1])
                || fne(t->envcolor[2], r->envcolor[2]) || fne(t->envcolor[3], r->envcolor[3]))
                m |= MGL_RP_ENV;
        }
        t->h = r->tex;
        t->min_filter = r->min_filter;
        t->mag_filter = r->mag_filter;
        t->wrap_s = r->wrap_s;
        t->wrap_t = r->wrap_t;
        t->texenv = r->texenv;
        for (int i = 0; i < 4; i++)
            t->envcolor[i] = r->envcolor[i];
        p->bound = r->tex;
    }
    p->texmap = tm;
    NEED(MGL_RP_BLEND, p->blend != r->blend);
    p->blend = r->blend;
    if (r->blend)
    {
        NEED(MGL_RP_BLENDFN, p->blend_src != r->blend_src || p->blend_dst != r->blend_dst);
        p->blend_src = r->blend_src;
        p->blend_dst = r->blend_dst;
    }
    NEED(MGL_RP_ZBUF, p->ztest != r->depth_test || p->zmask != r->depth_mask);
    p->ztest = r->depth_test;
    p->zmask = r->depth_mask;
    if (r->depth_test)
    {
        NEED(MGL_RP_ZFUNC, p->zfunc != r->depth_func);
        p->zfunc = r->depth_func;
    }
    NEED(MGL_RP_ALPHA, p->atest != r->alpha_test);
    p->atest = r->alpha_test;
    if (r->alpha_test)
    {
        NEED(MGL_RP_ALPHAFN, p->afunc != r->alpha_func || fne(p->aref, r->alpha_ref));
        p->afunc = r->alpha_func;
        p->aref = r->alpha_ref;
    }
    NEED(MGL_RP_FOG, p->fog != r->fog);
    p->fog = r->fog;
    if (r->fog)
    {
        NEED(MGL_RP_FOGP, p->fog_mode != r->fog_mode || fne(p->fog_ws, r->fog_wstart) || fne(p->fog_we, r->fog_wend)
             || fne(p->fog_density, r->fog_density) || fne(p->fog_col[0], r->fog_color[0])
             || fne(p->fog_col[1], r->fog_color[1]) || fne(p->fog_col[2], r->fog_color[2]));
        p->fog_mode = r->fog_mode;
        p->fog_ws = r->fog_wstart;
        p->fog_we = r->fog_wend;
        p->fog_density = r->fog_density;
        for (int i = 0; i < 3; i++)
            p->fog_col[i] = r->fog_color[i];
    }
    NEED(MGL_RP_SCISSOR, p->scissor != r->scissor);
    p->scissor = r->scissor;
    if (r->scissor)
    {
        NEED(MGL_RP_SCBOX, p->sc[0] != r->sc_x || p->sc[1] != r->sc_y || p->sc[2] != r->sc_w || p->sc[3] != r->sc_h);
        p->sc[0] = r->sc_x;
        p->sc[1] = r->sc_y;
        p->sc[2] = r->sc_w;
        p->sc[3] = r->sc_h;
    }
    NEED(MGL_RP_CMASK, p->cmask != r->color_mask);
    p->cmask = r->color_mask;
    NEED(MGL_RP_DITHER, p->dither != r->dither);
    p->dither = r->dither;
    NEED(MGL_RP_PERSP, p->persp != r->persp);
    p->persp = r->persp;
#undef NEED
    p->known |= m & ~MGL_RP_TEXOBJ;
    p->st_plans++;
    if (!k)
        p->st_full++;                       /* nothing was known: everything went (a new context, a new frame) */
    for (uint32_t b = m; b; b &= b - 1u)
        p->st_groups++;
    return m;
}
