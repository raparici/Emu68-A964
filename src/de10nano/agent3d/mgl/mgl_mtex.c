/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the second texture unit. */

#include <string.h>
#include "mgl_core.h"
#include "mgl_mtex_inl.h"

#define MT_POOL_MAX  32768u
#define MT_TRI_MAX   16384u
#define MT_RUN_MAX   256u                       /* states                                                          */
#define MT_ROOM      4096u                      /* a primitive starting with less room than this draws the pass first */

typedef char mgl_mtex_snap_is_words[(sizeof(mgl_mtex_snap) % 4u) == 0 ? 1 : -1];

static int same_words(const void *a, const void *b, uint32_t n)
{
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    for (uint32_t i = 0; i < n / 4u; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static int grow(GLcontext c, void **p, uint32_t *cap, uint32_t need, uint32_t each, uint32_t max)
{
    if (need <= *cap)
        return 1;
    if (need > max || !c->sink.alloc)
        return 0;
    uint32_t n = *cap ? *cap : 256u;
    while (n < need)
        n *= 2u;
    if (n > max)
        n = max;
    void *q = c->sink.alloc(c->sink.ud, n * each);
    if (!q)
        return 0;
    if (*p)
    {
        memcpy(q, *p, *cap * each);
        if (c->sink.free)
            c->sink.free(c->sink.ud, *p, *cap * each);
    }
    *p = q;
    *cap = n;
    return 1;
}

int mgl_mtex_unit1(GLcontext c)
{
    if (!c->en_tex2d[1])
        return 0;
    const uint32_t n = c->bound[1];
    return n < MGL_MAX_TEXNAMES && c->tex[n].used && c->tex[n].nlevels;
}

static inline __attribute__((always_inline)) int kept_kind(GLenum m)   /* the primitives that make triangles */
{
    return m == GL_TRIANGLES || m == GL_TRIANGLE_STRIP || m == GL_TRIANGLE_FAN || m == GL_QUADS ||
           m == GL_QUAD_STRIP || m == GL_POLYGON;
}

static void snap_get(GLcontext c, const mgl_ctx5 *x5, mgl_mtex_snap *s)
{
    s->tex = c->bound[1];
    s->env1 = c->texenv[1];
    s->env1_set = x5->mt_env1_set;
    memcpy(s->envcolor1, c->envcolor[1], sizeof(s->envcolor1));
    s->mv = c->mv[c->mv_sp];
    s->proj = c->proj[c->proj_sp];
    s->vp_x = c->vp_x; s->vp_y = c->vp_y; s->vp_w = c->vp_w; s->vp_h = c->vp_h;
    s->depth_n = c->depth_n; s->depth_f = c->depth_f; s->zoffset = c->zoffset;
    s->en_cull = c->en_cull; s->cull_mode = c->cull_mode; s->front_face = c->front_face; s->shade_model = c->shade_model;
    s->en_depth = c->en_depth; s->depth_func = c->depth_func; s->depth_mask = c->depth_mask;
    s->en_alpha = c->en_alpha; s->alpha_func = c->alpha_func; s->alpha_ref = c->alpha_ref;
    s->en_fog = c->en_fog; s->fog_mode = c->fog_mode;
    s->fog_start = c->fog_start; s->fog_end = c->fog_end; s->fog_density = c->fog_density;
    memcpy(s->fog_color, c->fog_color, sizeof(s->fog_color));
    s->en_scissor = c->en_scissor; s->sc_x = c->sc_x; s->sc_y = c->sc_y; s->sc_w = c->sc_w; s->sc_h = c->sc_h;
    s->color_mask = c->color_mask; s->en_dither = c->en_dither; s->en_persp = c->en_persp;
}

static void snap_put(GLcontext c, const mgl_mtex_snap *s)     /* the geometry and the raster state (not unit 1's) */
{
    c->mv[c->mv_sp] = s->mv;
    c->proj[c->proj_sp] = s->proj;
    c->vp_x = s->vp_x; c->vp_y = s->vp_y; c->vp_w = s->vp_w; c->vp_h = s->vp_h;
    c->depth_n = s->depth_n; c->depth_f = s->depth_f; c->zoffset = s->zoffset;
    c->en_cull = s->en_cull; c->cull_mode = s->cull_mode; c->front_face = s->front_face; c->shade_model = s->shade_model;
    c->en_depth = s->en_depth; c->depth_func = s->depth_func; c->depth_mask = s->depth_mask;
    c->en_alpha = s->en_alpha; c->alpha_func = s->alpha_func; c->alpha_ref = s->alpha_ref;
    c->en_fog = s->en_fog; c->fog_mode = s->fog_mode;
    c->fog_start = s->fog_start; c->fog_end = s->fog_end; c->fog_density = s->fog_density;
    memcpy(c->fog_color, s->fog_color, sizeof(c->fog_color));
    c->en_scissor = s->en_scissor; c->sc_x = s->sc_x; c->sc_y = s->sc_y; c->sc_w = s->sc_w; c->sc_h = s->sc_h;
    c->color_mask = s->color_mask; c->en_dither = s->en_dither; c->en_persp = s->en_persp;
    c->mvp_dirty = 1;
    c->geom_dirty = 1;
    c->rs_dirty = 1;
}

static int flushable(const mgl_ctx5 *x5)
{
    return x5->mt_env1_set || x5->mt_args_known;
}

static void reset(mgl_ctx5 *x5)
{
    x5->mt_npool = x5->mt_ntri = x5->mt_nrun = 0;
    x5->mt_live = x5->mt_rec = 0;
    x5->mt_k = 0;
}

static inline __attribute__((always_inline)) uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static inline __attribute__((always_inline)) int snap_is(GLcontext c, const mgl_ctx5 *x5, const mgl_mtex_snap *s)
{
    if (s->tex != c->bound[1] || s->env1 != c->texenv[1] || s->env1_set != x5->mt_env1_set)
        return 0;
    if (s->vp_x != c->vp_x || s->vp_y != c->vp_y || s->vp_w != c->vp_w || s->vp_h != c->vp_h ||
        fbits(s->depth_n) != fbits(c->depth_n) || fbits(s->depth_f) != fbits(c->depth_f) ||
        fbits(s->zoffset) != fbits(c->zoffset) || s->en_cull != c->en_cull || s->cull_mode != c->cull_mode ||
        s->front_face != c->front_face || s->shade_model != c->shade_model || s->en_depth != c->en_depth ||
        s->depth_func != c->depth_func || s->depth_mask != c->depth_mask || s->en_alpha != c->en_alpha ||
        s->alpha_func != c->alpha_func || fbits(s->alpha_ref) != fbits(c->alpha_ref) || s->en_fog != c->en_fog ||
        s->fog_mode != c->fog_mode || fbits(s->fog_start) != fbits(c->fog_start) ||
        fbits(s->fog_end) != fbits(c->fog_end) || fbits(s->fog_density) != fbits(c->fog_density) ||
        s->en_scissor != c->en_scissor || s->sc_x != c->sc_x || s->sc_y != c->sc_y || s->sc_w != c->sc_w ||
        s->sc_h != c->sc_h || s->color_mask != c->color_mask || s->en_dither != c->en_dither ||
        s->en_persp != c->en_persp)
        return 0;
    return same_words(s->envcolor1, c->envcolor[1], (uint32_t)sizeof(s->envcolor1)) &&
           same_words(s->fog_color, c->fog_color, (uint32_t)sizeof(s->fog_color)) &&
           same_words(&s->mv, &c->mv[c->mv_sp], (uint32_t)sizeof(s->mv)) &&
           same_words(&s->proj, &c->proj[c->proj_sp], (uint32_t)sizeof(s->proj));
}

/* this primitive's run: the state it is drawn under (-1: no room) */
static inline __attribute__((always_inline)) int run_for(GLcontext c, mgl_ctx5 *x5)
{
    mgl_mtex_snap s;
    if (x5->mt_generic)
    {
        snap_get(c, x5, &s);
        if (x5->mt_cur < x5->mt_nrun && same_words(&x5->mt_run[x5->mt_cur].st, &s, (uint32_t)sizeof(s)))
            return (int)x5->mt_cur;
        for (uint32_t r = x5->mt_nrun; r-- > 0;)
            if (x5->mt_run[r].st.tex == s.tex && same_words(&x5->mt_run[r].st, &s, (uint32_t)sizeof(s)))
                return (int)r;
    }
    else
    {
        if (x5->mt_cur < x5->mt_nrun && snap_is(c, x5, &x5->mt_run[x5->mt_cur].st))
            return (int)x5->mt_cur;
        const uint32_t tex = c->bound[1];
        for (uint32_t r = x5->mt_nrun; r-- > 0;)
            if (x5->mt_run[r].st.tex == tex && snap_is(c, x5, &x5->mt_run[r].st))
                return (int)r;
        snap_get(c, x5, &s);                    /* a new run: its state, once */
    }
    if (!grow(c, (void **)&x5->mt_run, &x5->mt_caprun, x5->mt_nrun + 1u, (uint32_t)sizeof(mgl_mtex_run), MT_RUN_MAX))
        return -1;
    mgl_mtex_run *r = &x5->mt_run[x5->mt_nrun];
    r->st = s;
    r->first = r->count = 0;
    return (int)x5->mt_nrun++;
}

static void prim_begin(GLcontext c, mgl_ctx5 *x5, GLenum mode, uint32_t nvtx)
{
    x5->mt_rec = 0;
    if ((x5->mt_npool + (nvtx > MT_ROOM ? nvtx : MT_ROOM) > MT_POOL_MAX || x5->mt_ntri + MT_ROOM > MT_TRI_MAX) &&
        flushable(x5))
        mgl_mtex_flush(c, MGL_MT_ROOM);
    int r = run_for(c, x5);
    if (r < 0 && flushable(x5))
    {
        mgl_mtex_flush(c, MGL_MT_ROOM);
        r = run_for(c, x5);
    }
    if (r < 0)
    {
        x5->mt_dropped++;
        return;
    }
    x5->mt_cur = (uint32_t)r;
    x5->mt_prim = mode;
    x5->mt_k = 0;
    x5->mt_rec = 1;
    x5->mt_live = 1;
}

void mgl_mtex_prim(GLcontext c, int unit1)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!unit1 || !kept_kind(c->prim))
    {
        x5->mt_rec = 0;
        if (x5->mt_live)
            mgl_mtex_flush(c, MGL_MT_PRIM);     /* drawn without unit 1: the kept ones first */
        return;
    }
    prim_begin(c, x5, c->prim, 0);
}

static mgl_mtex_v *keep(GLcontext c, mgl_ctx5 *x5)
{
    if (!grow(c, (void **)&x5->mt_pool, &x5->mt_cappool, x5->mt_npool + 1u, (uint32_t)sizeof(mgl_mtex_v), MT_POOL_MAX) ||
        !grow(c, (void **)&x5->mt_tri, &x5->mt_captri, x5->mt_ntri + 2u, (uint32_t)sizeof(mgl_mtex_t), MT_TRI_MAX))
    {
        x5->mt_rec = 0;
        x5->mt_dropped++;
        return 0;
    }
    return &x5->mt_pool[x5->mt_npool];
}

void mgl_mtex_vertex(GLcontext c, float x, float y, float z, float w)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!x5->mt_rec || !c->in_begin)
        return;
    mgl_mtex_v *o = keep(c, x5);
    if (!o)
        return;
    o->x = x; o->y = y; o->z = z; o->w = w;
    mt_st_of(c->cur_tex[1], o);
    o->r = c->cur_color[0]; o->g = c->cur_color[1]; o->b = c->cur_color[2]; o->a = c->cur_color[3];
    mt_kept(x5, o);
}

void mgl_mtex_element(GLcontext c, GLint i)    /* glArrayElement, after the core drew it: its attributes are current */
{
    mgl_ctx5 *const x5 = X5(c);
    if (!x5->mt_rec || !c->in_begin)
        return;
    mgl_afetched f;
    mgl_array_fetch(c, i, &f);
    if (f.has_vtx)
        mgl_mtex_vertex(c, f.vtx[0], f.vtx[1], f.vtx[2], f.vtx[3]);
}

static GLint index_at(const GLvoid *idx, GLenum type, uint32_t k)
{
    if (type == GL_UNSIGNED_BYTE)
        return ((const uint8_t *)idx)[k];
    if (type == GL_UNSIGNED_SHORT)
    {
        uint16_t s;
        memcpy(&s, (const uint8_t *)idx + 2u * k, 2);
        return s;
    }
    uint32_t v;
    memcpy(&v, (const uint8_t *)idx + 4u * k, 4);
    return (GLint)v;
}

static int element(GLcontext c, int fast, GLint i, mgl_mtex_v *o)
{
    if (i < 0)
        return 0;
    if (fast)
    {
        const mgl_array *a = &c->va_vertex;
        const float *p = (const float *)(const void *)(a->ptr + (uint32_t)i * (a->stride ? (uint32_t)a->stride : 4u * (uint32_t)a->size));
        o->x = p[0]; o->y = p[1];
        o->z = a->size >= 3 ? p[2] : 0.0f;
        o->w = a->size >= 4 ? p[3] : 1.0f;
        const mgl_array *t = &c->va_tex[1];
        if (t->enabled && t->ptr)
        {
            const float *q = (const float *)(const void *)(t->ptr + (uint32_t)i * (t->stride ? (uint32_t)t->stride : 4u * (uint32_t)t->size));
            const float tc[4] = { q[0], q[1], 0.0f, t->size >= 4 ? q[3] : 1.0f };
            mt_st_of(tc, o);
        }
        else
            mt_st_of(c->cur_tex[1], o);
        const mgl_array *col = &c->va_color;
        if (col->enabled && col->ptr)
        {
            const float *q = (const float *)(const void *)(col->ptr + (uint32_t)i * (col->stride ? (uint32_t)col->stride : 4u * (uint32_t)col->size));
            const float k0 = q[0] < 0.0f ? 0.0f : q[0] > 1.0f ? 1.0f : q[0];
            const float k1 = q[1] < 0.0f ? 0.0f : q[1] > 1.0f ? 1.0f : q[1];
            const float k2 = q[2] < 0.0f ? 0.0f : q[2] > 1.0f ? 1.0f : q[2];
            const float k3 = col->size < 4 ? 1.0f : q[3] < 0.0f ? 0.0f : q[3] > 1.0f ? 1.0f : q[3];
            o->r = k0; o->g = k1; o->b = k2; o->a = k3;
        }
        else
        {
            o->r = c->cur_color[0]; o->g = c->cur_color[1]; o->b = c->cur_color[2]; o->a = c->cur_color[3];
        }
        return 1;
    }
    mgl_afetched f;
    mgl_array_fetch(c, i, &f);
    if (!f.has_vtx)
        return 0;
    o->x = f.vtx[0]; o->y = f.vtx[1]; o->z = f.vtx[2]; o->w = f.vtx[3];
    mt_st_of((f.tex_mask & 2u) ? f.tex[1] : c->cur_tex[1], o);
    const float *k = f.has_col ? f.col : c->cur_color;
    for (uint32_t j = 0; j < 4u; j++)
    {
        const float v = k[j] < 0.0f ? 0.0f : k[j] > 1.0f ? 1.0f : k[j];   /* (GLColor4f's clamp) */
        (&o->r)[j] = v;
    }
    return 1;
}

static int fast_layout(GLcontext c)
{
    const mgl_array *a = &c->va_vertex, *t = &c->va_tex[1], *col = &c->va_color;
    if (!a->enabled || !a->ptr || a->type != GL_FLOAT || a->size < 2)
        return 0;
    if (t->enabled && t->ptr && (t->type != GL_FLOAT || t->size < 2))
        return 0;
    if (col->enabled && col->ptr && (col->type != GL_FLOAT || col->size < 3))
        return 0;
    return 1;
}

void mgl_mtex_arrays(GLcontext c, GLenum mode, GLint first, GLsizei count, GLenum type, const GLvoid *indices)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!mgl_mtex_unit1(c) || !kept_kind(mode))
    {
        x5->mt_rec = 0;
        if (x5->mt_live)
            mgl_mtex_flush(c, MGL_MT_PRIM);
        return;
    }
    if (count < 3 || (!indices && first < 0) ||
        (indices && type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT))
        return;
    const uint32_t n = (uint32_t)count;
    GLint lo = indices ? 0x7fffffff : first, hi = indices ? -1 : first + count - 1;
    if (indices)
        for (uint32_t k = 0; k < n; k++)
        {
            const GLint i = index_at(indices, type, k);
            lo = i < lo ? i : lo;
            hi = i > hi ? i : hi;
        }
    if (lo < 0 || hi < lo)
        return;
    const uint32_t span = (uint32_t)(hi - lo) + 1u;
    const int dense = span <= n + 16u;
    prim_begin(c, x5, mode, dense ? span : n);
    if (!x5->mt_rec)
        return;
    const int fast = fast_layout(c);
    if (dense && x5->mt_npool + span <= MT_POOL_MAX &&
        grow(c, (void **)&x5->mt_pool, &x5->mt_cappool, x5->mt_npool + span, (uint32_t)sizeof(mgl_mtex_v), MT_POOL_MAX))
    {
        const uint32_t base = x5->mt_npool;
        for (uint32_t j = 0; j < span; j++)
            if (!element(c, fast, lo + (GLint)j, &x5->mt_pool[base + j]))
                memset(&x5->mt_pool[base + j], 0, sizeof(mgl_mtex_v));
        x5->mt_npool += span;
        const int flatpoly = mode == GL_POLYGON && x5->mt_run[x5->mt_cur].st.shade_model == GL_FLAT;
        for (uint32_t k = 0; k < n && x5->mt_rec; k++)
        {
            if (!grow(c, (void **)&x5->mt_tri, &x5->mt_captri, x5->mt_ntri + 2u, (uint32_t)sizeof(mgl_mtex_t), MT_TRI_MAX))
            {
                x5->mt_dropped++;
                break;
            }
            const uint32_t v = base + (uint32_t)((indices ? index_at(indices, type, k) : first + (GLint)k) - lo);
            if (flatpoly && k > 0u)
            {
                const mgl_mtex_v *f = &x5->mt_pool[x5->mt_c0];
                mgl_mtex_v *o = &x5->mt_pool[v];
                o->r = f->r; o->g = f->g; o->b = f->b; o->a = f->a;
            }
            mt_step(x5, v);
        }
    }
    else
        for (uint32_t k = 0; k < n && x5->mt_rec; k++)
        {
            mgl_mtex_v *o = keep(c, x5);
            if (!o)
                break;
            if (!element(c, fast, indices ? index_at(indices, type, k) : first + (GLint)k, o))
                memset(o, 0, sizeof(*o));
            mt_kept(x5, o);
        }
    x5->mt_rec = 0;                             /* the primitive ends with the draw */
}

static void draw_pass(GLcontext c, mgl_ctx5 *x5)
{
    const uint32_t nrun = x5->mt_nrun, ntri = x5->mt_ntri;
    if (!grow(c, (void **)&x5->mt_order, &x5->mt_caporder, 3u * ntri, 2u, 3u * MT_TRI_MAX))
    {
        x5->mt_dropped += ntri;
        return;
    }
    /* the triangles by run (a counting sort: the program's order within a run) */
    for (uint32_t r = 0; r < nrun; r++)
        x5->mt_run[r].count = 0;
    for (uint32_t t = 0; t < ntri; t++)
        x5->mt_run[x5->mt_tri[t].run].count++;
    uint32_t at = 0;
    for (uint32_t r = 0; r < nrun; r++)
    {
        x5->mt_run[r].first = at;
        at += x5->mt_run[r].count;
        x5->mt_run[r].count = 0;
    }
    for (uint32_t t = 0; t < ntri; t++)
    {
        mgl_mtex_run *r = &x5->mt_run[x5->mt_tri[t].run];
        uint16_t *o = &x5->mt_order[3u * (r->first + r->count++)];
        o[0] = x5->mt_tri[t].v[0];
        o[1] = x5->mt_tri[t].v[1];
        o[2] = x5->mt_tri[t].v[2];
    }

    /* the program's state, kept */
    mgl_mtex_snap keep_st;
    snap_get(c, x5, &keep_st);
    const uint32_t in_begin = c->in_begin, nimm = c->nimm, imm_total = c->imm_total, loop_saved = c->loop_saved;
    const GLenum prim = c->prim, error = c->error;
    const uint32_t en0 = c->en_tex2d[0], en1 = c->en_tex2d[1], bound0 = c->bound[0], blend = c->en_blend;
    const uint32_t tid = c->tex_identity;
    const GLenum bs = c->blend_src, bd = c->blend_dst, env0 = c->texenv[0];
    float envc0[4], col[4], tc0[4], tc1[4];
    memcpy(envc0, c->envcolor[0], sizeof(envc0));
    memcpy(col, c->cur_color, sizeof(col));
    memcpy(tc0, c->cur_tex[0], sizeof(tc0));
    memcpy(tc1, c->cur_tex[1], sizeof(tc1));
    const mgl_array va_v = c->va_vertex, va_c = c->va_color, va_t0 = c->va_tex[0], va_t1 = c->va_tex[1];

    const mgl_mtex_v *pool = x5->mt_pool;
    const GLsizei stride = (GLsizei)sizeof(mgl_mtex_v);
    c->va_vertex.size = 4; c->va_vertex.type = GL_FLOAT; c->va_vertex.stride = stride;
    c->va_vertex.ptr = (const uint8_t *)(const void *)&pool->x; c->va_vertex.enabled = 1;
    c->va_tex[0].size = 2; c->va_tex[0].type = GL_FLOAT; c->va_tex[0].stride = stride;
    c->va_tex[0].ptr = (const uint8_t *)(const void *)&pool->s; c->va_tex[0].enabled = 1;
    c->va_color.size = 4; c->va_color.type = GL_FLOAT; c->va_color.stride = stride;
    c->va_color.ptr = (const uint8_t *)(const void *)&pool->r; c->va_color.enabled = 1;
    c->va_tex[1].enabled = 0;
    c->en_tex2d[0] = 1;
    c->en_tex2d[1] = 0;
    c->tex_identity = 1;
    c->in_begin = 0;
    for (uint32_t r = 0; r < nrun; r++)
    {
        const mgl_mtex_run *run = &x5->mt_run[r];
        const mgl_mtex_snap *s = &run->st;
        if (!run->count)
            continue;
        GLenum env = GL_REPLACE, src = GL_ONE, dst = GL_ZERO;
        uint32_t en_blend = 1, fogc = 0;        /* fogc: 1 white, 2 black */
        if (s->env1_set)
            switch (s->env1)
            {
            case GL_REPLACE: en_blend = 0; break;
            case GL_DECAL:   src = GL_SRC_ALPHA; dst = GL_ONE_MINUS_SRC_ALPHA; break;
            case GL_BLEND:   src = GL_ZERO; dst = GL_ONE_MINUS_SRC_COLOR; fogc = 2; break;
            default:         src = GL_ZERO; dst = GL_SRC_COLOR; fogc = 1; break;       /* GL_MODULATE */
            }
        else if (x5->mt_args_known)
        {
            env = x5->mt_env;
            src = x5->mt_bs;
            dst = x5->mt_bd;
        }
        else
        {
            x5->mt_dropped += run->count;
            continue;
        }
        snap_put(c, s);
        if (fogc && c->en_fog)
            for (uint32_t k = 0; k < 3u; k++)
                c->fog_color[k] = fogc == 1u ? 1.0f : 0.0f;
        if (c->depth_func == GL_LESS)
            c->depth_func = GL_LEQUAL;
        else if (c->depth_func == GL_GREATER)
            c->depth_func = GL_GEQUAL;
        c->bound[0] = s->tex;
        memcpy(c->envcolor[0], s->envcolor1, sizeof(s->envcolor1));
        c->texenv[0] = env;
        c->en_blend = en_blend;
        c->blend_src = src;
        c->blend_dst = dst;
        c->rs_dirty = 1;
        if (x5->mt_generic)                     /* the reference (tests/host t_mtex_direct): the core's array draw */
            GLDrawElements(c, GL_TRIANGLES, (GLsizei)(3u * run->count), GL_UNSIGNED_SHORT, &x5->mt_order[3u * run->first]);
        else
            mgl_mtex_draw(c, x5->mt_pool, &x5->mt_order[3u * run->first], run->count);
    }

    /* the program's state back */
    snap_put(c, &keep_st);
    c->in_begin = in_begin; c->nimm = nimm; c->imm_total = imm_total; c->loop_saved = loop_saved;
    c->prim = prim; c->error = error;
    c->en_tex2d[0] = en0; c->en_tex2d[1] = en1; c->bound[0] = bound0; c->en_blend = blend;
    c->tex_identity = tid;
    c->blend_src = bs; c->blend_dst = bd; c->texenv[0] = env0;
    memcpy(c->envcolor[0], envc0, sizeof(envc0));
    memcpy(c->cur_color, col, sizeof(col));
    memcpy(c->cur_tex[0], tc0, sizeof(tc0));
    memcpy(c->cur_tex[1], tc1, sizeof(tc1));
    c->va_vertex = va_v; c->va_color = va_c; c->va_tex[0] = va_t0; c->va_tex[1] = va_t1;
    c->mvp_dirty = 1;
    c->geom_dirty = 1;
    c->rs_dirty = 1;
    x5->mt_passes++;
}

void mgl_mtex_flush(GLcontext c, uint32_t why)
{
    mgl_ctx5 *const x5 = X5(c);
    if (why == MGL_MT_SWAP)
        x5->mt_called_frame = 0;                /* (a new frame: its own call) */
    if (!x5->mt_live)
        return;
    if (why != MGL_MT_CALL && !flushable(x5))
    {
        if (why == MGL_MT_SWAP)
            reset(x5);                          /* MiniGL 1.2's protocol, no call yet: what no call drew is dropped */
        return;
    }
    if (why == MGL_MT_PRIM && !x5->mt_env1_set && !x5->mt_called_frame)
        return;
    if (c->in_begin && (c->nimm || c->imm_total))
        return;                                 /* (never inside a primitive that has vertices) */
    x5->mt_rec = 0;
    if (x5->mt_ntri)
        draw_pass(c, x5);
    reset(x5);
}

void mgl_mtex_call(GLcontext c, GLenum bsrc, GLenum bdst, GLenum env)
{
    mgl_ctx5 *const x5 = X5(c);
    x5->mt_args_known = 1;
    x5->mt_called_frame = 1;
    x5->mt_bs = bsrc;
    x5->mt_bd = bdst;
    x5->mt_env = env == GL_MODULATE || env == GL_DECAL || env == GL_BLEND ? env : GL_REPLACE;   /* (MiniGL 1.2's default) */
    mgl_mtex_flush(c, MGL_MT_CALL);
}

void mgl_mtex_free(GLcontext c, mgl_ctx5 *x5)
{
    if (c->sink.free)
    {
        if (x5->mt_pool)
            c->sink.free(c->sink.ud, x5->mt_pool, x5->mt_cappool * (uint32_t)sizeof(mgl_mtex_v));
        if (x5->mt_tri)
            c->sink.free(c->sink.ud, x5->mt_tri, x5->mt_captri * (uint32_t)sizeof(mgl_mtex_t));
        if (x5->mt_run)
            c->sink.free(c->sink.ud, x5->mt_run, x5->mt_caprun * (uint32_t)sizeof(mgl_mtex_run));
        if (x5->mt_order)
            c->sink.free(c->sink.ud, x5->mt_order, x5->mt_caporder * 2u);
    }
    x5->mt_pool = 0;
    x5->mt_tri = 0;
    x5->mt_run = 0;
    x5->mt_order = 0;
    x5->mt_cappool = x5->mt_captri = x5->mt_caprun = x5->mt_caporder = 0;
    reset(x5);
}

void GLClear_a5(GLcontext c, GLbitfield mask)
{
    mgl_mtex_flush(c, MGL_MT_CLEAR);            /* what was drawn before the clear */
    GLClear(c, mask);
}

void GLTexEnvi_a5(GLcontext c, GLenum target, GLenum pname, GLint param)
{
    const uint32_t d = c->rs_dirty;
    if (c->active_unit == 1u && pname == GL_TEXTURE_ENV_MODE)
        X5(c)->mt_env1_set = 1;                 /* the program speaks GL 1.3's multitexture: its environment rules */
    GLTexEnvi(c, target, pname, param);
    if (c->active_unit == 1u)
        c->rs_dirty = d;
}

void GLTexEnvfv_a5(GLcontext c, GLenum target, GLenum pname, const GLfloat *p)
{
    const uint32_t d = c->rs_dirty;
    if (c->active_unit == 1u && pname == GL_TEXTURE_ENV_MODE)
        X5(c)->mt_env1_set = 1;
    GLTexEnvfv(c, target, pname, p);
    if (c->active_unit == 1u)
        c->rs_dirty = d;
}

void GLBindTexture_a5(GLcontext c, GLenum target, GLuint texture)
{
    if (__builtin_expect(target != GL_TEXTURE_2D || texture >= MGL_MAX_TEXNAMES, 0))
    {
        const uint32_t d = c->rs_dirty;
        GLBindTexture(c, target, texture);
        if (c->active_unit == 1u)
            c->rs_dirty = d;
        return;
    }
    mgl_texobj *const t = &c->tex[texture];
    if (texture && !t->used)
    {
        t->used = 1;                            /* binding an unused name creates the object (GL 1.1) */
        t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
        t->mag_filter = GL_LINEAR;
        t->wrap_s = t->wrap_t = GL_REPEAT;
    }
    if (c->bound[c->active_unit] != texture)
    {
        c->bound[c->active_unit] = texture;
        if (c->active_unit != 1u)
            c->rs_dirty = 1;
    }
}

void GLPushMatrix_a5(GLcontext c)                    { mgl_mtex_flush(c, MGL_MT_MATRIX); GLPushMatrix(c); }
void GLPopMatrix_a5(GLcontext c)                     { mgl_mtex_flush(c, MGL_MT_MATRIX); GLPopMatrix(c); }
void GLLoadIdentity_a5(GLcontext c)                  { mgl_mtex_flush(c, MGL_MT_MATRIX); GLLoadIdentity(c); }
void GLLoadMatrixf_a5(GLcontext c, const GLfloat *m) { mgl_mtex_flush(c, MGL_MT_MATRIX); GLLoadMatrixf(c, m); }
void GLLoadMatrixd_a5(GLcontext c, const GLdouble *m) { mgl_mtex_flush(c, MGL_MT_MATRIX); GLLoadMatrixd(c, m); }
