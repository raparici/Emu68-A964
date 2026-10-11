/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: matrices, immediate mode, vertex arrays, transform, clipping, culling. */

#include <string.h>
#include "mgl_core.h"

void mgl_seterr(GLcontext c, GLenum e);         /* mgl_state.c */
void mgl_raster_sync(GLcontext c);              /* mgl_state.c: the sink gets the state before the next triangle */

static void m4_ident(mgl_m4 *a)
{
    memset(a, 0, sizeof(*a));
    a->m[0] = a->m[5] = a->m[10] = a->m[15] = 1.0f;
}

static void m4_mul(mgl_m4 *r, const mgl_m4 *a, const mgl_m4 *b)
{
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
        {
            float s = a->m[i] * b->m[j * 4];
            s = s + a->m[4 + i] * b->m[j * 4 + 1];
            s = s + a->m[8 + i] * b->m[j * 4 + 2];
            s = s + a->m[12 + i] * b->m[j * 4 + 3];
            r->m[j * 4 + i] = s;
        }
}

static mgl_m4 *cur_matrix(GLcontext c)
{
    switch (c->matrix_mode)
    {
    case GL_PROJECTION: return &c->proj[c->proj_sp];
    case GL_TEXTURE:    return &c->texm[c->tex_sp];
    default:            return &c->mv[c->mv_sp];
    }
}

static void matrix_changed(GLcontext c)
{
    c->geom_dirty = 1;
    if (c->matrix_mode == GL_TEXTURE)
    {
        mgl_m4 id;
        m4_ident(&id);
        c->tex_identity = 1;
        for (int i = 0; i < 16; i++)
            c->tex_identity &= id.m[i] == c->texm[c->tex_sp].m[i];
    }
    else
        c->mvp_dirty = 1;
}

static void mult_cur(GLcontext c, const mgl_m4 *b)
{
    mgl_m4 *a = cur_matrix(c), r;
    m4_mul(&r, a, b);
    *a = r;
    matrix_changed(c);
}

static int no_begin(GLcontext c)
{
    if (c->in_begin)
        mgl_seterr(c, GL_INVALID_OPERATION);
    return 1;
}

void GLMatrixMode(GLcontext c, GLenum mode)
{
    if (!no_begin(c))
        return;
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->matrix_mode = mode;
}

void GLLoadIdentity(GLcontext c)
{
    if (!no_begin(c))
        return;
    m4_ident(cur_matrix(c));
    matrix_changed(c);
}

void GLLoadMatrixf(GLcontext c, const GLfloat *m)
{
    if (!no_begin(c))
        return;
    memcpy(cur_matrix(c)->m, m, sizeof(float) * 16u);
    matrix_changed(c);
}

void GLLoadMatrixd(GLcontext c, const GLdouble *m)
{
    if (!no_begin(c))
        return;
    mgl_m4 *a = cur_matrix(c);
    for (int i = 0; i < 16; i++)
        a->m[i] = (float)m[i];
    matrix_changed(c);
}

void GLMultMatrixf(GLcontext c, const GLfloat *m)
{
    if (!no_begin(c))
        return;
    mgl_m4 b;
    memcpy(b.m, m, sizeof(b.m));
    mult_cur(c, &b);
}

void GLMultMatrixd(GLcontext c, const GLdouble *m)
{
    if (!no_begin(c))
        return;
    mgl_m4 b;
    for (int i = 0; i < 16; i++)
        b.m[i] = (float)m[i];
    mult_cur(c, &b);
}

void GLPushMatrix(GLcontext c)
{
    if (!no_begin(c))
        return;
    uint32_t *sp = c->matrix_mode == GL_PROJECTION ? &c->proj_sp : c->matrix_mode == GL_TEXTURE ? &c->tex_sp : &c->mv_sp;
    const uint32_t depth = c->matrix_mode == GL_PROJECTION ? MGL_PROJ_DEPTH : c->matrix_mode == GL_TEXTURE ? MGL_TEX_DEPTH
                                                                                                          : MGL_MV_DEPTH;
    mgl_m4 *st = c->matrix_mode == GL_PROJECTION ? c->proj : c->matrix_mode == GL_TEXTURE ? c->texm : c->mv;
    if (*sp + 1u >= depth)
    {
        mgl_seterr(c, GL_STACK_OVERFLOW);
        return;
    }
    st[*sp + 1u] = st[*sp];
    (*sp)++;
}

void GLPopMatrix(GLcontext c)
{
    if (!no_begin(c))
        return;
    uint32_t *sp = c->matrix_mode == GL_PROJECTION ? &c->proj_sp : c->matrix_mode == GL_TEXTURE ? &c->tex_sp : &c->mv_sp;
    if (*sp == 0)
    {
        mgl_seterr(c, GL_STACK_UNDERFLOW);
        return;
    }
    (*sp)--;
    matrix_changed(c);
}

static void rotate_sc(GLcontext c, float s, float co, float x, float y, float z)
{
    const float l2 = x * x + y * y + z * z;
    if (l2 == 0.0f)
        return;
    if (l2 != 1.0f)
    {
        const float l = mgl_sqrtf(l2);
        x = x / l;
        y = y / l;
        z = z / l;
    }
    const float t = 1.0f - co;
    mgl_m4 r;
    m4_ident(&r);
    r.m[0] = x * x * t + co;     r.m[4] = x * y * t - z * s;  r.m[8] = x * z * t + y * s;
    r.m[1] = y * x * t + z * s;  r.m[5] = y * y * t + co;     r.m[9] = y * z * t - x * s;
    r.m[2] = x * z * t - y * s;  r.m[6] = y * z * t + x * s;  r.m[10] = z * z * t + co;
    mult_cur(c, &r);
}

void GLRotatef(GLcontext c, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    if (!no_begin(c))
        return;
    const float a = angle * (3.14159265358979323846f / 180.0f);
    rotate_sc(c, mgl_sinf(a), mgl_cosf(a), x, y, z);
}

void GLRotated(GLcontext c, GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    GLRotatef(c, (float)angle, (float)x, (float)y, (float)z);
}

static void rot_axes(GLint xyz, float *x, float *y, float *z)
{
    *x = *y = *z = 0.0f;
    if (xyz == 0x800)
        *z = 1.0f;
    else if (xyz == 0x1000)
        *y = 1.0f;
    else
        *x = 1.0f;
}

void GLRotatefEXT(GLcontext c, GLfloat angle, const GLint xyz)
{
    float x, y, z;
    rot_axes(xyz, &x, &y, &z);
    GLRotatef(c, angle, x, y, z);
}

void GLRotatefEXTs(GLcontext c, GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    if (!no_begin(c))
        return;
    float x, y, z;
    rot_axes(xyz, &x, &y, &z);
    rotate_sc(c, sin_an, cos_an, x, y, z);
}

void GLScalef(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    if (!no_begin(c))
        return;
    mgl_m4 r;
    m4_ident(&r);
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    mult_cur(c, &r);
}

void GLScaled(GLcontext c, GLdouble x, GLdouble y, GLdouble z)
{
    GLScalef(c, (float)x, (float)y, (float)z);
}

void GLTranslatef(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    if (!no_begin(c))
        return;
    mgl_m4 r;
    m4_ident(&r);
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    mult_cur(c, &r);
}

void GLTranslated(GLcontext c, GLdouble x, GLdouble y, GLdouble z)
{
    GLTranslatef(c, (float)x, (float)y, (float)z);
}

void GLFrustum(GLcontext c, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    if (!no_begin(c))
        return;
    if (n <= 0.0 || f <= 0.0 || l == r || b == t || n == f)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    const float L = (float)l, R = (float)r, B = (float)b, T = (float)t, N = (float)n, F = (float)f;
    mgl_m4 m;
    memset(&m, 0, sizeof(m));
    m.m[0] = 2.0f * N / (R - L);
    m.m[5] = 2.0f * N / (T - B);
    m.m[8] = (R + L) / (R - L);
    m.m[9] = (T + B) / (T - B);
    m.m[10] = -(F + N) / (F - N);
    m.m[11] = -1.0f;
    m.m[14] = -2.0f * F * N / (F - N);
    mult_cur(c, &m);
}

void GLOrtho(GLcontext c, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    if (!no_begin(c))
        return;
    if (l == r || b == t || n == f)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    const float L = (float)l, R = (float)r, B = (float)b, T = (float)t, N = (float)n, F = (float)f;
    mgl_m4 m;
    m4_ident(&m);
    m.m[0] = 2.0f / (R - L);
    m.m[5] = 2.0f / (T - B);
    m.m[10] = -2.0f / (F - N);
    m.m[12] = -(R + L) / (R - L);
    m.m[13] = -(T + B) / (T - B);
    m.m[14] = -(F + N) / (F - N);
    mult_cur(c, &m);
}

/* GLU, as the standard's table carries it (no context argument: the current one) */
extern GLcontext mgl_current;                  /* the platform's (src/mgl_lib.c) or the test's */

void GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    GLcontext c = mgl_current;
    if (!c)
        return;
    const float a = fovy * (3.14159265358979323846f / 360.0f);
    const float top = znear * mgl_tanf(a);
    const float right = top * aspect;
    GLFrustum(c, -right, right, -top, top, znear, zfar);
}

void GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    GLcontext c = mgl_current;
    if (!c || !no_begin(c))
        return;
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float l = mgl_sqrtf(fx * fx + fy * fy + fz * fz);
    if (l == 0.0f)
        return;
    fx = fx / l; fy = fy / l; fz = fz / l;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    l = mgl_sqrtf(sx * sx + sy * sy + sz * sz);
    if (l == 0.0f)
        return;
    sx = sx / l; sy = sy / l; sz = sz / l;
    const float vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    mgl_m4 m;
    m4_ident(&m);
    m.m[0] = sx;  m.m[4] = sy;  m.m[8] = sz;
    m.m[1] = vx;  m.m[5] = vy;  m.m[9] = vz;
    m.m[2] = -fx; m.m[6] = -fy; m.m[10] = -fz;
    mult_cur(c, &m);
    GLTranslatef(c, -ex, -ey, -ez);
}

void GLViewport(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (!no_begin(c))
        return;
    if (w < 0 || h < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    mgl_core_flush(c);
    c->vp_x = x;
    c->vp_y = y;
    c->vp_w = (int32_t)w;
    c->vp_h = (int32_t)h;
    c->geom_dirty = 1;
}

void GLDepthRange(GLcontext c, GLclampd n, GLclampd f)
{
    if (!no_begin(c))
        return;
    mgl_core_flush(c);
    c->depth_n = (float)(n < 0.0 ? 0.0 : n > 1.0 ? 1.0 : n);
    c->depth_f = (float)(f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f);
    c->geom_dirty = 1;
}

static void update_mvp(GLcontext c)
{
    if (c->mvp_dirty)
    {
        m4_mul(&c->mvp, &c->proj[c->proj_sp], &c->mv[c->mv_sp]);
        c->mvp_dirty = 0;
    }
}

/* one vertex to clip space, with the current attributes */
static inline __attribute__((always_inline)) void emit_vertex(GLcontext c, float x, float y, float z, float w);

static void geom_sync(GLcontext c)
{
    update_mvp(c);
    for (int i = 0; i < 16; i++)
        c->geom.mvp[i] = c->mvp.m[i];
    c->geom.vp_x = c->vp_x;
    c->geom.vp_y = c->vp_y;
    c->geom.vp_w = c->vp_w;
    c->geom.vp_h = c->vp_h;
    c->geom.height = c->height;
    c->geom.depth_n = c->depth_n;
    c->geom.depth_f = c->depth_f;
    c->geom.zoffset = c->zoffset;
    uint32_t f = 0;
    if (c->en_cull)
        f |= MGL_GC_CULL;
    if (c->cull_mode == GL_FRONT)
        f |= MGL_GC_CULL_FRONT;
    if (c->cull_mode == GL_FRONT_AND_BACK)
        f |= MGL_GC_CULL_BOTH;
    if (c->front_face == GL_CW)
        f |= MGL_GC_FRONT_CW;
    c->geom.flags = f;
    c->geom_dirty = 0;
    c->geom_flat = 0;
}

static void defer_flush(GLcontext c)
{
    if (c->ndt && c->sink.tris_object)
    {
        c->sink.tris_object(c->sink.ud, c->dv, c->ndt, &c->geom);
        c->st_defer_batches++;
    }
    c->ndt = 0;
}

/* the immediate batch alone to the sink */
static void imm_flush(GLcontext c)
{
    if (c->nbt && c->sink.tris)
    {
        c->sink.tris(c->sink.ud, c->bv, c->nbv, c->bi, c->nbt);
        c->st_batches++;
    }
    c->nbv = 0;
    c->nbt = 0;
}

void mgl_core_flush(GLcontext c)
{
    if (c->deferred)
        defer_flush(c);
    imm_flush(c);
}

void mgl_core_set_deferred(GLcontext c, uint32_t on)
{
    mgl_core_flush(c);
    if (on && c->sink.tris_object && !c->dv && c->sink.alloc)
        c->dv = (mgl_cvtx *)c->sink.alloc(c->sink.ud, (uint32_t)sizeof(mgl_cvtx) * 3u * MGL_DEFER_TRI);
    c->deferred = on && c->sink.tris_object && c->dv ? 1u : 0u;   /* no memory: the immediate path, the same pixels */
    c->geom_dirty = 1;
}

/* a clip-space vertex to the sink's screen space (the stage's, under the current geometry) */
static void to_screen(GLcontext c, const mgl_cvtx *v, mgl_vtx *o)
{
    if (c->geom_dirty)
        geom_sync(c);
    mgl_stage_to_screen(&c->geom, v, o);
}

/* room for nv vertices and nt triangles in the batch (a flush when full); the raster state first */
static void batch_room(GLcontext c, uint32_t nv, uint32_t nt)
{
    if (c->rs_dirty || !c->rs_valid)
    {
        mgl_core_flush(c);
        mgl_raster_sync(c);
    }
    if (c->nbv + nv > MGL_BATCH_VTX || c->nbt + nt > MGL_BATCH_TRI)
        mgl_core_flush(c);
}
static void stage_room(void *ud, uint32_t nv, uint32_t nt) { batch_room((GLcontext)ud, nv, nt); }

static void tri_out(GLcontext c, const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *cc)
{
    if (c->geom_dirty)
        geom_sync(c);
    mgl_stage_out *o = &c->stage_out;
    o->v = c->bv;
    o->idx = c->bi;
    o->nv = &c->nbv;
    o->nt = &c->nbt;
    o->room = stage_room;
    o->ud = c;
    o->st_in = &c->st_tris_in;
    o->st_culled = &c->st_tris_culled;
    o->st_clipped = &c->st_tris_clipped;
    mgl_stage_tri(&c->geom, a, b, cc, o);
}

static inline __attribute__((always_inline)) void tri_defer(GLcontext c, const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *cc)
{
    if (c->rs_dirty || !c->rs_valid)
    {
        mgl_core_flush(c);
        mgl_raster_sync(c);
    }
    if (c->geom_dirty)
    {
        defer_flush(c);
        geom_sync(c);
    }
    if (c->nbt)
        imm_flush(c);
    if (c->ndt >= MGL_DEFER_TRI)
        defer_flush(c);
    mgl_cvtx *d = &c->dv[3u * c->ndt];
    d[0] = *a;
    d[1] = *b;
    d[2] = *cc;
    c->ndt++;
    c->st_tris_in++;
}

static inline __attribute__((always_inline)) void tri(GLcontext c, const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *cc,
                                                      const mgl_cvtx *prov)
{
    if (c->shade_model == GL_FLAT)
    {
        mgl_cvtx fa = *a, fb = *b, fc = *cc;
        fa.r = fb.r = fc.r = prov->r;
        fa.g = fb.g = fc.g = prov->g;
        fa.b = fb.b = fc.b = prov->b;
        fa.a = fb.a = fc.a = prov->a;
        if (c->deferred)
            tri_defer(c, &fa, &fb, &fc);
        else
            tri_out(c, &fa, &fb, &fc);
    }
    else if (c->deferred)
        tri_defer(c, a, b, cc);
    else
        tri_out(c, a, b, cc);
}

static void flat_defer(GLcontext c, const mgl_cvtx v[3])
{
    if (c->rs_dirty || !c->rs_valid)
    {
        mgl_core_flush(c);
        mgl_raster_sync(c);
    }
    if (!c->geom_flat || c->geom.vp_w != c->width || c->geom.vp_h != c->height)
    {
        defer_flush(c);                         /* the triangles held, under their own geometry */
        mgl_geom *g = &c->geom;
        memset(g, 0, sizeof(*g));
        g->mvp[0] = g->mvp[5] = g->mvp[10] = g->mvp[15] = 1.0f;
        g->vp_w = c->width;
        g->vp_h = c->height;
        g->height = c->height;
        g->depth_n = 0.0f;
        g->depth_f = 1.0f;
        c->geom_flat = 1;
        c->geom_dirty = 1;                      /* the next 3D consumer re-makes its own (geom_sync) */
    }
    if (c->nbt)
        imm_flush(c);
    if (c->ndt >= MGL_DEFER_TRI)
        defer_flush(c);
    mgl_cvtx *d = &c->dv[3u * c->ndt];
    d[0] = v[0];
    d[1] = v[1];
    d[2] = v[2];
    c->ndt++;
    c->st_tris_in++;
    c->st_tris_flat_def++;
}

static void flat_tri(GLcontext c, const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *cc, const mgl_cvtx *prov)
{
    c->st_tris_flat++;
    mgl_geom g;
    memset(&g, 0, sizeof(g));
    g.vp_w = c->width;
    g.vp_h = c->height;
    g.height = c->height;
    g.depth_n = 0.0f;
    g.depth_f = 1.0f;
    const float sx = c->width > 0 ? 2.0f / (float)c->width : 0.0f, sy = c->height > 0 ? 2.0f / (float)c->height : 0.0f;
    mgl_cvtx v[3];
    const mgl_cvtx *in[3] = { a, b, cc };
    for (int k = 0; k < 3; k++)
    {
        v[k] = *in[k];
        v[k].x = in[k]->x * sx - 1.0f;
        v[k].y = 1.0f - in[k]->y * sy;
        v[k].z = in[k]->z * 2.0f - 1.0f;
        v[k].w = 1.0f;
        if (c->shade_model == GL_FLAT)
        {
            v[k].r = prov->r; v[k].g = prov->g; v[k].b = prov->b; v[k].a = prov->a;
        }
    }
    if (c->deferred)
    {
        flat_defer(c, v);
        return;
    }
    mgl_stage_out *o = &c->stage_out;
    o->v = c->bv;
    o->idx = c->bi;
    o->nv = &c->nbv;
    o->nt = &c->nbt;
    o->room = stage_room;
    o->ud = c;
    o->st_in = &c->st_tris_in;
    o->st_culled = &c->st_tris_culled;
    o->st_clipped = &c->st_tris_clipped;
    mgl_stage_tri(&g, &v[0], &v[1], &v[2], o);
}
static int prim_flat(GLenum m) { return m == MGL_FLATFAN || m == MGL_FLATSTRIP; }

static mgl_cvtx clip_of(GLcontext c, const mgl_cvtx *v)
{
    if (!c->deferred)
        return *v;
    mgl_cvtx o = *v;
    update_mvp(c);
    mgl_stage_xform(c->mvp.m, v->x, v->y, v->z, v->w, &o);
    return o;
}

static void line(GLcontext c, const mgl_cvtx *ao, const mgl_cvtx *bo)
{
    const mgl_cvtx ca = clip_of(c, ao), cb = clip_of(c, bo);
    const mgl_cvtx *a = &ca, *b = &cb;
    if (!c->sink.lines || a->w <= 0.0f || b->w <= 0.0f || (mgl_stage_outcode(a) & mgl_stage_outcode(b)))
        return;
    mgl_core_flush(c);
    if (c->rs_dirty || !c->rs_valid)
        mgl_raster_sync(c);
    mgl_vtx v[2];
    to_screen(c, a, &v[0]);
    to_screen(c, b, &v[1]);
    if (c->shade_model == GL_FLAT)
    {
        v[0].r = v[1].r; v[0].g = v[1].g; v[0].b = v[1].b; v[0].a = v[1].a;
    }
    c->sink.lines(c->sink.ud, v, 1);
}

#define MGL_MAX_PRIMSIZE 256.0f                 /* the engine's A3D_MAX_PRIMSIZE */
static void point(GLcontext c, const mgl_cvtx *ao)
{
    const mgl_cvtx ca = clip_of(c, ao);
    const mgl_cvtx *a = &ca;
    if (a->w <= 0.0f || mgl_stage_outcode(a))
        return;
    if (c->deferred)
        defer_flush(c);                         /* the triangles held before this point go first */
    batch_room(c, 4, 2);
    mgl_vtx v;
    to_screen(c, a, &v);
    const float size = !(c->point_size >= 1.0f) ? 1.0f : c->point_size > MGL_MAX_PRIMSIZE ? MGL_MAX_PRIMSIZE
                                                                                          : c->point_size;
    const float h = 0.5f * size;
    const uint32_t base = c->nbv;
    mgl_vtx *q = &c->bv[base];
    q[0] = v; q[1] = v; q[2] = v; q[3] = v;
    q[0].x -= h; q[0].y -= h;
    q[1].x += h; q[1].y -= h;
    q[2].x += h; q[2].y += h;
    q[3].x -= h; q[3].y += h;
    uint16_t *i = &c->bi[3u * c->nbt];
    i[0] = (uint16_t)base; i[1] = (uint16_t)(base + 1u); i[2] = (uint16_t)(base + 2u);
    i[3] = (uint16_t)base; i[4] = (uint16_t)(base + 2u); i[5] = (uint16_t)(base + 3u);
    c->nbv += 4u;
    c->nbt += 2u;
}

static int prim_ok(GLenum m)
{
    return m == GL_POINTS || m == GL_LINES || m == GL_LINE_STRIP || m == GL_LINE_LOOP || m == GL_TRIANGLES ||
           m == GL_TRIANGLE_STRIP || m == GL_TRIANGLE_FAN || m == GL_QUADS || m == GL_QUAD_STRIP || m == GL_POLYGON ||
           prim_flat(m);
}

void GLBegin(GLcontext c, GLenum mode)
{
    if (c->in_begin)
    {
        mgl_seterr(c, GL_INVALID_OPERATION);
        c->in_begin = 0;
        c->nimm = 0;
        c->loop_saved = 0;
    }
    if (!prim_ok(mode))
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    update_mvp(c);
    c->in_begin = 1;
    c->prim = mode;
    c->nimm = 0;
    c->imm_total = 0;
    c->loop_saved = 0;
}

static void assemble(GLcontext c, int final)
{
    mgl_cvtx *v = c->imm;
    const uint32_t n = c->nimm;
    uint32_t keep_from = n;                     /* vertices [keep_from, n) stay for the next part */
    switch (c->prim)
    {
    case GL_TRIANGLES:
        for (uint32_t i = 0; i + 3u <= n; i += 3u)
            tri(c, &v[i], &v[i + 1u], &v[i + 2u], &v[i + 2u]);
        keep_from = n - n % 3u;
        break;
    case GL_QUADS:
        for (uint32_t i = 0; i + 4u <= n; i += 4u)
            for (uint32_t h = 0; h < 2u; h++)
                tri(c, &v[i + h], &v[i + 1u + h], &v[i + 3u], &v[i + 3u]);
        keep_from = n - n % 4u;
        break;
    case GL_TRIANGLE_STRIP:
        for (uint32_t i = 0; i + 3u <= n; i++)
        {
            const uint32_t odd = (c->imm_total - n + i) & 1u;
            tri(c, &v[i + odd], &v[i + 1u - odd], &v[i + 2u], &v[i + 2u]);
        }
        keep_from = n >= 2u ? n - 2u : 0;
        break;
    case GL_QUAD_STRIP:
        for (uint32_t i = 0; i + 4u <= n; i += 2u)
            for (uint32_t h = 0; h < 2u; h++)
                tri(c, &v[i], &v[i + 1u + 2u * h], &v[i + 3u - h], &v[i + 3u]);
        keep_from = n >= 2u ? n - 2u - (n & 1u) : 0;  /* the next quad starts at the last even index pair */
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (uint32_t i = 1; i + 1u < n; i++)
            tri(c, &v[0], &v[i], &v[i + 1u], c->prim == GL_POLYGON ? &v[0] : &v[i + 1u]);
        keep_from = n >= 1u ? n - 1u : 0;       /* the fan centre is kept separately below */
        break;
    case MGL_FLATFAN:
        for (uint32_t i = 1; i + 1u < n; i++)
            flat_tri(c, &v[0], &v[i], &v[i + 1u], &v[i + 1u]);
        keep_from = n >= 1u ? n - 1u : 0;
        break;
    case MGL_FLATSTRIP:
        for (uint32_t i = 0; i + 3u <= n; i++)
        {
            const uint32_t k = c->imm_total - n + i;
            if (k & 1u)
                flat_tri(c, &v[i + 1u], &v[i], &v[i + 2u], &v[i + 2u]);
            else
                flat_tri(c, &v[i], &v[i + 1u], &v[i + 2u], &v[i + 2u]);
        }
        keep_from = n >= 2u ? n - 2u : 0;
        break;
    case GL_LINES:
        for (uint32_t i = 0; i + 2u <= n; i += 2u)
            line(c, &v[i], &v[i + 1u]);
        keep_from = n - n % 2u;
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        for (uint32_t i = 0; i + 2u <= n; i++)
            line(c, &v[i], &v[i + 1u]);
        keep_from = n >= 1u ? n - 1u : 0;
        break;
    case GL_POINTS:
        for (uint32_t i = 0; i < n; i++)
            point(c, &v[i]);
        keep_from = n;
        break;
    default:
        break;
    }
    if (final)
    {
        c->nimm = 0;
        return;
    }
    /* a partial flush: keep what the rest of the primitive needs at the front of the buffer */
    if (c->prim == GL_TRIANGLE_FAN || c->prim == GL_POLYGON || c->prim == MGL_FLATFAN)
    {
        v[1] = v[n - 1u];                       /* the centre stays at 0, the last edge vertex follows it */
        c->nimm = 2;
    }
    else if (c->prim == GL_LINE_LOOP)
    {
        if (c->imm_total == n)                  /* the first part: the loop's first vertex closes it at glEnd */
        {
            c->loop_first = v[0];
            c->loop_saved = 1;
        }
        v[0] = v[n - 1u];
        c->nimm = 1;
    }
    else
    {
        const uint32_t k = n - keep_from;
        memmove(v, v + keep_from, sizeof(*v) * k);
        c->nimm = k;
    }
}

void GLEnd(GLcontext c)
{
    if (!c->in_begin)
    {
        mgl_seterr(c, GL_INVALID_OPERATION);
        return;
    }
    if (c->prim == GL_LINE_LOOP && c->imm_total >= 2u)
    {
        /* close the loop: the last vertex back to the first (kept aside if a partial flush moved it out) */
        if (c->nimm < MGL_IMM_MAX)
        {
            c->imm[c->nimm] = c->loop_saved ? c->loop_first : c->imm[0];
            c->nimm++;
        }
        c->prim = GL_LINE_STRIP;
    }
    c->loop_saved = 0;
    assemble(c, 1);
    c->in_begin = 0;
}

static inline __attribute__((always_inline)) void emit_vertex_core(GLcontext c, float x, float y, float z, float w);
static inline __attribute__((always_inline)) void emit_vertex(GLcontext c, float x, float y, float z, float w)
{
    emit_vertex_core(c, x, y, z, w);
}
static inline __attribute__((always_inline)) void emit_vertex_core(GLcontext c, float x, float y, float z, float w)
{
    if (!c->in_begin)
        return;                                 /* a vertex outside Begin/End is undefined: ignored */
    if (c->nimm >= MGL_IMM_MAX)
        assemble(c, 0);
    mgl_cvtx *o = &c->imm[c->nimm];
    if (c->deferred || prim_flat(c->prim))
    {
        o->x = x;
        o->y = y;
        o->z = z;
        o->w = w;
    }
    else
    {
        if (c->mvp_dirty)
            update_mvp(c);
        mgl_stage_xform(c->mvp.m, x, y, z, w, o);
    }
    const float *tc = c->cur_tex[0];
    if (c->tex_identity)
    {
        o->s = tc[3] == 1.0f ? tc[0] : tc[0] / tc[3];
        o->t = tc[3] == 1.0f ? tc[1] : tc[1] / tc[3];
    }
    else
    {
        const float *t = c->texm[c->tex_sp].m;
        const float s = t[0] * tc[0] + t[4] * tc[1] + t[8] * tc[2] + t[12] * tc[3];
        const float tt = t[1] * tc[0] + t[5] * tc[1] + t[9] * tc[2] + t[13] * tc[3];
        const float q = t[3] * tc[0] + t[7] * tc[1] + t[11] * tc[2] + t[15] * tc[3];
        o->s = q == 1.0f || q == 0.0f ? s : s / q;
        o->t = q == 1.0f || q == 0.0f ? tt : tt / q;
    }
    o->r = c->cur_color[0];
    o->g = c->cur_color[1];
    o->b = c->cur_color[2];
    o->a = c->cur_color[3];
    c->nimm++;
    c->imm_total++;
    c->st_verts++;
}

void GLVertex4f(GLcontext c, GLfloat x, GLfloat y, GLfloat z, GLfloat w)  { emit_vertex(c, x, y, z, w); }
void GLVertex4fv(GLcontext c, GLfloat *v)                                { emit_vertex(c, v[0], v[1], v[2], v[3]); }
void GLVertex3fv(GLcontext c, GLfloat *v)                                { emit_vertex(c, v[0], v[1], v[2], 1.0f); }
void GLVertex2fv(GLcontext c, GLfloat *v)                                { emit_vertex(c, v[0], v[1], 0.0f, 1.0f); }

static float clampf(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

void GLColor4f(GLcontext c, GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    c->cur_color[0] = clampf(r);
    c->cur_color[1] = clampf(g);
    c->cur_color[2] = clampf(b);
    c->cur_color[3] = clampf(a);
}
void GLColor4fv(GLcontext c, GLfloat *v)   { GLColor4f(c, v[0], v[1], v[2], v[3]); }
void GLColor3fv(GLcontext c, GLfloat *v)   { GLColor4f(c, v[0], v[1], v[2], 1.0f); }
static inline __attribute__((always_inline)) void color_ub(GLcontext c, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    c->cur_color[0] = (float)r * (1.0f / 255.0f);
    c->cur_color[1] = (float)g * (1.0f / 255.0f);
    c->cur_color[2] = (float)b * (1.0f / 255.0f);
    c->cur_color[3] = (float)a * (1.0f / 255.0f);
}
void GLColor4ub(GLcontext c, GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    color_ub(c, r, g, b, a);
}
void GLColor4ubv(GLcontext c, GLubyte *v)  { GLColor4ub(c, v[0], v[1], v[2], v[3]); }
void GLColor3ubv(GLcontext c, GLubyte *v)  { GLColor4ub(c, v[0], v[1], v[2], 255); }

static void texcoord(GLcontext c, uint32_t u, float s, float t, float r, float q)
{
    if (u >= MGL_UNITS)
        return;
    c->cur_tex[u][0] = s;
    c->cur_tex[u][1] = t;
    c->cur_tex[u][2] = r;
    c->cur_tex[u][3] = q;
}
void GLTexCoord2f(GLcontext c, GLfloat s, GLfloat t)                       { texcoord(c, 0, s, t, 0.0f, 1.0f); }
void GLTexCoord2fv(GLcontext c, GLfloat *v)                                { texcoord(c, 0, v[0], v[1], 0.0f, 1.0f); }
void GLTexCoord4f(GLcontext c, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { texcoord(c, 0, s, t, r, q); }
void GLTexCoord4fv(GLcontext c, GLfloat *v)                                { texcoord(c, 0, v[0], v[1], v[2], v[3]); }

static int unit_of(GLenum u)
{
    if (u >= GL_TEXTURE0_ARB && u < GL_TEXTURE0_ARB + 4u)
        return (int)(u - GL_TEXTURE0_ARB);
    if (u >= 223u && u <= 226u)
        return (int)(u - 223u);
    return -1;
}

void GLMultiTexCoord2fARB(GLcontext c, GLenum unit, GLfloat s, GLfloat t)
{
    const int u = unit_of(unit);
    if (u < 0)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    texcoord(c, (uint32_t)u, s, t, 0.0f, 1.0f);
}
void GLMultiTexCoord2fvARB(GLcontext c, GLenum unit, GLfloat *v) { GLMultiTexCoord2fARB(c, unit, v[0], v[1]); }

void GLNormal3f(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    c->cur_normal[0] = x;                       /* kept: the standard has no lighting entry to consume it */
    c->cur_normal[1] = y;
    c->cur_normal[2] = z;
}

void GLActiveTextureARB(GLcontext c, GLenum unit)
{
    const int u = unit_of(unit);
    if (u < 0 || (uint32_t)u >= MGL_UNITS)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->active_unit = (GLenum)u;
}

void GLClientActiveTextureARB(GLcontext c, GLenum unit)
{
    const int u = unit_of(unit);
    if (u < 0 || (uint32_t)u >= MGL_UNITS)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->client_unit = (GLenum)u;
}

void GLEdgeFlag(GLcontext c, GLboolean flag)          { (void)c; (void)flag; }  /* polygons are filled: no edges  */
void GLEdgeFlagv(GLcontext c, const GLboolean *flag)  { (void)c; (void)flag; }
void GLIndexi(GLcontext c, GLint i)                   { (void)c; (void)i; }     /* colour index: not supported    */
void GLIndexiv(GLcontext c, const GLint *i)           { (void)c; (void)i; }

static uint32_t type_size(GLenum t)
{
    switch (t)
    {
    case GL_BYTE: case GL_UNSIGNED_BYTE:   return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
    case GL_DOUBLE: return 8;
    default: return 0;
    }
}

static void set_array(GLcontext c, mgl_array *a, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr,
                      GLint minsz, GLint maxsz)
{
    if (size < minsz || size > maxsz || stride < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    if (!type_size(type) && type != MGL_UBYTE_ARGB && type != MGL_UBYTE_BGRA)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    a->size = size;
    a->type = type;
    a->stride = stride;
    a->ptr = (const uint8_t *)ptr;
}

void GLVertexPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    set_array(c, &c->va_vertex, size, type, stride, pointer, 2, 4);
}
void GLColorPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    set_array(c, &c->va_color, size, type, stride, pointer, 3, 4);
}
void GLTexCoordPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    set_array(c, &c->va_tex[c->client_unit], size, type, stride, pointer, 1, 4);
}
void GLEdgeFlagPointer(GLcontext c, GLsizei stride, const GLvoid *pointer)            { (void)c; (void)stride; (void)pointer; }
void GLIndexPointer(GLcontext c, GLenum type, GLsizei stride, const GLvoid *pointer)  { (void)c; (void)type; (void)stride; (void)pointer; }

static mgl_array *client_array(GLcontext c, GLenum cap)
{
    switch (cap)
    {
    case GL_VERTEX_ARRAY:        return &c->va_vertex;
    case GL_COLOR_ARRAY:         return &c->va_color;
    case GL_TEXTURE_COORD_ARRAY: return &c->va_tex[c->client_unit];
    default:                     return 0;
    }
}

void GLEnableClientState(GLcontext c, GLenum cap)
{
    mgl_array *a = client_array(c, cap);
    if (a)
        a->enabled = 1;
    else if (cap != GL_INDEX_ARRAY && cap != GL_EDGE_FLAG_ARRAY)
        mgl_seterr(c, GL_INVALID_ENUM);
}

void GLDisableClientState(GLcontext c, GLenum cap)
{
    mgl_array *a = client_array(c, cap);
    if (a)
        a->enabled = 0;
    else if (cap != GL_INDEX_ARRAY && cap != GL_EDGE_FLAG_ARRAY)
        mgl_seterr(c, GL_INVALID_ENUM);
}

/* one component of an array element as a float (GL's normalisation applies to colours only) */
static float fetch(const uint8_t *p, GLenum type, uint32_t k, int norm)
{
    switch (type)
    {
    case GL_FLOAT:          { float f; memcpy(&f, p + 4u * k, 4); return f; }
    case GL_DOUBLE:         { double d; memcpy(&d, p + 8u * k, 8); return (float)d; }
    case GL_INT:            { int32_t i; memcpy(&i, p + 4u * k, 4); return norm ? (float)i * (1.0f / 2147483647.0f) : (float)i; }
    case GL_UNSIGNED_INT:   { uint32_t i; memcpy(&i, p + 4u * k, 4); return norm ? (float)i * (1.0f / 4294967295.0f) : (float)i; }
    case GL_SHORT:          { int16_t i; memcpy(&i, p + 2u * k, 2); return norm ? (float)i * (1.0f / 32767.0f) : (float)i; }
    case GL_UNSIGNED_SHORT: { uint16_t i; memcpy(&i, p + 2u * k, 2); return norm ? (float)i * (1.0f / 65535.0f) : (float)i; }
    case GL_BYTE:           return norm ? (float)(int8_t)p[k] * (1.0f / 127.0f) : (float)(int8_t)p[k];
    default:                return norm ? (float)p[k] * (1.0f / 255.0f) : (float)p[k];
    }
}

static void array_element(GLcontext c, GLint i)
{
    if (i < 0)
        return;
    if (c->va_color.enabled && c->va_color.ptr)
    {
        const mgl_array *a = &c->va_color;
        const uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * (type_size(a->type) ? type_size(a->type) : 1u);
        const uint8_t *p = a->ptr + es * (uint32_t)i;
        if (a->type == MGL_UBYTE_ARGB)
            GLColor4ub(c, p[1], p[2], p[3], p[0]);
        else if (a->type == MGL_UBYTE_BGRA)
            GLColor4ub(c, p[2], p[1], p[0], a->size == 4 ? p[3] : 255);
        else
            GLColor4f(c, fetch(p, a->type, 0, 1), fetch(p, a->type, 1, 1), fetch(p, a->type, 2, 1),
                      a->size == 4 ? fetch(p, a->type, 3, 1) : 1.0f);
    }
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        const mgl_array *a = &c->va_tex[u];
        if (!a->enabled || !a->ptr)
            continue;
        const uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * type_size(a->type);
        const uint8_t *p = a->ptr + es * (uint32_t)i;
        texcoord(c, u, fetch(p, a->type, 0, 0), a->size >= 2 ? fetch(p, a->type, 1, 0) : 0.0f,
                 a->size >= 3 ? fetch(p, a->type, 2, 0) : 0.0f, a->size >= 4 ? fetch(p, a->type, 3, 0) : 1.0f);
    }
    if (c->va_vertex.enabled && c->va_vertex.ptr)
    {
        const mgl_array *a = &c->va_vertex;
        const uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * type_size(a->type);
        const uint8_t *p = a->ptr + es * (uint32_t)i;
        emit_vertex(c, fetch(p, a->type, 0, 0), fetch(p, a->type, 1, 0), a->size >= 3 ? fetch(p, a->type, 2, 0) : 0.0f,
                    a->size >= 4 ? fetch(p, a->type, 3, 0) : 1.0f);
        c->st_verts_array++;
    }
}

void GLArrayElement(GLcontext c, GLint i)
{
    array_element(c, i);
}

enum { AF_NONE = 0, AF_FLOAT, AF_INT, AF_UBYTE, AF_ARGB, AF_BGRA };
typedef struct mgl_afetch
{
    const uint8_t *p;
    uint32_t es, kind, size;
} mgl_afetch;
typedef struct mgl_adraw
{
    mgl_afetch v, col, tex[MGL_UNITS];
} mgl_adraw;

static int afast_plan(GLcontext c, mgl_adraw *d)    /* 1 = every enabled array has a reader */
{
    if (c->arr_generic)
        return 0;
    const mgl_array *a = &c->va_vertex;
    if (!a->enabled || !a->ptr)
        return 0;                               /* no vertex array (colours / coordinates only): array_element */
    if (a->type == GL_FLOAT)
        d->v.kind = AF_FLOAT;
    else if (a->type == GL_INT)
        d->v.kind = AF_INT;
    else
        return 0;
    d->v.p = a->ptr;
    d->v.size = (uint32_t)a->size;
    d->v.es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * 4u;
    a = &c->va_color;
    d->col.kind = AF_NONE;
    if (a->enabled && a->ptr)
    {
        if (a->type == MGL_UBYTE_ARGB)
            d->col.kind = AF_ARGB;
        else if (a->type == MGL_UBYTE_BGRA)
            d->col.kind = AF_BGRA;
        else if (a->type == GL_UNSIGNED_BYTE)
            d->col.kind = AF_UBYTE;
        else if (a->type == GL_FLOAT)
            d->col.kind = AF_FLOAT;
        else
            return 0;
        d->col.p = a->ptr;
        d->col.size = (uint32_t)a->size;
        d->col.es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * (type_size(a->type) ? type_size(a->type) : 1u);
    }
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        a = &c->va_tex[u];
        d->tex[u].kind = AF_NONE;
        if (!a->enabled || !a->ptr)
            continue;
        if (a->type != GL_FLOAT)
            return 0;
        d->tex[u].kind = AF_FLOAT;
        d->tex[u].p = a->ptr;
        d->tex[u].size = (uint32_t)a->size;
        d->tex[u].es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * 4u;
    }
    return 1;
}

static inline __attribute__((always_inline)) float af_f(const uint8_t *p, uint32_t k)
{
    union { uint32_t u; float f; } x;
    memcpy(&x.u, p + 4u * k, 4);
    return x.f;
}
static inline __attribute__((always_inline)) float af_i(const uint8_t *p, uint32_t k)
{
    int32_t i;
    memcpy(&i, p + 4u * k, 4);
    return (float)i;
}

static inline __attribute__((always_inline)) void afast_element(GLcontext c, const mgl_adraw *d, uint32_t i)
{
    if (d->col.kind != AF_NONE)                 /* array_element's order: the colour, the coordinates, the vertex */
    {
        const uint8_t *p = d->col.p + d->col.es * i;
        if (d->col.kind == AF_ARGB)
            color_ub(c, p[1], p[2], p[3], p[0]);
        else if (d->col.kind == AF_BGRA)
            color_ub(c, p[2], p[1], p[0], d->col.size == 4 ? p[3] : 255u);
        else if (d->col.kind == AF_UBYTE)
        {
            c->cur_color[0] = clampf((float)p[0] * (1.0f / 255.0f));
            c->cur_color[1] = clampf((float)p[1] * (1.0f / 255.0f));
            c->cur_color[2] = clampf((float)p[2] * (1.0f / 255.0f));
            c->cur_color[3] = clampf(d->col.size == 4 ? (float)p[3] * (1.0f / 255.0f) : 1.0f);
        }
        else
        {
            c->cur_color[0] = clampf(af_f(p, 0));
            c->cur_color[1] = clampf(af_f(p, 1));
            c->cur_color[2] = clampf(af_f(p, 2));
            c->cur_color[3] = clampf(d->col.size == 4 ? af_f(p, 3) : 1.0f);
        }
    }
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        const mgl_afetch *t = &d->tex[u];
        if (t->kind == AF_NONE)
            continue;
        const uint8_t *p = t->p + t->es * i;
        c->cur_tex[u][0] = af_f(p, 0);
        c->cur_tex[u][1] = t->size >= 2 ? af_f(p, 1) : 0.0f;
        c->cur_tex[u][2] = t->size >= 3 ? af_f(p, 2) : 0.0f;
        c->cur_tex[u][3] = t->size >= 4 ? af_f(p, 3) : 1.0f;
    }
    const uint8_t *p = d->v.p + d->v.es * i;
    float x, y, z = 0.0f, w = 1.0f;
    if (d->v.kind == AF_FLOAT)
    {
        x = af_f(p, 0);
        y = af_f(p, 1);
        if (d->v.size >= 3)
            z = af_f(p, 2);
        if (d->v.size >= 4)
            w = af_f(p, 3);
    }
    else
    {
        x = af_i(p, 0);
        y = af_i(p, 1);
        if (d->v.size >= 3)
            z = af_i(p, 2);
        if (d->v.size >= 4)
            w = af_i(p, 3);
    }
    emit_vertex_core(c, x, y, z, w);
    c->st_verts_array++;
}

void GLDrawArrays(GLcontext c, GLenum mode, GLint first, GLsizei count)
{
    if (count < 0 || first < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    GLBegin(c, mode);
    if (!c->in_begin)
        return;
    mgl_adraw d;
    if (afast_plan(c, &d))
    {
        c->st_draws_fast++;
        for (GLsizei k = 0; k < count; k++)
            afast_element(c, &d, (uint32_t)first + (uint32_t)k);
    }
    else
    {
        c->st_draws_generic++;
        for (GLsizei k = 0; k < count; k++)
            array_element(c, first + (GLint)k);
    }
    GLEnd(c);
}

void GLMultiDrawArrays(GLcontext c, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    for (GLsizei p = 0; p < primcount; p++)
        GLDrawArrays(c, mode, first[p], count[p]);
}

void GLDrawElements(GLcontext c, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (count < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    GLBegin(c, mode);
    if (!c->in_begin)
        return;
    const uint8_t *ix = (const uint8_t *)indices;
    mgl_adraw d;
    const int fast = afast_plan(c, &d);
    if (fast)
        c->st_draws_fast++;
    else
        c->st_draws_generic++;
    for (GLsizei k = 0; k < count; k++)
    {
        uint32_t i;
        if (type == GL_UNSIGNED_BYTE)
            i = ix[k];
        else if (type == GL_UNSIGNED_SHORT)
        {
            uint16_t s;
            memcpy(&s, ix + 2 * k, 2);
            i = s;
        }
        else
            memcpy(&i, ix + 4 * k, 4);
        if (!fast)
            array_element(c, (GLint)i);
        else if ((GLint)i >= 0)                 /* array_element's own rule: a negative index draws nothing */
            afast_element(c, &d, i);
    }
    GLEnd(c);
}

void GLInterleavedArrays(GLcontext c, GLenum format, GLsizei stride, const GLvoid *pointer)
{
    const uint8_t *p = (const uint8_t *)pointer;
    GLsizei s;
    c->va_color.enabled = 0;
    c->va_tex[c->client_unit].enabled = 0;
    switch (format)
    {
    case GL_V2F:
        s = stride ? stride : 8;
        GLVertexPointer(c, 2, GL_FLOAT, s, p);
        break;
    case GL_V3F:
        s = stride ? stride : 12;
        GLVertexPointer(c, 3, GL_FLOAT, s, p);
        break;
    case GL_C4UB_V2F:
        s = stride ? stride : 12;
        GLColorPointer(c, 4, GL_UNSIGNED_BYTE, s, p);
        GLVertexPointer(c, 2, GL_FLOAT, s, p + 4);
        c->va_color.enabled = 1;
        break;
    case GL_C4UB_V3F:
        s = stride ? stride : 16;
        GLColorPointer(c, 4, GL_UNSIGNED_BYTE, s, p);
        GLVertexPointer(c, 3, GL_FLOAT, s, p + 4);
        c->va_color.enabled = 1;
        break;
    case GL_C3F_V3F:
        s = stride ? stride : 24;
        GLColorPointer(c, 3, GL_FLOAT, s, p);
        GLVertexPointer(c, 3, GL_FLOAT, s, p + 12);
        c->va_color.enabled = 1;
        break;
    case GL_T2F_V3F:
        s = stride ? stride : 20;
        GLTexCoordPointer(c, 2, GL_FLOAT, s, p);
        GLVertexPointer(c, 3, GL_FLOAT, s, p + 8);
        c->va_tex[c->client_unit].enabled = 1;
        break;
    case GL_T2F_C4UB_V3F:
        s = stride ? stride : 24;
        GLTexCoordPointer(c, 2, GL_FLOAT, s, p);
        GLColorPointer(c, 4, GL_UNSIGNED_BYTE, s, p + 8);
        GLVertexPointer(c, 3, GL_FLOAT, s, p + 12);
        c->va_tex[c->client_unit].enabled = 1;
        c->va_color.enabled = 1;
        break;
    case GL_T2F_C3F_V3F:
        s = stride ? stride : 32;
        GLTexCoordPointer(c, 2, GL_FLOAT, s, p);
        GLColorPointer(c, 3, GL_FLOAT, s, p + 8);
        GLVertexPointer(c, 3, GL_FLOAT, s, p + 20);
        c->va_tex[c->client_unit].enabled = 1;
        c->va_color.enabled = 1;
        break;
    default:
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->va_vertex.enabled = 1;
}

void GLLockArrays(GLcontext c, GLuint first, GLsizei count)   { (void)c; (void)first; (void)count; }  /* a hint */
void GLUnlockArrays(GLcontext c)                              { (void)c; }

void mgl_geom_init(GLcontext c)
{
    for (uint32_t i = 0; i < MGL_MV_DEPTH; i++)
        m4_ident(&c->mv[i]);
    for (uint32_t i = 0; i < MGL_PROJ_DEPTH; i++)
        m4_ident(&c->proj[i]);
    for (uint32_t i = 0; i < MGL_TEX_DEPTH; i++)
        m4_ident(&c->texm[i]);
    c->mv_sp = c->proj_sp = c->tex_sp = 0;
    c->tex_identity = 1;
    c->mvp_dirty = 1;
    c->matrix_mode = GL_MODELVIEW;
    c->vp_x = 0;
    c->vp_y = 0;
    c->vp_w = c->width;
    c->vp_h = c->height;
    c->depth_n = 0.0f;
    c->depth_f = 1.0f;
    c->geom_dirty = 1;
    c->cur_color[0] = c->cur_color[1] = c->cur_color[2] = c->cur_color[3] = 1.0f;
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        c->cur_tex[u][0] = c->cur_tex[u][1] = c->cur_tex[u][2] = 0.0f;
        c->cur_tex[u][3] = 1.0f;
    }
    c->cur_normal[0] = c->cur_normal[1] = 0.0f;
    c->cur_normal[2] = 1.0f;
}

static MGL_A5_TEXT void __attribute__((noinline)) emit_lit(GLcontext c, float x, float y, float z, float w)
{
    if (prim_flat(c->prim))                     /* MiniGL's device-coordinate 2D: never lit */
    {
        emit_vertex(c, x, y, z, w);
        return;
    }
    float save[4];
    memcpy(save, c->cur_color, sizeof(save));
    mgl_light_rgba(c, x, y, z, w, c->cur_color);
    emit_vertex(c, x, y, z, w);
    memcpy(c->cur_color, save, sizeof(save));
}
#include "mgl_mtex_inl.h"

static MGL_A5_TEXT void __attribute__((noinline)) vertex_a5_slow(GLcontext c, mgl_ctx5 *x5, float x, float y, float z,
                                                                  float w)
{
    if (c->in_begin && c->imm_total == 0u)      /* a primitive's first vertex: kept from here, or the pass first */
        mgl_mtex_prim(c, mgl_mtex_unit1(c));
    float keep[4];
    if (x5->tg_on)
    {
        memcpy(keep, c->cur_tex[0], sizeof(keep));
        mgl_texgen(c, x5, x, y, z, w, c->cur_tex[0]);
    }
    if (x5->en_lighting)
        emit_lit(c, x, y, z, w);
    else
        emit_vertex(c, x, y, z, w);
    if (x5->tg_on)
        memcpy(c->cur_tex[0], keep, sizeof(keep));             /* (GL: the current coordinates as they were) */
    if (x5->mt_rec && (x5->mt_generic || !mt_vertex_fast(c, x5, x, y, z, w)))
        mgl_mtex_vertex(c, x, y, z, w);
}
static inline __attribute__((always_inline)) void vertex_a5(GLcontext c, float x, float y, float z, float w)
{
    mgl_ctx5 *const x5 = X5(c);
    if (__builtin_expect((x5->en_lighting | x5->tg_on | x5->mt_live | c->en_tex2d[1]) == 0u, 1))
        emit_vertex(c, x, y, z, w);
    else
        vertex_a5_slow(c, x5, x, y, z, w);
}
MGL_A5_TEXT void GLVertex2fv_a5(GLcontext c, GLfloat *v)
{
    if (!v) return;
    vertex_a5(c, v[0], v[1], 0.0f, 1.0f);
}
MGL_A5_TEXT void GLVertex3fv_a5(GLcontext c, GLfloat *v)
{
    if (!v) return;
    vertex_a5(c, v[0], v[1], v[2], 1.0f);
}
MGL_A5_TEXT void GLVertex4fv_a5(GLcontext c, GLfloat *v)
{
    if (!v) return;
    vertex_a5(c, v[0], v[1], v[2], v[3]);
}
MGL_A5_TEXT void GLVertex4f_a5(GLcontext c, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    vertex_a5(c, x, y, z, w);
}
MGL_A5_TEXT void GLColor3fv_a5(GLcontext c, GLfloat *v)   { if (v) GLColor4f(c, v[0], v[1], v[2], 1.0f); }
MGL_A5_TEXT void GLColor4fv_a5(GLcontext c, GLfloat *v)   { if (v) GLColor4f(c, v[0], v[1], v[2], v[3]); }
MGL_A5_TEXT void GLColor3ubv_a5(GLcontext c, GLubyte *v)  { if (v) color_ub(c, v[0], v[1], v[2], 255); }
MGL_A5_TEXT void GLColor4ubv_a5(GLcontext c, GLubyte *v)  { if (v) color_ub(c, v[0], v[1], v[2], v[3]); }
MGL_A5_TEXT void GLTexCoord2fv_a5(GLcontext c, GLfloat *v) { if (v) texcoord(c, 0, v[0], v[1], 0.0f, 1.0f); }
MGL_A5_TEXT void GLTexCoord4fv_a5(GLcontext c, GLfloat *v) { if (v) texcoord(c, 0, v[0], v[1], v[2], v[3]); }
MGL_A5_TEXT void GLMultiTexCoord2fvARB_a5(GLcontext c, GLenum unit, GLfloat *v) { if (v) GLMultiTexCoord2fARB(c, unit, v[0], v[1]); }

MGL_A5_TEXT void mgl_array_fetch(GLcontext c, GLint i, mgl_afetched *o)
{
    o->has_col = o->has_vtx = o->tex_mask = 0;
    if (i < 0)
        return;
    if (c->va_color.enabled && c->va_color.ptr)
    {
        const mgl_array *a = &c->va_color;
        const uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * (type_size(a->type) ? type_size(a->type) : 1u);
        const uint8_t *p = a->ptr + es * (uint32_t)i;
        uint32_t ub[4] = { 0, 0, 0, 255 };
        int is_ub = 1;
        if (a->type == MGL_UBYTE_ARGB)
        {
            ub[0] = p[1]; ub[1] = p[2]; ub[2] = p[3]; ub[3] = p[0];
        }
        else if (a->type == MGL_UBYTE_BGRA)
        {
            ub[0] = p[2]; ub[1] = p[1]; ub[2] = p[0]; ub[3] = a->size == 4 ? p[3] : 255;
        }
        else
        {
            is_ub = 0;
            o->col[0] = fetch(p, a->type, 0, 1);
            o->col[1] = fetch(p, a->type, 1, 1);
            o->col[2] = fetch(p, a->type, 2, 1);
            o->col[3] = a->size == 4 ? fetch(p, a->type, 3, 1) : 1.0f;
        }
        if (is_ub)
            for (uint32_t k = 0; k < 4u; k++)
                o->col[k] = (float)ub[k] * (1.0f / 255.0f);
        o->has_col = 1;
    }
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        const mgl_array *a = &c->va_tex[u];
        if (!a->enabled || !a->ptr)
            continue;
        const uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * type_size(a->type);
        const uint8_t *p = a->ptr + es * (uint32_t)i;
        o->tex[u][0] = fetch(p, a->type, 0, 0);
        o->tex[u][1] = a->size >= 2 ? fetch(p, a->type, 1, 0) : 0.0f;
        o->tex[u][2] = a->size >= 3 ? fetch(p, a->type, 2, 0) : 0.0f;
        o->tex[u][3] = a->size >= 4 ? fetch(p, a->type, 3, 0) : 1.0f;
        o->tex_mask |= 1u << u;
    }
    if (c->va_vertex.enabled && c->va_vertex.ptr)
    {
        const mgl_array *a = &c->va_vertex;
        const uint32_t es = a->stride ? (uint32_t)a->stride : (uint32_t)a->size * type_size(a->type);
        const uint8_t *p = a->ptr + es * (uint32_t)i;
        o->vtx[0] = fetch(p, a->type, 0, 0);
        o->vtx[1] = fetch(p, a->type, 1, 0);
        o->vtx[2] = a->size >= 3 ? fetch(p, a->type, 2, 0) : 0.0f;
        o->vtx[3] = a->size >= 4 ? fetch(p, a->type, 3, 0) : 1.0f;
        o->has_vtx = 1;
    }
}

static inline __attribute__((always_inline)) float mt_clamp(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}
MGL_A5_TEXT void mgl_mtex_draw(GLcontext c, const mgl_mtex_v *pool, const uint16_t *idx, uint32_t ntri)
{
    GLBegin(c, GL_TRIANGLES);
    if (!c->in_begin)
        return;
    c->st_draws_fast++;
    const uint32_t def = c->deferred;
    struct { uint32_t tag; float x, y, z, w; } xc[16];     /* the immediate path's transformed pool vertices, by index */
    for (uint32_t k = 0; k < 16u; k++)
        xc[k].tag = 0xFFFFFFFFu;
    for (uint32_t n = 0; n < ntri; n++, idx += 3)
    {
        mgl_cvtx v[3];
        for (uint32_t k = 0; k < 3u; k++)
        {
            const uint32_t i = idx[k];
            const mgl_mtex_v *p = &pool[i];
            if (def)
            {
                v[k].x = p->x; v[k].y = p->y; v[k].z = p->z; v[k].w = p->w;      /* object space: the stage runs later */
            }
            else
            {
                if (xc[i & 15u].tag != i)
                {
                    if (c->mvp_dirty)
                        update_mvp(c);
                    mgl_stage_xform(c->mvp.m, p->x, p->y, p->z, p->w, &v[k]);
                    xc[i & 15u].tag = i;
                    xc[i & 15u].x = v[k].x; xc[i & 15u].y = v[k].y; xc[i & 15u].z = v[k].z; xc[i & 15u].w = v[k].w;
                }
                else
                {
                    v[k].x = xc[i & 15u].x; v[k].y = xc[i & 15u].y; v[k].z = xc[i & 15u].z; v[k].w = xc[i & 15u].w;
                }
            }
            v[k].s = p->s;                      /* (the emit: q 1, unit 0's matrix the identity -> s t as they are) */
            v[k].t = p->t;
            v[k].r = mt_clamp(p->r);              /* (the fast fetcher's GL_FLOAT colour: clamped) */
            v[k].g = mt_clamp(p->g);
            v[k].b = mt_clamp(p->b);
            v[k].a = mt_clamp(p->a);
        }
        c->st_verts += 3u;
        c->st_verts_array += 3u;
        c->imm_total += 3u;
        tri(c, &v[0], &v[1], &v[2], &v[2]);
    }
    GLEnd(c);
}
