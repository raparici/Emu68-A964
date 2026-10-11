/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: textures larger than the renderer keeps, filtered down. */

#include <string.h>
#include "mgl_core.h"

void mgl_seterr(GLcontext c, GLenum e);        /* mgl_state.c */

#define DS_MAX 256u                             /* the engine's A3D_MAX_TEXSIZE */

static uint32_t pow2floor(uint32_t v)
{
    uint32_t p = 1u;
    while (p * 2u <= v)
        p *= 2u;
    return p;
}

static uint32_t lvl_bytes(uint32_t fmt)         /* the core's level formats (mgl_state.c fmt_bytes) */
{
    return fmt == MGL_TF_RGBA8 ? 4u : fmt == MGL_TF_RGB8 ? 3u : fmt == MGL_TF_LA8 ? 2u : 1u;
}

static uint32_t src_bpt(GLenum format, GLenum type)        /* the program's texel size (mgl_state.c src_texel) */
{
    if (format == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_5_6_5 || format == MGL_UNSIGNED_SHORT_4_4_4_4 ||
        type == MGL_UNSIGNED_SHORT_4_4_4_4)
        return 2u;
    if (type != GL_UNSIGNED_BYTE)
        return 0u;
    switch (format)
    {
    case GL_RGBA: case MGL_UBYTE_ARGB: case MGL_UBYTE_BGRA: return 4u;
    case GL_RGB: return 3u;
    case GL_LUMINANCE_ALPHA: return 2u;
    case GL_LUMINANCE: case GL_ALPHA: case GL_INTENSITY: case GL_COLOR_INDEX: return 1u;
    default: return 0u;
    }
}

uint32_t mgl_ds_live = 0;

static mgl_ds *ds_find(mgl_ctx5 *x5, uint32_t name)
{
    for (uint32_t i = 0; i < x5->nds; i++)
        if (x5->ds[i].name == name)
            return &x5->ds[i];
    return 0;
}

static void ds_drop(mgl_ctx5 *x5, uint32_t name)
{
    mgl_ds *d = ds_find(x5, name);
    if (d)
    {
        *d = x5->ds[--x5->nds];
        mgl_ds_live--;
    }
}

static mgl_ds *ds_put(GLcontext c, mgl_ctx5 *x5, uint32_t name)
{
    mgl_ds *d = ds_find(x5, name);
    if (d)
        return d;
    if (x5->nds >= x5->capds)
    {
        const uint32_t n = x5->capds ? 2u * x5->capds : 32u;
        mgl_ds *q = c->sink.alloc ? (mgl_ds *)c->sink.alloc(c->sink.ud, n * (uint32_t)sizeof(mgl_ds)) : 0;
        if (!q)
            return 0;
        if (x5->ds)
        {
            memcpy(q, x5->ds, x5->nds * sizeof(mgl_ds));
            if (c->sink.free)
                c->sink.free(c->sink.ud, x5->ds, x5->capds * (uint32_t)sizeof(mgl_ds));
        }
        x5->ds = q;
        x5->capds = n;
    }
    d = &x5->ds[x5->nds++];
    mgl_ds_live++;
    d->name = name;
    d->ow = d->oh = 0;
    return d;
}

void mgl_ds_free(GLcontext c, mgl_ctx5 *x5)
{
    mgl_ds_live -= x5->nds;
    if (x5->ds && c->sink.free)
        c->sink.free(c->sink.ud, x5->ds, x5->capds * (uint32_t)sizeof(mgl_ds));
    x5->ds = 0;
    x5->nds = x5->capds = 0;
}

static int band(GLcontext c, mgl_texobj *t, uint8_t *scratch, uint32_t w, uint32_t n, GLenum format, GLenum type,
                const uint8_t *src)
{
    mgl_level *lv = &t->lv[0];
    const mgl_level keep = *lv;
    const uint32_t dirty = t->dirty, nlevels = t->nlevels;
    const GLenum err = c->error;
    lv->px = scratch;
    lv->w = w;
    lv->h = n;
    t->dirty = 1;
    if (!t->nlevels)
        t->nlevels = 1;
    c->error = GL_NO_ERROR;
    GLTexSubImage2D(c, GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)n, format, type, src);
    const int ok = c->error == GL_NO_ERROR;
    c->error = err;
    *lv = keep;
    t->dirty = dirty;
    t->nlevels = nlevels;
    return ok;
}

/* the part of length (b - a) of the cell [k u, (k + 1) u) that [a, b) covers */
static uint32_t cover(uint32_t a, uint32_t b, uint32_t k, uint32_t u)
{
    const uint32_t lo = k * u > a ? k * u : a, hi = (k + 1u) * u < b ? (k + 1u) * u : b;
    return hi > lo ? hi - lo : 0u;
}

static int ds_fill(GLcontext c, mgl_texobj *t, uint32_t ow, uint32_t oh, uint32_t sx, uint32_t sy, uint32_t sw,
                   uint32_t sh, GLenum format, GLenum type, const uint8_t *src, uint32_t r[4])
{
    mgl_level *lv = &t->lv[0];
    const uint32_t tw = lv->w, th = lv->h, ob = lvl_bytes(t->fmt), bpt = src_bpt(format, type);
    if (!bpt || !lv->px)
        return 0;
    const uint32_t rowpx = c->unpack_row > 0 ? (uint32_t)c->unpack_row : sw;
    const uint32_t al = c->unpack_align > 0 ? (uint32_t)c->unpack_align : 4u;
    const uint32_t stride = (rowpx * bpt + al - 1u) / al * al;
    const uint32_t dx0 = sx * tw / ow, dx1 = ((sx + sw) * tw + ow - 1u) / ow;
    const uint32_t dy0 = sy * th / oh, dy1 = ((sy + sh) * th + oh - 1u) / oh;
    const uint32_t band_max = (oh + th - 1u) / th + 1u;
    const uint32_t sbytes = sw * band_max * ob;
    uint8_t *scratch = c->sink.alloc ? (uint8_t *)c->sink.alloc(c->sink.ud, sbytes) : 0;
    if (!scratch)
    {
        mgl_seterr(c, GL_OUT_OF_MEMORY);
        r[0] = r[1] = r[2] = r[3] = 0;
        return 1;
    }
    int ok = 1;
    for (uint32_t dy = dy0; dy < dy1 && dy < th && ok; dy++)
    {
        const uint32_t ya = dy * oh, yb = ya + oh;
        uint32_t r0 = ya / th, r1 = (yb + th - 1u) / th;
        if (r0 < sy)
            r0 = sy;
        if (r1 > sy + sh)
            r1 = sy + sh;
        if (r0 >= r1)
            continue;
        if (!band(c, t, scratch, sw, r1 - r0, format, type, src + (r0 - sy) * stride))
        {
            ok = 0;
            break;
        }
        for (uint32_t dx = dx0; dx < dx1 && dx < tw; dx++)
        {
            const uint32_t xa = dx * ow, xb = xa + ow;
            uint32_t c0 = xa / tw, c1 = (xb + tw - 1u) / tw;
            if (c0 < sx)
                c0 = sx;
            if (c1 > sx + sw)
                c1 = sx + sw;
            uint32_t sum[4] = { 0, 0, 0, 0 }, wsum = 0;
            for (uint32_t row = r0; row < r1; row++)
            {
                const uint32_t wy = cover(ya, yb, row, th);
                const uint8_t *p = scratch + ((row - r0) * sw + (c0 - sx)) * ob;
                for (uint32_t col = c0; col < c1; col++, p += ob)
                {
                    const uint32_t wgt = cover(xa, xb, col, tw) * wy;
                    for (uint32_t k = 0; k < ob; k++)
                        sum[k] += p[k] * wgt;
                    wsum += wgt;
                }
            }
            if (!wsum)
                continue;
            uint8_t *o = lv->px + (dy * tw + dx) * ob;
            for (uint32_t k = 0; k < ob; k++)
                o[k] = (uint8_t)((sum[k] + wsum / 2u) / wsum);
        }
    }
    if (c->sink.free)
        c->sink.free(c->sink.ud, scratch, sbytes);
    r[0] = dx0;
    r[1] = dy0;
    r[2] = (dx1 < tw ? dx1 : tw) - dx0;
    r[3] = (dy1 < th ? dy1 : th) - dy0;
    return ok;
}

void mgl_ds_image_slow(GLcontext c, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum format,
              GLenum type, const GLvoid *pixels);
void mgl_ds_subimage_slow(GLcontext c, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                 GLenum type, const GLvoid *pixels);

#if defined(__mc68000__) || defined(__m68k__)
void mgl_ds_TexImage2D(GLcontext c, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                       GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)c; (void)target; (void)level; (void)ifmt; (void)w; (void)h; (void)border; (void)format; (void)type; (void)pixels;
    __asm__ volatile ("tst.l %c0\n\t"
                      "bne.s 9f\n\t"
                      "move.l 20(sp),d0\n\t"          /* w: 1 .. 256 and a power of two */
                      "subq.l #1,d0\n\t"
                      "cmp.l #255,d0\n\t"
                      "bhi.s 9f\n\t"
                      "and.l 20(sp),d0\n\t"
                      "bne.s 9f\n\t"
                      "move.l 24(sp),d0\n\t"          /* h: the same */
                      "subq.l #1,d0\n\t"
                      "cmp.l #255,d0\n\t"
                      "bhi.s 9f\n\t"
                      "and.l 24(sp),d0\n\t"
                      "bne.s 9f\n\t"
                      "jmp %c1\n"
                      "9:\tjmp %c2"
                      :: "i"(&mgl_ds_live), "i"(GLTexImage2D), "i"(mgl_ds_image_slow) : "d0", "cc", "memory");
    __builtin_unreachable();
}

void mgl_ds_TexSubImage2D(GLcontext c, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                          GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)c; (void)target; (void)level; (void)x; (void)y; (void)w; (void)h; (void)format; (void)type; (void)pixels;
    __asm__ volatile ("tst.l %c0\n\t"
                      "bne.s 9f\n\t"
                      "jmp %c1\n"
                      "9:\tjmp %c2"
                      :: "i"(&mgl_ds_live), "i"(GLTexSubImage2D), "i"(mgl_ds_subimage_slow) : "cc", "memory");
    __builtin_unreachable();
}
#else
void mgl_ds_TexImage2D(GLcontext c, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                       GLenum format, GLenum type, const GLvoid *pixels)
{
    if (!mgl_ds_live && w > 0 && h > 0 && w <= (GLsizei)DS_MAX && h <= (GLsizei)DS_MAX && !(w & (w - 1)) && !(h & (h - 1)))
        GLTexImage2D(c, target, level, ifmt, w, h, border, format, type, pixels);
    else
        mgl_ds_image_slow(c, target, level, ifmt, w, h, border, format, type, pixels);
}

void mgl_ds_TexSubImage2D(GLcontext c, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                          GLenum format, GLenum type, const GLvoid *pixels)
{
    if (!mgl_ds_live)
        GLTexSubImage2D(c, target, level, x, y, w, h, format, type, pixels);
    else
        mgl_ds_subimage_slow(c, target, level, x, y, w, h, format, type, pixels);
}
#endif

void mgl_ds_image_slow(GLcontext c, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
              GLenum format, GLenum type, const GLvoid *pixels)
{
    const uint32_t name = c->bound[c->active_unit];
    if (target != GL_TEXTURE_2D || name >= MGL_MAX_TEXNAMES || level < 0 || w < 1 || h < 1 || w > 2048 || h > 2048 ||
        border != 0)
    {
        GLTexImage2D(c, target, level, ifmt, w, h, border, format, type, pixels);   /* (the core reports it) */
        return;
    }
    mgl_ctx5 *const x5 = X5(c);
    mgl_texobj *const t = &c->tex[name];
    if (level > 0)
    {
        if (!ds_find(x5, name))
            GLTexImage2D(c, target, level, ifmt, w, h, border, format, type, pixels);
        return;                                 /* a reduced texture keeps level 0 only */
    }
    const uint32_t tw = pow2floor((uint32_t)w < DS_MAX ? (uint32_t)w : DS_MAX);
    const uint32_t th = pow2floor((uint32_t)h < DS_MAX ? (uint32_t)h : DS_MAX);
    if (tw == (uint32_t)w && th == (uint32_t)h)
    {
        if (x5->nds)
            ds_drop(x5, name);
        GLTexImage2D(c, target, level, ifmt, w, h, border, format, type, pixels);
        return;
    }
    if (pixels && !src_bpt(format, type))
    {
        if (x5->nds)
            ds_drop(x5, name);
        GLTexImage2D(c, target, level, ifmt, w, h, border, format, type, pixels);   /* (a format the core refuses) */
        return;
    }
    mgl_ds *const d = ds_put(c, x5, name);
    if (!d)
    {
        GLTexImage2D(c, target, level, ifmt, w, h, border, format, type, pixels);   /* no table: the engine's way */
        return;
    }
    const GLenum err = c->error;
    c->error = GL_NO_ERROR;
    GLTexImage2D(c, target, 0, ifmt, (GLsizei)tw, (GLsizei)th, 0, format, type, 0);   /* the stored level, zeroed */
    const GLenum e = c->error;
    c->error = err != GL_NO_ERROR ? err : e;
    if (e != GL_NO_ERROR || !t->lv[0].px)
    {
        ds_drop(x5, name);
        return;
    }
    for (uint32_t l = 1; l < MGL_MAX_LEVELS; l++)  /* the smaller levels of an earlier image: gone */
        if (t->lv[l].px)
        {
            if (c->sink.free)
                c->sink.free(c->sink.ud, t->lv[l].px, t->lv[l].w * t->lv[l].h * lvl_bytes(t->fmt));
            t->lv[l].px = 0;
            t->lv[l].w = t->lv[l].h = 0;
        }
    t->nlevels = 1;
    t->dirty = 1;
    d->ow = (uint32_t)w;
    d->oh = (uint32_t)h;
    uint32_t r[4];
    if (pixels && !ds_fill(c, t, d->ow, d->oh, 0, 0, (uint32_t)w, (uint32_t)h, format, type, (const uint8_t *)pixels, r))
        mgl_seterr(c, GL_INVALID_ENUM);
}

void mgl_ds_subimage_slow(GLcontext c, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                 GLenum format, GLenum type, const GLvoid *pixels)
{
    const uint32_t name = c->bound[c->active_unit];
    mgl_ctx5 *const x5 = X5(c);
    mgl_ds *const d = x5->nds && target == GL_TEXTURE_2D ? ds_find(x5, name) : 0;
    if (!d)
    {
        GLTexSubImage2D(c, target, level, x, y, w, h, format, type, pixels);   /* the core's path, unchanged */
        return;
    }
    mgl_texobj *const t = &c->tex[name];
    if (c->in_begin)
        mgl_seterr(c, GL_INVALID_OPERATION);
    if (level != 0)
        return;                                 /* a reduced texture keeps level 0 only */
    if (x < 0 || y < 0 || w < 0 || h < 0 || (uint32_t)(x + w) > d->ow || (uint32_t)(y + h) > d->oh || !t->lv[0].px)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    if (!w || !h || !pixels)
        return;
    mgl_core_flush(c);
    uint32_t r[4];
    if (!ds_fill(c, t, d->ow, d->oh, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, format, type,
                 (const uint8_t *)pixels, r))
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (!r[2] || !r[3])
        return;
    if (!t->dirty && t->handle && c->sink.tex_update)
        c->sink.tex_update(c->sink.ud, t->handle, t->fmt, 0, &t->lv[0], r[0], r[1], r[2], r[3]);
    else
        t->dirty = 1;
}

static int tc_bound(GLcontext c, uint32_t n)
{
    for (uint32_t u = 0; u < MGL_UNITS; u++)
        if (c->bound[u] == n)
            return 1;
    return 0;
}

static uint32_t tc_bytes(const mgl_level *lv0, uint32_t nlevels)
{
    const uint32_t w = lv0->w < DS_MAX ? lv0->w : DS_MAX, h = lv0->h < DS_MAX ? lv0->h : DS_MAX;
    uint32_t b = w * h * 2u;
    if (nlevels > 1u)
        b += b / 3u;
    return (b + 4095u) & ~4095u;
}

/* the texture the renderer holds that was drawn least recently and is bound to no unit (0: none) */
static uint32_t tc_victim(GLcontext c, const mgl_ctx5 *x)
{
    uint32_t best = 0, bs = 0xFFFFFFFFu;
    for (uint32_t n = 1; n < MGL_MAX_TEXNAMES; n++)
        if (c->tex[n].handle && x->tc_stamp[n] < bs && !tc_bound(c, n))
        {
            best = n;
            bs = x->tc_stamp[n];
        }
    return best;
}

static uint32_t tc_create(void *ud, uint32_t fmt, uint32_t nl, const mgl_level *lv)
{
    mgl_ctx5 *const x = mgl_x5_of_ud(ud);
    if (!x)
        return 0;                               /* (cannot happen: the sink's own entries go back before the block) */
    uint32_t h = x->tc_create(ud, fmt, nl, lv);
    if (h)
        return h;
    GLcontext c = x->owner;
    uint32_t nomem = 0, n, k = 0, freed = 0;
    const uint32_t budget = 2u * tc_bytes(&lv[0], nl);
    h = x->tc_why(ud, fmt, nl, lv, &nomem);     /* asked again, the reason kept */
    while (!h && nomem && freed < budget && (n = tc_victim(c, x)) != 0)
    {
        mgl_texobj *const t = &c->tex[n];
        if (c->sink.tex_free)
            c->sink.tex_free(ud, t->handle);
        t->handle = 0;                          /* made again when next drawn (mgl_raster_sync: !t->handle) */
        freed += tc_bytes(&t->lv[0], t->nlevels);
        x->tc_evicted++;
        k++;
        h = x->tc_why(ud, fmt, nl, lv, &nomem);
    }
    if (k)
        c->rs_valid = 0;                        /* the raster state goes to the sink again: a handle may be a freed one's */
    if (!h)
        x->tc_refused++;
    return h;
}

static void tc_state(void *ud, const mgl_raster *r)
{
    mgl_ctx5 *const x = mgl_x5_of_ud(ud);
    if (!x)
        return;
    GLcontext c = x->owner;
    const uint32_t clk = ++x->tc_clock, n = c->bound[0];
    x->tc_stamp[x->tc_cur] = clk;
    x->tc_cur = (r->tex && n < MGL_MAX_TEXNAMES && c->tex[n].handle == r->tex) ? n : 0u;
    x->tc_stamp[x->tc_cur] = clk;
    x->tc_state(ud, r);
}

void mgl_tc_attach(GLcontext c, mgl_tc_why_fn why)
{
    mgl_ctx5 *const x = mgl_x5_find(c);
    if (!why || x->owner != c || x->tc_create || !c->sink.tex_create || !c->sink.state || !c->sink.alloc)
        return;                                 /* (no block of its own: the shared one, out of memory) */
    x->tc_stamp = (uint32_t *)c->sink.alloc(c->sink.ud, MGL_MAX_TEXNAMES * 4u);
    if (!x->tc_stamp)
        return;
    memset(x->tc_stamp, 0, MGL_MAX_TEXNAMES * 4u);
    x->tc_why = why;
    x->tc_create = c->sink.tex_create;
    x->tc_state = c->sink.state;
    x->tc_clock = x->tc_cur = 0;
    c->sink.tex_create = tc_create;
    c->sink.state = tc_state;
}

void mgl_tc_detach(GLcontext c, mgl_ctx5 *x5)
{
    if (!x5->tc_create)
        return;
    if (c->sink.tex_create == tc_create)
        c->sink.tex_create = x5->tc_create;
    if (c->sink.state == tc_state)
        c->sink.state = x5->tc_state;
    if (x5->tc_stamp && c->sink.free)
        c->sink.free(c->sink.ud, x5->tc_stamp, MGL_MAX_TEXNAMES * 4u);
    x5->tc_stamp = 0;
    x5->tc_create = 0;
    x5->tc_state = 0;
}
