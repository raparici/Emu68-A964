/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the geometry stage (cull, clip, to screen). */

#include "mgl_stage.h"

void mgl_stage_xform(const float *m, float x, float y, float z, float w, mgl_cvtx *o)
{
    o->x = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    o->y = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    o->z = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    o->w = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
}

void mgl_stage_to_screen(const mgl_geom *g, const mgl_cvtx *v, mgl_vtx *o)
{
    const float iw = 1.0f / v->w;
    const float nx = v->x * iw, ny = v->y * iw, nz = v->z * iw;
    const float hw = (float)g->vp_w * 0.5f, hh = (float)g->vp_h * 0.5f;
    o->x = (float)g->vp_x + (nx + 1.0f) * hw;
    const float yw = (float)g->vp_y + (ny + 1.0f) * hh;           /* GL window y, up */
    o->y = (float)g->height - yw;
    float z = g->depth_n + (nz + 1.0f) * ((g->depth_f - g->depth_n) * 0.5f);
    z = z + g->zoffset;
    o->z = z < 0.0f ? 0.0f : z > 1.0f ? 1.0f : z;
    o->w = iw;
    o->s = v->s;
    o->t = v->t;
    o->r = v->r;
    o->g = v->g;
    o->b = v->b;
    o->a = v->a;
}

static float area_ndc(const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *cc)
{
    const float ax = a->x / a->w, ay = a->y / a->w;
    const float bx = b->x / b->w, by = b->y / b->w;
    const float cx = cc->x / cc->w, cy = cc->y / cc->w;
    return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
}

static int culled(const mgl_geom *g, float area)
{
    if (!(g->flags & MGL_GC_CULL))
        return 0;
    const int ccw = area > 0.0f;
    const int front = (g->flags & MGL_GC_FRONT_CW) ? !ccw : ccw;
    if (g->flags & MGL_GC_CULL_BOTH)
        return 1;
    if (area == 0.0f)
        return 1;                               /* degenerate: no area to fill */
    return (g->flags & MGL_GC_CULL_FRONT) ? front : !front;
}

uint32_t mgl_stage_outcode(const mgl_cvtx *v)
{
    uint32_t o = 0;
    if (v->x < -v->w) o |= 1u;
    if (v->x > v->w)  o |= 2u;
    if (v->y < -v->w) o |= 4u;
    if (v->y > v->w)  o |= 8u;
    if (v->z < -v->w) o |= 16u;
    if (v->z > v->w)  o |= 32u;
    return o;
}

static float plane_dist(const mgl_cvtx *v, uint32_t p)
{
    switch (p)
    {
    case 0:  return v->x + v->w;
    case 1:  return v->w - v->x;
    case 2:  return v->y + v->w;
    case 3:  return v->w - v->y;
    case 4:  return v->z + v->w;
    default: return v->w - v->z;
    }
}

static void lerp(mgl_cvtx *o, const mgl_cvtx *a, const mgl_cvtx *b, float t)
{
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
    o->s = a->s + (b->s - a->s) * t;
    o->t = a->t + (b->t - a->t) * t;
    o->r = a->r + (b->r - a->r) * t;
    o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t;
    o->a = a->a + (b->a - a->a) * t;
}

#define MGL_CLIP_MAX 16u

void mgl_stage_tri(const mgl_geom *g, const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *cc, const mgl_stage_out *out)
{
    (*out->st_in)++;
    const uint32_t o0 = mgl_stage_outcode(a), o1 = mgl_stage_outcode(b), o2 = mgl_stage_outcode(cc);
    if (o0 & o1 & o2)
    {
        (*out->st_culled)++;                    /* all three outside one plane */
        return;
    }
    if (!(o0 | o1 | o2))
    {
        if (culled(g, area_ndc(a, b, cc)))
        {
            (*out->st_culled)++;
            return;
        }
        out->room(out->ud, 3, 1);
        const uint32_t base = *out->nv;
        mgl_stage_to_screen(g, a, &out->v[base]);
        mgl_stage_to_screen(g, b, &out->v[base + 1u]);
        mgl_stage_to_screen(g, cc, &out->v[base + 2u]);
        out->idx[3u * *out->nt] = (uint16_t)base;
        out->idx[3u * *out->nt + 1u] = (uint16_t)(base + 1u);
        out->idx[3u * *out->nt + 2u] = (uint16_t)(base + 2u);
        *out->nv += 3u;
        (*out->nt)++;
        return;
    }
    /* Sutherland-Hodgman against the planes the triangle crosses */
    mgl_cvtx buf[2][MGL_CLIP_MAX];
    uint32_t n = 3, cur = 0;
    buf[0][0] = *a;
    buf[0][1] = *b;
    buf[0][2] = *cc;
    const uint32_t any = o0 | o1 | o2;
    for (uint32_t p = 0; p < 6u && n >= 3u; p++)
    {
        if (!(any & (1u << p)))
            continue;
        const mgl_cvtx *in = buf[cur];
        mgl_cvtx *o = buf[cur ^ 1u];
        uint32_t m = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            const mgl_cvtx *p0 = &in[i], *p1 = &in[(i + 1u) % n];
            const float d0 = plane_dist(p0, p), d1 = plane_dist(p1, p);
            if (d0 >= 0.0f && m < MGL_CLIP_MAX)
                o[m++] = *p0;
            if ((d0 >= 0.0f) != (d1 >= 0.0f) && m < MGL_CLIP_MAX)
                lerp(&o[m++], p0, p1, d0 / (d0 - d1));
        }
        n = m;
        cur ^= 1u;
    }
    if (n < 3u)
    {
        (*out->st_culled)++;
        return;
    }
    const mgl_cvtx *v = buf[cur];
    if (culled(g, area_ndc(&v[0], &v[1], &v[2])))
    {
        (*out->st_culled)++;
        return;
    }
    (*out->st_clipped)++;
    out->room(out->ud, n, n - 2u);
    const uint32_t base = *out->nv;
    for (uint32_t i = 0; i < n; i++)
        mgl_stage_to_screen(g, &v[i], &out->v[base + i]);
    for (uint32_t i = 1; i + 1u < n; i++)       /* the clipped polygon as a fan */
    {
        out->idx[3u * *out->nt] = (uint16_t)base;
        out->idx[3u * *out->nt + 1u] = (uint16_t)(base + i);
        out->idx[3u * *out->nt + 2u] = (uint16_t)(base + i + 1u);
        (*out->nt)++;
    }
    *out->nv += n;
}
