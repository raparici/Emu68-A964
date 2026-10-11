/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the second texture unit's per-vertex pass. */

#ifndef MGL_MTEX_INL_H
#define MGL_MTEX_INL_H

/* a kept triangle: three pool indices, the primitive's run */
static inline __attribute__((always_inline)) void mt_tri(mgl_ctx5 *x5, uint32_t a, uint32_t b, uint32_t d)
{
    if (x5->mt_ntri >= x5->mt_captri)
    {
        x5->mt_dropped++;                       /* (room is made before the vertex: keep / mt_vertex_fast) */
        return;
    }
    mgl_mtex_t *t = &x5->mt_tri[x5->mt_ntri++];
    t->v[0] = (uint16_t)a;
    t->v[1] = (uint16_t)b;
    t->v[2] = (uint16_t)d;
    t->run = (uint16_t)x5->mt_cur;
}

static inline __attribute__((always_inline)) void mt_step(mgl_ctx5 *x5, uint32_t n)
{
    const uint32_t k = x5->mt_k++;
    uint32_t *w = x5->mt_w;
    const uint32_t m0 = k % 3u, m1 = (k + 1u) % 3u, m2 = (k + 2u) % 3u;
    switch (x5->mt_prim)
    {
    case GL_TRIANGLES:
        if (m0 == 2u)
            mt_tri(x5, w[0], w[1], n);
        break;
    case GL_TRIANGLE_STRIP:                     /* an odd triangle swaps its first two */
        if (k >= 2u)
        {
            if ((k - 2u) & 1u)
                mt_tri(x5, w[m2], w[m1], n);
            else
                mt_tri(x5, w[m1], w[m2], n);
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (k == 0u)
            x5->mt_c0 = n;
        else if (k >= 2u)
            mt_tri(x5, x5->mt_c0, w[m2], n);
        break;
    case GL_QUADS:                              /* (i, i+1, i+3), (i+1, i+2, i+3) */
        if ((k & 3u) == 3u)
        {
            mt_tri(x5, w[m0], w[m1], n);
            mt_tri(x5, w[m1], w[m2], n);
        }
        break;
    case GL_QUAD_STRIP:                         /* (i, i+1, i+3), (i, i+3, i+2) */
        if (k >= 3u && (k & 1u))
        {
            mt_tri(x5, w[m0], w[m1], n);
            mt_tri(x5, w[m0], n, w[m2]);
        }
        break;
    default:
        break;
    }
    w[m0] = n;
}

/* unit 1's coordinates as the pass draws them (s / q, t / q) */
static inline __attribute__((always_inline)) void mt_st_of(const float *tc, mgl_mtex_v *o)
{
    const float q = tc[3];
    o->s = q == 1.0f || q == 0.0f ? tc[0] : tc[0] / q;
    o->t = q == 1.0f || q == 0.0f ? tc[1] : tc[1] / q;
}

static inline __attribute__((always_inline)) void mt_kept(mgl_ctx5 *x5, mgl_mtex_v *o)
{
    if (x5->mt_prim == GL_POLYGON && x5->mt_k > 0u && x5->mt_run[x5->mt_cur].st.shade_model == GL_FLAT)
    {
        const mgl_mtex_v *f = &x5->mt_pool[x5->mt_c0];
        o->r = f->r; o->g = f->g; o->b = f->b; o->a = f->a;
    }
    mt_step(x5, x5->mt_npool++);
}

static inline __attribute__((always_inline)) int mt_vertex_fast(GLcontext c, mgl_ctx5 *x5, float x, float y, float z,
                                                                 float w)
{
    if (!c->in_begin || x5->mt_npool >= x5->mt_cappool || x5->mt_ntri + 2u > x5->mt_captri)
        return 0;
    mgl_mtex_v *o = &x5->mt_pool[x5->mt_npool];
    o->x = x; o->y = y; o->z = z; o->w = w;
    mt_st_of(c->cur_tex[1], o);
    o->r = c->cur_color[0]; o->g = c->cur_color[1]; o->b = c->cur_color[2]; o->a = c->cur_color[3];
    mt_kept(x5, o);
    return 1;
}

#endif
