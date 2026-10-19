/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the minigl.library 29.x interface (dispatch ABI 5 and 4). */

#include <string.h>
#include "mgl_core.h"
#include "mgl_gl5.h"
#include "mgl_tok5.h"
#include "mgl_abi5_protos.h"

void mgl_seterr(GLcontext c, GLenum e);        /* mgl_state.c */

#define TOK_BAD 0x7FFF0000u                     /* no internal token: the core answers GL_INVALID_ENUM */
#define NTOK_FWD (sizeof(TOK5_FWD) / sizeof(TOK5_FWD[0]))
#define NTOK_REV (sizeof(TOK5_REV) / sizeof(TOK5_REV[0]))

#define TR_MEMO 64u
static uint32_t tr_memo[TR_MEMO] = { 0 };
static uint32_t __attribute__((noinline)) tr_search(uint32_t v)
{
    uint32_t lo = 0, hi = (uint32_t)NTOK_FWD;
    while (lo < hi)
    {
        const uint32_t mid = (lo + hi) >> 1;
        if (TOK5_FWD[mid][0] < v)
            lo = mid + 1u;
        else
            hi = mid;
    }
    const uint32_t r = lo < NTOK_FWD && TOK5_FWD[lo][0] == v ? TOK5_FWD[lo][1] : TOK_BAD;
    if (v <= 0xFFFFu && r <= 0xFFFFu && v)
        tr_memo[(v ^ (v >> 6)) & (TR_MEMO - 1u)] = v << 16 | r;
    return r;
}
static inline __attribute__((always_inline)) uint32_t tr(uint32_t v)   /* a generic ABI-5 token -> ours */
{
    const uint32_t e = tr_memo[(v ^ (v >> 6)) & (TR_MEMO - 1u)];
    if (e && (e >> 16) == v)
        return e & 0xFFFFu;
    return tr_search(v);
}

static const uint32_t PRIM_IN[10] = { GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES,
                                      GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS, GL_QUAD_STRIP, GL_POLYGON };

static uint32_t tr_prim(uint32_t v)
{
    if (v < 10u)
        return PRIM_IN[v];
    if (v == MGL5_FLATFAN)
        return MGL_FLATFAN;
    if (v == MGL5_FLATSTRIP)
        return MGL_FLATSTRIP;
    return TOK_BAD;
}

static uint32_t tr_blend(uint32_t v)
{
    return v == 0u ? (uint32_t)GL_ZERO : v == 1u ? (uint32_t)GL_ONE : tr(v);
}

static uint32_t tr_mask(uint32_t m)
{
    uint32_t r = 0;
    if (m & GL5_COLOR_BUFFER_BIT)
        r |= GL_COLOR_BUFFER_BIT;
    if (m & GL5_DEPTH_BUFFER_BIT)
        r |= GL_DEPTH_BUFFER_BIT;
    return r;
}

static uint32_t tr_ifmt(uint32_t v)             /* an internal format: 1..4 (components) as they are */
{
    return v >= 1u && v <= 4u ? v : tr(v);
}

/* ours -> ABI 5, for the queries' answers (an enum-valued state) and glGetError */
static uint32_t rtr(uint32_t v)
{
    if (v == GL_NO_ERROR)
        return 0u;
    if (v == GL_ZERO || v == GL_NONE)
        return 0u;
    if (v == GL_ONE)
        return 1u;
    for (uint32_t k = 0; k < 10u; k++)
        if (PRIM_IN[k] == v)
            return k;
    uint32_t lo = 0, hi = (uint32_t)NTOK_REV;
    while (lo < hi)
    {
        const uint32_t mid = (lo + hi) >> 1;
        if (TOK5_REV[mid][0] < v)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo < NTOK_REV && TOK5_REV[lo][0] == v ? TOK5_REV[lo][1] : v;
}

/* the same, for the platform's entries (src/mgl_tab5.c) */
uint32_t mgl_abi5_tr(uint32_t v)        { return tr(v); }
uint32_t mgl_abi5_tr_blend(uint32_t v)  { return tr_blend(v); }

static int texpar_is_token(uint32_t pname)      /* glTexParameteri's pnames whose value is a token */
{
    return pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER || pname == GL_TEXTURE_WRAP_S ||
           pname == GL_TEXTURE_WRAP_T;
}

static int pfmode_is_token(uint32_t pname)      /* a float-array parameter whose first element is a token */
{
    return pname == GL_FOG_MODE || pname == GL_TEXTURE_GEN_MODE || pname == GL_TEXTURE_ENV_MODE;
}

/* how many floats a pname-dependent array parameter holds (at most 4) */
static uint32_t cnt_fog(uint32_t p)    { return p == GL_FOG_COLOR ? 4u : 1u; }
static uint32_t cnt_light(uint32_t p)
{
    return p == GL_AMBIENT || p == GL_DIFFUSE || p == GL_SPECULAR || p == GL_POSITION ? 4u :
           p == GL_SPOT_DIRECTION ? 3u : 1u;
}
static uint32_t cnt_mat(uint32_t p)
{
    return p == GL_AMBIENT || p == GL_DIFFUSE || p == GL_SPECULAR || p == GL_EMISSION || p == GL_AMBIENT_AND_DIFFUSE ? 4u :
           1u;                                  /* (the standard's header names no GL_COLOR_INDEXES: RGBA only) */
}
static uint32_t cnt_lm(uint32_t p)     { return p == GL_LIGHT_MODEL_AMBIENT ? 4u : 1u; }
static uint32_t cnt_texgen(uint32_t p) { return p == GL_OBJECT_PLANE || p == GL_EYE_PLANE ? 4u : 1u; }
static uint32_t cnt_texenv(uint32_t p) { return p == GL_TEXTURE_ENV_COLOR ? 4u : 1u; }

static uint32_t fbits(float f)        { uint32_t w; memcpy(&w, &f, 4); return w; }
static float bitsf(uint32_t w)        { float f; memcpy(&f, &w, 4); return f; }
static void put_d(uint32_t *w, double d)  { memcpy(w, &d, 8); }
static double get_d(const uint32_t *w)    { double d; memcpy(&d, w, 8); return d; }

static int dl_grow(GLcontext c, mgl_dlist *l, uint32_t need)
{
    if (l->n + need <= l->cap)
        return 1;
    uint32_t cap = l->cap ? l->cap : 256u;
    while (cap < l->n + need)
        cap *= 2u;
    uint32_t *w = c->sink.alloc ? (uint32_t *)c->sink.alloc(c->sink.ud, cap * 4u) : 0;
    if (!w)
        return 0;
    if (l->w)
    {
        memcpy(w, l->w, l->n * 4u);
        c->sink.free(c->sink.ud, l->w, l->cap * 4u);
    }
    l->w = w;
    l->cap = cap;
    return 1;
}

static uint32_t *dl_put(GLcontext c, uint32_t op, uint32_t n)
{
    mgl_ctx5 *const x5 = X5(c);
    if (x5->dl_oom)
        return 0;
    mgl_dlist *l = &x5->dl_new;
    const uint32_t hdr = n < 0xFFFFu ? 1u : 2u;
    if (x5->m2b && l->n && l->n + hdr + n > MGL_M2B_WORDS)
        mgl_m2b_submit(c, 0);
    if (!dl_grow(c, l, hdr + n))
    {
        x5->dl_oom = 1;
        mgl_seterr(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    uint32_t *w = l->w + l->n;
    if (hdr == 1u)
        w[0] = (op << 16) | n;
    else
    {
        w[0] = (op << 16) | 0xFFFFu;
        w[1] = n;
    }
    l->n += hdr + n;
    return w + hdr;
}

static inline __attribute__((always_inline)) uint32_t *m2b_put(GLcontext c, mgl_ctx5 *x5, uint32_t op, uint32_t n)
{
    mgl_dlist *const l = &x5->dl_new;
    if (__builtin_expect(x5->m2b && l->n + 1u + n <= MGL_M2B_WORDS, 1))
    {
        uint32_t *w = l->w + l->n;
        w[0] = (op << 16) | n;
        l->n += 1u + n;
        return w + 1;
    }
    return dl_put(c, op, n);
}
#define M2B_PUT(c, x5, op, n) m2b_put((c), (x5), (op), (n))
static inline __attribute__((always_inline)) void m2b_tex_q(GLcontext c, mgl_ctx5 *x5, uint32_t name)
{
    if (name && name < MGL_MAX_TEXNAMES)
    {
        const mgl_texobj *t = &c->tex[name];
        if (t->used && t->nlevels && !t->dirty && t->handle && x5->m2b_sent[name] == t->handle)
        {
            if (x5->tc_stamp)
                x5->tc_stamp[name] = ++x5->tc_clock;
            return;
        }
    }
    mgl_m2b_tex(c, name);
}

static void m2b_tex_changed(GLcontext c, mgl_ctx5 *x5)
{
    const uint32_t name = c->bound[c->active_unit];
    if (name && name < MGL_MAX_TEXNAMES)
    {
        const mgl_texobj *t = &c->tex[name];
        if (t->dirty || !t->handle || x5->m2b_sent[name] != t->handle)
            x5->m2b_tpend = 1;
    }
}
#define M2B_TPEND(c, x5) do { if ((x5)->m2b_tpend) mgl_m2b_tpend(c); } while (0)

#include "mgl_abi5_gen.h"

/* the special recorded calls (the generated ops end at OP5_GEN_LAST) */
enum { OP5_CALL = OP5_GEN_LAST + 1, OP5_SETSTATE, OP5_TEXIMAGE, OP5_TEXSUBIMAGE, OP5_COLORTABLE, OP5_TEXUNIT,
       OP5_TEXH, OP5_DELTEX, OP5_MTEXCALL, OP5_M2BARR, OP5_LAST };

extern MGLDispatchTable5 *const mgl_abi5_tables[2];
static uint32_t g_compiling = 0;                /* contexts of this copy compiling a list */

static void dl_patch(int on)
{
    for (uint32_t k = 0; k < 2u; k++)
    {
        MGLDispatchTable5 *t = mgl_abi5_tables[k];
        if (!t)
            continue;
        void **slot = (void **)(void *)&t->GLActiveTextureARB;    /* the first slot (20 bytes in on the 68k) */
#define MGL_PATCH_ONE(i, usual, rec) slot[i] = on ? (void *)rec : (void *)usual;
        MGL_A5_PATCH(MGL_PATCH_ONE)
#undef MGL_PATCH_ONE
    }
}

static void dl_replay_special(GLcontext c, uint32_t op, const uint32_t *w, uint32_t n);
static uint32_t g_m2b = 0;
void (*mgl_m2b_patchfn)(int on) = 0;

int mgl_m2b_begin(GLcontext c, uint32_t handle, int (*submit)(GLcontext, const uint32_t *, uint32_t, uint32_t),
                  int (*query)(GLcontext, uint32_t *))
{
    mgl_ctx5 *const x5 = X5(c);
    if (!handle || !submit || x5->owner != c || x5->m2b || x5->dl_cur || c->in_begin || !c->sink.alloc ||
        !c->sink.free)
        return 0;
    uint32_t *sent = (uint32_t *)c->sink.alloc(c->sink.ud, MGL_MAX_TEXNAMES * 4u);
    if (!sent)
        return 0;
    memset(sent, 0, MGL_MAX_TEXNAMES * 4u);
    x5->dl_new.n = 0;
    if (!dl_grow(c, &x5->dl_new, MGL_M2B_WORDS))
    {
        c->sink.free(c->sink.ud, sent, MGL_MAX_TEXNAMES * 4u);
        return 0;
    }
    x5->m2b_sent = sent;
    x5->m2b_submit = submit;
    x5->m2b_query = query;
    x5->m2b = handle;
    x5->dl_oom = 0;
    x5->dl_cur = MGL_M2B_LIST;
    x5->dl_exec = MGL_DLX_STATE;
    if (!g_compiling++)
        dl_patch(1);
    if (!g_m2b++ && mgl_m2b_patchfn)
        mgl_m2b_patchfn(1);
    return 1;
}

int mgl_m2b_submit(GLcontext c, uint32_t flags)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!x5->m2b)
        return 0;
    mgl_dlist *l = &x5->dl_new;
    if (!l->n && !flags)
        return 1;
    const uint32_t n = l->n;
    l->n = 0;
    x5->m2b_submits++;
    x5->m2b_words += n;
    return x5->m2b_submit(c, l->w, n, flags);
}

void mgl_m2b_sync(GLcontext c)
{
    mgl_m2b_submit(c, MGL_M2B_SYNC);
}

void mgl_m2b_end(GLcontext c)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!x5->m2b)
        return;
    mgl_m2b_submit(c, 0);
    uint32_t q[16];
    if (x5->m2b_query && x5->m2b_query(c, q))
    {
        if (q[0] != GL_NO_ERROR)
            mgl_seterr(c, (GLenum)q[0]);
        memcpy(c->cur_color, &q[1], 16);        /* the attributes only the engine kept (the vertex calls) */
        memcpy(c->cur_normal, &q[5], 12);
        memcpy(c->cur_tex[0], &q[8], 16);
        memcpy(c->cur_tex[1], &q[12], 16);
    }
    x5->m2b_submit(c, 0, 0, MGL_M2B_END);       /* the platform drops the engine's context */
    x5->m2b = 0;
    x5->m2b_tpend = 0;
    x5->dl_cur = 0;
    x5->dl_exec = 0;
    x5->dl_new.n = 0;
    if (g_compiling && !--g_compiling)
        dl_patch(0);
    if (g_m2b && !--g_m2b && mgl_m2b_patchfn)
        mgl_m2b_patchfn(0);
    c->sink.free(c->sink.ud, x5->m2b_sent, MGL_MAX_TEXNAMES * 4u);
    x5->m2b_sent = 0;
    c->rs_valid = 0;                            /* this side draws again: its raster state sent anew */
}

void mgl_m2b_tex(GLcontext c, uint32_t name)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!x5->m2b || name >= MGL_MAX_TEXNAMES)
        return;
    mgl_texobj *const t = &c->tex[name];
    const uint32_t img = name && t->used && t->nlevels;
    uint32_t made = 0;
    if (img && (t->dirty || !t->handle))
    {
        mgl_m2b_submit(c, 0);
        if (t->handle && c->sink.tex_free)
            c->sink.tex_free(c->sink.ud, t->handle);
        t->handle = c->sink.tex_create ? c->sink.tex_create(c->sink.ud, t->fmt, t->nlevels, t->lv) : 0u;
        t->dirty = 0;
        made = 1;
    }
    if (img && x5->tc_stamp)
        x5->tc_stamp[name] = ++x5->tc_clock;
    const uint32_t h = img ? t->handle : 0u;
    if (!name || (!made && x5->m2b_sent[name] == h))
        return;
    uint32_t *w = dl_put(c, OP5_TEXH, 6u);
    if (!w)
        return;
    w[0] = name;
    w[1] = h;
    w[2] = img ? t->lv[0].w : 0u;
    w[3] = img ? t->lv[0].h : 0u;
    w[4] = img ? t->nlevels : 0u;
    w[5] = t->fmt;
    x5->m2b_sent[name] = h;
}

void mgl_m2b_tpend(GLcontext c)
{
    mgl_ctx5 *const x5 = X5(c);
    x5->m2b_tpend = 0;
    if (!x5->m2b)
        return;
    for (uint32_t u = 0; u < MGL_UNITS; u++)
        m2b_tex_q(c, x5, c->bound[u]);
}

/* the engine side: a name's texture is the library's object h */
static void m2b_texh_replay(GLcontext c, const uint32_t *w)
{
    const uint32_t name = w[0];
    if (!name || name >= MGL_MAX_TEXNAMES)
        return;
    mgl_texobj *const t = &c->tex[name];
    if (!t->used)
    {
        t->used = 1;                            /* (as binding an unused name: GL 1.1's defaults) */
        t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
        t->mag_filter = GL_LINEAR;
        t->wrap_s = t->wrap_t = GL_REPEAT;
    }
    for (uint32_t l = 0; l < MGL_MAX_LEVELS; l++)
    {
        t->lv[l].px = 0;                        /* the texels are the library's: none here */
        t->lv[l].w = l == 0 ? w[2] : 0u;
        t->lv[l].h = l == 0 ? w[3] : 0u;
    }
    t->handle = w[1];
    t->nlevels = w[4] < MGL_MAX_LEVELS ? w[4] : MGL_MAX_LEVELS;
    t->fmt = w[5];
    t->dirty = 0;
    c->rs_dirty = 1;                            /* the bound name's object may be another */
}

int mgl_m2b_replay(GLcontext c, const uint32_t *w, uint32_t n, uint32_t flags)
{
    const uint32_t *p = w, *const end = w + n;
    while (p < end)
    {
        const uint32_t op = p[0] >> 16;
        uint32_t k = p[0] & 0xFFFFu;
        p++;
        if (k == 0xFFFFu)
        {
            if (p >= end)
                return 0;
            k = *p++;
        }
        if (k > (uint32_t)(end - p) || (k > 64u && op != OP5_M2BARR) || op == OP5_NONE || op >= OP5_LAST || op == OP5_CALL ||
            op == OP5_TEXIMAGE || op == OP5_TEXSUBIMAGE || op == OP5_COLORTABLE)
            return 0;
        if (!dl_replay_gen(c, op, p))
            dl_replay_special(c, op, p, k);
        p += k;
    }
    if (flags & MGL_M2B_SWAP)
        mgl_mtex_flush(c, MGL_MT_SWAP);
    mgl_core_flush(c);                          /* what the buffer drew is the renderer's before the library goes on */
    return 1;
}

void mgl_m2b_query(GLcontext c, uint32_t out16[16])
{
    out16[0] = (uint32_t)GLGetError(c);
    memcpy(&out16[1], c->cur_color, 16);
    memcpy(&out16[5], c->cur_normal, 12);
    memcpy(&out16[8], c->cur_tex[0], 16);
    memcpy(&out16[12], c->cur_tex[1], 16);
}

void mgl_m2b_GLDeleteTextures(GLcontext c, GLsizei n, const GLuint *names)
{
    mgl_ctx5 *const x5 = X5(c);
    const uint32_t m = x5->m2b && n > 0 && names;
    if (m)
        mgl_m2b_submit(c, 0);
    GLDeleteTextures(c, n, names);
    if (!m)
        return;
    for (GLsizei k = 0; k < n; k++)
        if (names[k] && names[k] < MGL_MAX_TEXNAMES)
        {
            uint32_t *w = dl_put(c, OP5_DELTEX, 1u);
            if (w)
                w[0] = names[k];
            x5->m2b_sent[names[k]] = 0;
        }
}

void mgl_m2b_mtexcall(GLcontext c, GLenum bsrc, GLenum bdst, GLenum env)
{
    M2B_TPEND(c, X5(c));
    uint32_t *w = dl_put(c, OP5_MTEXCALL, 3u);
    if (w)
    {
        w[0] = bsrc;
        w[1] = bdst;
        w[2] = env;
    }
}

void mgl_m2b_GLFlush(GLcontext c)
{
    if (X5(c)->m2b)
        mgl_m2b_submit(c, 0);
    GLFlush(c);
}

void mgl_m2b_GLFinish(GLcontext c)
{
    if (X5(c)->m2b)
        mgl_m2b_submit(c, MGL_M2B_SYNC);
    GLFinish(c);
}

static void v4(float *d, float a, float b, float c2, float e) { d[0] = a; d[1] = b; d[2] = c2; d[3] = e; }

static void x5_defaults(mgl_ctx5 *x5)
{
    x5->pack_align = 4;
    mgl_texgen_defaults(x5);
    for (uint32_t i = 0; i < MGL_MAX_LIGHTS; i++)
    {
        mgl_light *l = &x5->light[i];
        v4(l->amb, 0.0f, 0.0f, 0.0f, 1.0f);
        if (i == 0)
        {
            v4(l->dif, 1.0f, 1.0f, 1.0f, 1.0f);
            v4(l->spe, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        else
        {
            v4(l->dif, 0.0f, 0.0f, 0.0f, 1.0f);
            v4(l->spe, 0.0f, 0.0f, 0.0f, 1.0f);
        }
        v4(l->pos, 0.0f, 0.0f, 1.0f, 0.0f);
        l->dir[0] = 0.0f; l->dir[1] = 0.0f; l->dir[2] = -1.0f;
        l->exp = 0.0f;
        l->cut = 180.0f;
        l->att[0] = 1.0f; l->att[1] = 0.0f; l->att[2] = 0.0f;
    }
    for (uint32_t f = 0; f < 2u; f++)
    {
        mgl_material *m = &x5->mat[f];
        v4(m->amb, 0.2f, 0.2f, 0.2f, 1.0f);
        v4(m->dif, 0.8f, 0.8f, 0.8f, 1.0f);
        v4(m->spe, 0.0f, 0.0f, 0.0f, 1.0f);
        v4(m->emi, 0.0f, 0.0f, 0.0f, 1.0f);
        m->shi = 0.0f;
    }
    v4(x5->lm_ambient, 0.2f, 0.2f, 0.2f, 1.0f);
    x5->cm_face = GL_FRONT_AND_BACK;
    x5->cm_mode = GL_AMBIENT_AND_DIFFUSE;
    /* (GL's current normal, 0 0 1, is the core's: mgl_geom_init) */
}

mgl_ctx5 *mgl_x5_last = 0;
void (*mgl_x5_lockfn)(int on) = 0;
static mgl_ctx5 *g_x5_list = 0;
static mgl_ctx5 g_x5_none = { 0 };              /* no memory for a context's block: this one, shared, never freed */
static uint32_t g_x5_none_ready = 0;

static void x5_lock(int on)
{
    if (mgl_x5_lockfn)
        mgl_x5_lockfn(on);
}

mgl_ctx5 *mgl_x5_find(GLcontext c)
{
    x5_lock(1);
    mgl_ctx5 *x = g_x5_list;
    while (x && x->owner != c)
        x = x->next;
    x5_lock(0);
    if (!x)
    {
        x = c->sink.alloc ? (mgl_ctx5 *)c->sink.alloc(c->sink.ud, (uint32_t)sizeof(mgl_ctx5)) : 0;
        if (!x)
        {
            if (!g_x5_none_ready)
            {
                x5_defaults(&g_x5_none);
                g_x5_none_ready = 1;
            }
            mgl_seterr(c, GL_OUT_OF_MEMORY);
            return &g_x5_none;
        }
        memset(x, 0, sizeof(*x));
        x5_defaults(x);
        x->owner = c;
        x5_lock(1);
        x->next = g_x5_list;
        g_x5_list = x;
        x5_lock(0);
    }
    mgl_x5_last = x;
    return x;
}

mgl_ctx5 *mgl_x5_of_ud(void *ud)
{
    mgl_ctx5 *x = mgl_x5_last;
    if (x && x->owner && x->owner->sink.ud == ud && x->tc_create)
        return x;
    x5_lock(1);
    for (x = g_x5_list; x && !(x->owner && x->owner->sink.ud == ud && x->tc_create); x = x->next)
        ;
    x5_lock(0);
    if (x)
        mgl_x5_last = x;
    return x;
}

static void dl_drop(GLcontext c, mgl_dlist *l)
{
    if (l->w && c->sink.free)
        c->sink.free(c->sink.ud, l->w, l->cap * 4u);
    l->w = 0;
    l->n = l->cap = 0;
}

void mgl_abi5_drop(GLcontext c)
{
    x5_lock(1);
    mgl_ctx5 **pp = &g_x5_list, *x5;
    while ((x5 = *pp) != 0 && x5->owner != c)
        pp = &x5->next;
    if (x5)
    {
        *pp = x5->next;
        if (mgl_x5_last == x5)
            mgl_x5_last = 0;
    }
    x5_lock(0);
    if (!x5)
        return;
    if (x5->m2b)
    {
        mgl_x5_last = x5;
        mgl_m2b_end(c);
        mgl_x5_last = 0;
    }
    if (x5->dl_cur)
    {
        x5->dl_cur = 0;
        if (g_compiling && !--g_compiling)
            dl_patch(0);
    }
    dl_drop(c, &x5->dl_new);
    if (x5->dl)
    {
        for (uint32_t i = 0; i < x5->dl_max; i++)
            dl_drop(c, &x5->dl[i]);
        if (c->sink.free)
            c->sink.free(c->sink.ud, x5->dl, x5->dl_max * (uint32_t)sizeof(mgl_dlist));
    }
    mgl_tc_detach(c, x5);
    mgl_mtex_free(c, x5);
    mgl_ds_free(c, x5);
    if (c->sink.free)
        c->sink.free(c->sink.ud, x5, (uint32_t)sizeof(mgl_ctx5));
}

void mgl_abi5_forget(void)
{
    extern uint32_t mgl_ds_live;
    g_x5_list = 0;
    mgl_x5_last = 0;
    g_compiling = 0;
    g_m2b = 0;
    mgl_ds_live = 0;
}

void mgl_abi5_attach(GLcontext c)
{
    mgl_abi5_drop(c);                           /* a block a context at this address left behind */
    (void)mgl_x5_find(c);
}

static int light_of(GLcontext c, GLenum light)
{
    if (light >= GL_LIGHT0 && light < GL_LIGHT0 + MGL_MAX_LIGHTS)
        return (int)(light - GL_LIGHT0);
    mgl_seterr(c, GL_INVALID_ENUM);
    return -1;
}

static void xform4(const float *m, const float *v, float *o)     /* column-major m x v */
{
    for (uint32_t r = 0; r < 4u; r++)
        o[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * v[3];
}

void GLLightfv(GLcontext c, GLenum light, GLenum pname, const GLfloat *p)
{
    mgl_ctx5 *const x5 = X5(c);
    const int i = light_of(c, light);
    if (i < 0 || !p)
        return;
    x5->lt_ready = 0;                           /* step 2: mgl_light.c's eye-space data made again */
    mgl_light *l = &x5->light[i];
    const float *mv = c->mv[c->mv_sp].m;
    switch (pname)
    {
    case GL_AMBIENT:  memcpy(l->amb, p, 16); break;
    case GL_DIFFUSE:  memcpy(l->dif, p, 16); break;
    case GL_SPECULAR: memcpy(l->spe, p, 16); break;
    case GL_POSITION: xform4(mv, p, l->pos); break;
    case GL_SPOT_DIRECTION:
    {
        const float d[4] = { p[0], p[1], p[2], 0.0f };
        float o[4];
        xform4(mv, d, o);                                         /* the upper 3x3 (w = 0) */
        l->dir[0] = o[0]; l->dir[1] = o[1]; l->dir[2] = o[2];
        break;
    }
    case GL_SPOT_EXPONENT:
        if (p[0] < 0.0f || p[0] > 128.0f) { mgl_seterr(c, GL_INVALID_VALUE); return; }
        l->exp = p[0];
        break;
    case GL_SPOT_CUTOFF:
        if ((p[0] < 0.0f || p[0] > 90.0f) && p[0] != 180.0f) { mgl_seterr(c, GL_INVALID_VALUE); return; }
        l->cut = p[0];
        break;
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        if (p[0] < 0.0f) { mgl_seterr(c, GL_INVALID_VALUE); return; }
        l->att[pname - GL_CONSTANT_ATTENUATION] = p[0];
        break;
    default:
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
}

void GLLightf(GLcontext c, GLenum light, GLenum pname, GLfloat param)
{
    if (cnt_light(pname) != 1u)
    {
        mgl_seterr(c, GL_INVALID_ENUM);                           /* a vector pname through the scalar entry */
        return;
    }
    const GLfloat p[4] = { param, 0.0f, 0.0f, 0.0f };
    GLLightfv(c, light, pname, p);
}

void GLGetLightfv(GLcontext c, GLenum light, GLenum pname, GLfloat *o)
{
    mgl_ctx5 *const x5 = X5(c);
    const int i = light_of(c, light);
    if (i < 0 || !o)
        return;
    const mgl_light *l = &x5->light[i];
    switch (pname)
    {
    case GL_AMBIENT:        memcpy(o, l->amb, 16); break;
    case GL_DIFFUSE:        memcpy(o, l->dif, 16); break;
    case GL_SPECULAR:       memcpy(o, l->spe, 16); break;
    case GL_POSITION:       memcpy(o, l->pos, 16); break;
    case GL_SPOT_DIRECTION: memcpy(o, l->dir, 12); break;
    case GL_SPOT_EXPONENT:  o[0] = l->exp; break;
    case GL_SPOT_CUTOFF:    o[0] = l->cut; break;
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION: o[0] = l->att[pname - GL_CONSTANT_ATTENUATION]; break;
    default:                mgl_seterr(c, GL_INVALID_ENUM); break;
    }
}

static int faces_of(GLcontext c, GLenum face, uint32_t *f0, uint32_t *f1)
{
    if (face == GL_FRONT) { *f0 = 0; *f1 = 0; return 1; }
    if (face == GL_BACK) { *f0 = 1; *f1 = 1; return 1; }
    if (face == GL_FRONT_AND_BACK) { *f0 = 0; *f1 = 1; return 1; }
    mgl_seterr(c, GL_INVALID_ENUM);
    return 0;
}

void GLMaterialfv(GLcontext c, GLenum face, GLenum pname, const GLfloat *p)
{
    mgl_ctx5 *const x5 = X5(c);
    uint32_t f0, f1;
    if (!p || !faces_of(c, face, &f0, &f1))
        return;
    for (uint32_t f = f0; f <= f1; f++)
    {
        mgl_material *m = &x5->mat[f];
        switch (pname)
        {
        case GL_AMBIENT:  memcpy(m->amb, p, 16); break;
        case GL_DIFFUSE:  memcpy(m->dif, p, 16); break;
        case GL_SPECULAR: memcpy(m->spe, p, 16); break;
        case GL_EMISSION: memcpy(m->emi, p, 16); break;
        case GL_AMBIENT_AND_DIFFUSE: memcpy(m->amb, p, 16); memcpy(m->dif, p, 16); break;
        case GL_SHININESS:
            if (p[0] < 0.0f || p[0] > 128.0f) { mgl_seterr(c, GL_INVALID_VALUE); return; }
            m->shi = p[0];
            break;
        default: mgl_seterr(c, GL_INVALID_ENUM); return;
        }
    }
}

void GLMaterialf(GLcontext c, GLenum face, GLenum pname, GLfloat param)
{
    if (pname != GL_SHININESS)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    const GLfloat p[4] = { param, 0.0f, 0.0f, 0.0f };
    GLMaterialfv(c, face, pname, p);
}

void GLGetMaterialfv(GLcontext c, GLenum face, GLenum pname, GLfloat *o)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!o)
        return;
    if (face != GL_FRONT && face != GL_BACK)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    const mgl_material *m = &x5->mat[face == GL_BACK ? 1 : 0];
    switch (pname)
    {
    case GL_AMBIENT:   memcpy(o, m->amb, 16); break;
    case GL_DIFFUSE:   memcpy(o, m->dif, 16); break;
    case GL_SPECULAR:  memcpy(o, m->spe, 16); break;
    case GL_EMISSION:  memcpy(o, m->emi, 16); break;
    case GL_SHININESS: o[0] = m->shi; break;
    default:           mgl_seterr(c, GL_INVALID_ENUM); break;
    }
}

void GLLightModelfv(GLcontext c, GLenum pname, const GLfloat *p)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!p)
        return;
    switch (pname)
    {
    case GL_LIGHT_MODEL_AMBIENT:      memcpy(x5->lm_ambient, p, 16); break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: x5->lm_local_viewer = p[0] != 0.0f; break;
    case GL_LIGHT_MODEL_TWO_SIDE:     x5->lm_two_side = p[0] != 0.0f; break;
    default:                          mgl_seterr(c, GL_INVALID_ENUM); break;
    }
}

void GLLightModelf(GLcontext c, GLenum pname, GLfloat param)
{
    if (pname == GL_LIGHT_MODEL_AMBIENT)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    const GLfloat p[4] = { param, 0.0f, 0.0f, 0.0f };
    GLLightModelfv(c, pname, p);
}

void GLColorMaterial(GLcontext c, GLenum face, GLenum mode)
{
    mgl_ctx5 *const x5 = X5(c);
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (mode != GL_EMISSION && mode != GL_AMBIENT && mode != GL_DIFFUSE && mode != GL_SPECULAR &&
        mode != GL_AMBIENT_AND_DIFFUSE)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    x5->cm_face = face;
    x5->cm_mode = mode;
}

void GLTexEnvfv(GLcontext c, GLenum target, GLenum pname, const GLfloat *p)
{
    if (!p)
        return;
    if (target != GL_TEXTURE_ENV)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_TEXTURE_ENV_MODE)
    {
        GLTexEnvi(c, target, pname, (GLint)p[0]);
        return;
    }
    if (pname != GL_TEXTURE_ENV_COLOR)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    float *e = c->envcolor[c->active_unit];
    for (uint32_t k = 0; k < 4u; k++)
    {
        const float v = p[k] < 0.0f ? 0.0f : p[k] > 1.0f ? 1.0f : p[k];
        if (e[k] != v)
        {
            e[k] = v;
            c->rs_dirty = 1;                    /* the raster state carries it (mgl_raster.envcolor) */
        }
    }
}

void GLNormal3fv(GLcontext c, GLfloat *v)       { GLNormal3f(c, v[0], v[1], v[2]); }
void GLNormal3fv_a5(GLcontext c, GLfloat *v)    { if (v) GLNormal3f(c, v[0], v[1], v[2]); }

static mgl_dlist *dl_slot(GLcontext c, GLuint name, int create)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!name)
        return 0;
    if (name < x5->dl_max)
        return &x5->dl[name];
    if (!create || !c->sink.alloc)
        return 0;
    uint32_t n = x5->dl_max ? x5->dl_max : 64u;
    while (n <= name)
        n *= 2u;
    if (n > 65536u)
        return 0;
    mgl_dlist *d = (mgl_dlist *)c->sink.alloc(c->sink.ud, n * (uint32_t)sizeof(mgl_dlist));
    if (!d)
        return 0;
    memset(d, 0, n * sizeof(mgl_dlist));
    if (x5->dl)
    {
        memcpy(d, x5->dl, x5->dl_max * sizeof(mgl_dlist));
        c->sink.free(c->sink.ud, x5->dl, x5->dl_max * (uint32_t)sizeof(mgl_dlist));
    }
    x5->dl = d;
    x5->dl_max = n;
    return &x5->dl[name];
}

GLuint a5_GLGenLists(GLcontext c, GLsizei range)
{
    mgl_ctx5 *const x5 = X5(c);
    if (range < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return 0;
    }
    if (!range)
        return 0;
    for (GLuint first = 1; first + (GLuint)range <= 65536u; first++)
    {
        GLsizei k = 0;
        for (; k < range; k++)
        {
            const mgl_dlist *l = dl_slot(c, first + (GLuint)k, 0);
            if ((l && l->used) || first + (GLuint)k == x5->dl_cur)
                break;
        }
        if (k == range)
        {
            if (!dl_slot(c, first + (GLuint)range - 1u, 1))
            {
                mgl_seterr(c, GL_OUT_OF_MEMORY);
                return 0;
            }
            for (k = 0; k < range; k++)
                x5->dl[first + (GLuint)k].used = 1;
            return first;
        }
        first += (GLuint)k;                     /* skip past the name in use */
    }
    mgl_seterr(c, GL_OUT_OF_MEMORY);
    return 0;
}

void a5_GLDeleteLists(GLcontext c, GLuint list, GLsizei range)
{
    if (range < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei k = 0; k < range; k++)
    {
        mgl_dlist *l = dl_slot(c, list + (GLuint)k, 0);
        if (l)
        {
            dl_drop(c, l);
            l->used = 0;
        }
    }
}

GLboolean a5_GLIsList(GLcontext c, GLuint list)
{
    const mgl_dlist *l = dl_slot(c, list, 0);
    return l && l->used ? GL_TRUE : GL_FALSE;
}

void a5_GLNewList(GLcontext c, GLuint list, GLenum mode)
{
    mgl_ctx5 *const x5 = X5(c);
    if (x5->m2b)
        mgl_m2b_end(c);
    mode = (GLenum)tr((uint32_t)mode);
    if (!list)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (x5->dl_cur || c->in_begin)
    {
        mgl_seterr(c, GL_INVALID_OPERATION);
        return;
    }
    x5->dl_new.n = 0;
    x5->dl_oom = 0;
    x5->dl_cur = list;
    x5->dl_exec = mode == GL_COMPILE_AND_EXECUTE ? MGL_DLX_VERTEX | MGL_DLX_STATE : 0u;
    if (!g_compiling++)
        dl_patch(1);
}

void a5_GLEndList(GLcontext c)
{
    mgl_ctx5 *const x5 = X5(c);
    if (!x5->dl_cur)
    {
        mgl_seterr(c, GL_INVALID_OPERATION);
        return;
    }
    const GLuint name = x5->dl_cur;
    x5->dl_cur = 0;
    if (g_compiling && !--g_compiling)
        dl_patch(0);
    mgl_dlist *l = dl_slot(c, name, 1);
    if (!l)
    {
        mgl_seterr(c, GL_OUT_OF_MEMORY);
        x5->dl_new.n = 0;
        return;
    }
    dl_drop(c, l);                              /* GL: the new list replaces the name's old one */
    if (x5->dl_oom)
        x5->dl_new.n = 0;                        /* out of memory while compiling: the list ends empty */
    *l = x5->dl_new;
    l->used = 1;
    memset(&x5->dl_new, 0, sizeof(x5->dl_new));
}

static void mgl_setstate5(GLcontext c, GLenum cap, GLboolean state);
static void tex_image_replay(GLcontext c, const uint32_t *w, int sub);

static void dl_replay_special(GLcontext c, uint32_t op, const uint32_t *w, uint32_t n);
static void m2b_texh_replay(GLcontext c, const uint32_t *w);
static void m2b_arrays_replay(GLcontext c, const uint32_t *w, uint32_t n);

static void dl_call(GLcontext c, GLuint list)
{
    mgl_ctx5 *const x5 = X5(c);
    const mgl_dlist *l = dl_slot(c, list, 0);
    if (!l || !l->used || !l->w || x5->dl_depth >= 64u)          /* GL_MAX_LIST_NESTING */
        return;
    x5->dl_depth++;
    const uint32_t *p = l->w, *end = l->w + l->n;
    while (p < end)
    {
        const uint32_t op = p[0] >> 16;
        uint32_t n = p[0] & 0xFFFFu;
        p++;
        if (n == 0xFFFFu)
            n = *p++;
        if (!dl_replay_gen(c, op, p))
            dl_replay_special(c, op, p, n);
        p += n;
    }
    x5->dl_depth--;
}

void a5_GLCallList(GLcontext c, GLuint list)
{
    mgl_ctx5 *const x5 = X5(c);
    if (x5->m2b)
        mgl_m2b_end(c);
    if (x5->dl_cur)
    {
        uint32_t *w = dl_put(c, OP5_CALL, 1u);
        if (w)
            w[0] = list;
        if (!x5->dl_exec)
            return;
    }
    dl_call(c, list);
}

static void dl_replay_special(GLcontext c, uint32_t op, const uint32_t *w, uint32_t n)
{
    (void)n;
    switch (op)
    {
    case OP5_CALL:        dl_call(c, w[0]); break;
    case OP5_SETSTATE:    mgl_setstate5(c, (GLenum)w[0], (GLboolean)w[1]); break;
    case OP5_TEXIMAGE:    tex_image_replay(c, w, 0); break;
    case OP5_TEXSUBIMAGE: tex_image_replay(c, w, 1); break;
    case OP5_COLORTABLE:  GLColorTable(c, (GLenum)w[0], (GLenum)w[1], (GLint)w[2], (GLenum)w[3], (GLenum)w[4], (GLvoid *)(void *)&w[5]); break;
    case OP5_TEXUNIT:     GLMultiTexCoord2fARB(c, (GLenum)w[0], bitsf(w[1]), bitsf(w[2])); break;
    case OP5_TEXH:        m2b_texh_replay(c, w); break;
    case OP5_DELTEX:      GLDeleteTextures(c, 1, (const GLuint *)(const void *)&w[0]); break;
    case OP5_MTEXCALL:    mgl_mtex_call(c, (GLenum)w[0], (GLenum)w[1], (GLenum)w[2]); break;
    case OP5_M2BARR:      m2b_arrays_replay(c, w, n); break;
    default: break;
    }
}

static uint32_t *cap5(GLcontext c, GLenum cap)  /* the final standard's own capabilities */
{
    mgl_ctx5 *const x5 = X5(c);
    if (cap == GL_LIGHTING)       return &x5->en_lighting;
    if (cap == GL_COLOR_MATERIAL) return &x5->en_color_material;
    if (cap == GL_NORMALIZE)      return &x5->en_normalize;
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + MGL_MAX_LIGHTS)
        return &x5->en_light[cap - GL_LIGHT0];
    if (cap == GL_TEXTURE_GEN_S)  return &x5->tg_en[0];
    if (cap == GL_TEXTURE_GEN_T)  return &x5->tg_en[1];
    if (cap == GL_TEXTURE_GEN_R)  return &x5->tg_en_rq[0];
    if (cap == GL_TEXTURE_GEN_Q)  return &x5->tg_en_rq[1];
    return 0;
}

static void color_material_now(GLcontext c)
{
    mgl_ctx5 *const x5 = X5(c);
    for (uint32_t f = 0; f < 2u; f++)
    {
        if ((f == 0 && x5->cm_face == GL_BACK) || (f == 1 && x5->cm_face == GL_FRONT))
            continue;
        mgl_material *m = &x5->mat[f];
        const float *cc = c->cur_color;
        switch (x5->cm_mode)
        {
        case GL_EMISSION: memcpy(m->emi, cc, 16); break;
        case GL_AMBIENT:  memcpy(m->amb, cc, 16); break;
        case GL_DIFFUSE:  memcpy(m->dif, cc, 16); break;
        case GL_SPECULAR: memcpy(m->spe, cc, 16); break;
        default:          memcpy(m->amb, cc, 16); memcpy(m->dif, cc, 16); break;
        }
    }
}

static void mgl_setstate5(GLcontext c, GLenum cap, GLboolean state)
{
    uint32_t *p = cap5(c, cap);
    if (p)
    {
        *p = state ? 1u : 0u;
        if (cap == GL_COLOR_MATERIAL && state)
            color_material_now(c);
        X5(c)->tg_on = X5(c)->tg_en[0] | X5(c)->tg_en[1];
        return;
    }
    if (cap == GL_LINE_SMOOTH || cap == GL_POLYGON_SMOOTH || cap == GL_LINE_STIPPLE || cap == GL_POLYGON_STIPPLE ||
        cap == GL_COLOR_LOGIC_OP || cap == GL_STENCIL_TEST || cap == GL_AUTO_NORMAL ||
        (cap >= GL_CLIP_PLANE0 && cap <= GL_CLIP_PLANE5) || cap == GL_TEXTURE_1D)
        return;
    if (cap == GL_TEXTURE_2D && c->active_unit == 1u)
    {
        const uint32_t d = c->rs_dirty;
        MGLSetState(c, cap, state);
        c->rs_dirty = d;
        return;
    }
    MGLSetState(c, cap, state);
}

void a5_MGLSetState(GLcontext c, GLenum cap, GLboolean state)    /* glEnable / glDisable */
{
    mgl_ctx5 *const x5 = X5(c);
    if (__builtin_expect(!x5->dl_cur || x5->m2b, 1))
    {
        uint32_t *p, geom = 0;
        switch (cap)
        {
        case GL5_TEXTURE_2D:   p = &c->en_tex2d[c->active_unit]; break;
        case GL5_BLEND:        p = &c->en_blend; break;
        case GL5_DEPTH_TEST:   p = &c->en_depth; break;
        case GL5_ALPHA_TEST:   p = &c->en_alpha; break;
        case GL5_CULL_FACE:    p = &c->en_cull; geom = 1; break;
        case GL5_FOG:          p = &c->en_fog; break;
        case GL5_SCISSOR_TEST: p = &c->en_scissor; break;
        case GL5_DITHER:       p = &c->en_dither; break;
        default:               p = 0; break;
        }
        if (p)
        {
            if (x5->m2b)
            {
                uint32_t *w = m2b_put(c, x5, OP5_SETSTATE, 2u);
                if (w)
                {
                    w[0] = (uint32_t)tr((uint32_t)cap);
                    w[1] = state ? 1u : 0u;
                }
            }
            else if (c->in_begin)
                mgl_seterr(c, GL_INVALID_OPERATION);
            const uint32_t v = state ? 1u : 0u;
            if (*p != v)
            {
                *p = v;
                if (geom)
                    c->geom_dirty = 1;
                else if (!(cap == GL5_TEXTURE_2D && c->active_unit == 1u))
                    c->rs_dirty = 1;
            }
            return;
        }
    }
    cap = (GLenum)tr((uint32_t)cap);
    if (x5->dl_cur)
    {
        uint32_t *w = dl_put(c, OP5_SETSTATE, 2u);
        if (w)
        {
            w[0] = cap;
            w[1] = state ? 1u : 0u;
        }
        if (!(x5->dl_exec & MGL_DLX_STATE))
            return;
    }
    mgl_setstate5(c, cap, state);
}

GLboolean a5_GLIsEnabled(GLcontext c, GLenum cap)
{
    mgl_ctx5 *const x5 = X5(c);
    cap = (GLenum)tr((uint32_t)cap);
    const uint32_t *p = cap5(c, cap);
    if (p)
        return *p ? GL_TRUE : GL_FALSE;
    if (cap == GL_NORMAL_ARRAY)
        return x5->va_normal.enabled ? GL_TRUE : GL_FALSE;
    return GLIsEnabled(c, cap);
}

void a5_GLEnableClientState(GLcontext c, GLenum cap)
{
    mgl_ctx5 *const x5 = X5(c);
    cap = (GLenum)tr((uint32_t)cap);
    if (cap == GL_NORMAL_ARRAY)
        x5->va_normal.enabled = 1;
    else
        GLEnableClientState(c, cap);
}

void a5_GLDisableClientState(GLcontext c, GLenum cap)
{
    mgl_ctx5 *const x5 = X5(c);
    cap = (GLenum)tr((uint32_t)cap);
    if (cap == GL_NORMAL_ARRAY)
        x5->va_normal.enabled = 0;
    else
        GLDisableClientState(c, cap);
}

void a5_GLNormalPointer(GLcontext c, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    mgl_ctx5 *const x5 = X5(c);
    type = (GLenum)tr((uint32_t)type);
    if (stride < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    if (type != GL_BYTE && type != GL_SHORT && type != GL_INT && type != GL_FLOAT && type != GL_DOUBLE)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    x5->va_normal.size = 3;
    x5->va_normal.type = type;
    x5->va_normal.stride = stride;
    x5->va_normal.ptr = (const uint8_t *)pointer;
}

void a5_GLInterleavedArrays(GLcontext c, GLenum format, GLsizei stride, const GLvoid *pointer)
{
    mgl_ctx5 *const x5 = X5(c);
    format = (GLenum)tr((uint32_t)format);
    const uint8_t *p = (const uint8_t *)pointer;
    int tc = 0, cs = 0, ct = 0, nv = 0, vs = 3, ns = 0;          /* sizes; ct: 1 = ubyte, 2 = float */
    switch (format)
    {
    case GL_V2F:            vs = 2; break;
    case GL_V3F:            break;
    case GL_C4UB_V2F:       cs = 4; ct = 1; vs = 2; break;
    case GL_C4UB_V3F:       cs = 4; ct = 1; break;
    case GL_C3F_V3F:        cs = 3; ct = 2; break;
    case GL_T2F_V3F:        tc = 2; break;
    case GL_T2F_C4UB_V3F:   tc = 2; cs = 4; ct = 1; break;
    case GL_T2F_C3F_V3F:    tc = 2; cs = 3; ct = 2; break;
    default:                mgl_seterr(c, GL_INVALID_ENUM); return;
    }
    (void)nv; (void)ns;
    const int csz = ct == 1 ? 4 : ct == 2 ? cs * 4 : 0;
    const int whole = tc * 4 + csz + vs * 4;
    const GLsizei st = stride ? stride : whole;
    GLDisableClientState(c, GL_TEXTURE_COORD_ARRAY);
    GLDisableClientState(c, GL_COLOR_ARRAY);
    x5->va_normal.enabled = 0;
    if (tc)
    {
        GLTexCoordPointer(c, tc, GL_FLOAT, st, p);
        GLEnableClientState(c, GL_TEXTURE_COORD_ARRAY);
    }
    if (cs)
    {
        GLColorPointer(c, cs, ct == 1 ? GL_UNSIGNED_BYTE : GL_FLOAT, st, p + tc * 4);
        GLEnableClientState(c, GL_COLOR_ARRAY);
    }
    GLVertexPointer(c, vs, GL_FLOAT, st, p + tc * 4 + csz);
    GLEnableClientState(c, GL_VERTEX_ARRAY);
}

static int normal_at(GLcontext c, GLint i, float *n)
{
    const mgl_array *a = &X5(c)->va_normal;
    if (!a->enabled || !a->ptr || i < 0)
        return 0;
    const uint32_t ts = a->type == GL_BYTE ? 1u : a->type == GL_SHORT ? 2u : a->type == GL_DOUBLE ? 8u : 4u;
    const uint8_t *p = a->ptr + (a->stride ? (uint32_t)a->stride : 3u * ts) * (uint32_t)i;
    for (uint32_t k = 0; k < 3u; k++)
    {
        float v;
        switch (a->type)
        {
        case GL_BYTE:   v = (2.0f * (float)(int8_t)p[k] + 1.0f) / 255.0f; break;
        case GL_SHORT:  { int16_t s; memcpy(&s, p + 2u * k, 2); v = (2.0f * (float)s + 1.0f) / 65535.0f; break; }
        case GL_INT:    { int32_t s; memcpy(&s, p + 4u * k, 4); v = (float)((2.0 * (double)s + 1.0) / 4294967295.0); break; }
        case GL_DOUBLE: { double d; memcpy(&d, p + 8u * k, 8); v = (float)d; break; }
        default:        memcpy(&v, p + 4u * k, 4); break;
        }
        n[k] = v;
    }
    return 1;
}

static void lit_element(GLcontext c, GLint i)
{
    mgl_afetched f;
    mgl_array_fetch(c, i, &f);
    if (f.has_col)
        GLColor4f(c, f.col[0], f.col[1], f.col[2], f.col[3]);
    if (f.tex_mask & 1u)
        GLTexCoord4f(c, f.tex[0][0], f.tex[0][1], f.tex[0][2], f.tex[0][3]);
    for (uint32_t u = 1; u < MGL_UNITS; u++)
        if (f.tex_mask & (1u << u))
            GLMultiTexCoord2fARB(c, GL_TEXTURE0_ARB + u, f.tex[u][0], f.tex[u][1]);
    float n[3];
    if (normal_at(c, i, n))
        GLNormal3f(c, n[0], n[1], n[2]);
    if (f.has_vtx)
        GLVertex4f_a5(c, f.vtx[0], f.vtx[1], f.vtx[2], f.vtx[3]);
}
static void dl_element(GLcontext c, GLint i)    /* one element's calls into the list being compiled */
{
    mgl_ctx5 *const x5 = X5(c);
    mgl_afetched f;
    mgl_array_fetch(c, i, &f);
    uint32_t *w;
    if (f.has_col && (w = M2B_PUT(c, x5, OP5_GLColor4f, 4u)))
        for (uint32_t k = 0; k < 4u; k++)
            w[k] = fbits(f.col[k]);
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        if (!(f.tex_mask & (1u << u)))
            continue;
        if (u == 0)
        {
            if ((w = M2B_PUT(c, x5, OP5_GLTexCoord4f, 4u)))
                for (uint32_t k = 0; k < 4u; k++)
                    w[k] = fbits(f.tex[0][k]);
        }
        else if ((w = M2B_PUT(c, x5, OP5_TEXUNIT, 3u)))
        {
            w[0] = GL_TEXTURE0_ARB + u;
            w[1] = fbits(f.tex[u][0]);
            w[2] = fbits(f.tex[u][1]);
        }
    }
    float nrm[3];
    if (normal_at(c, i, nrm) && (w = M2B_PUT(c, x5, OP5_GLNormal3f, 3u)))
        for (uint32_t k = 0; k < 3u; k++)
            w[k] = fbits(nrm[k]);
    if (f.has_vtx && (w = M2B_PUT(c, x5, OP5_GLVertex4f, 4u)))
        for (uint32_t k = 0; k < 4u; k++)
            w[k] = fbits(f.vtx[k]);
}

static int dl_begin_rec(GLcontext c, GLenum prim)  /* 1 = recorded (an array draw is compiled as Begin / calls / End) */
{
    M2B_TPEND(c, X5(c));
    uint32_t *w = dl_put(c, OP5_GLBegin, 1u);
    if (w)
        w[0] = prim;
    return w != 0;
}

void a5_GLArrayElement(GLcontext c, GLint i)
{
    mgl_ctx5 *const x5 = X5(c);
    if (x5->dl_cur)
    {
        dl_element(c, i);
        if (!(x5->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    if (x5->en_lighting | x5->tg_on)
    {
        lit_element(c, i);                      /* (unit 1: the vertex entry keeps it; texgen there too) */
        return;
    }
    if ((x5->mt_live | c->en_tex2d[1]) && c->in_begin && c->imm_total == 0u)
        mgl_mtex_prim(c, mgl_mtex_unit1(c));
    GLArrayElement(c, i);
    if (x5->mt_rec)
        mgl_mtex_element(c, i);
}

static int m2b_arrays_rec(GLcontext c, mgl_ctx5 *x5, GLenum mode, GLint first, GLsizei count, GLenum itype,
                          const GLvoid *idx);
static void draw_arrays_x(GLcontext c, mgl_ctx5 *x5, GLenum mode, GLint first, GLsizei count);

void a5_GLDrawArrays(GLcontext c, GLenum mode, GLint first, GLsizei count)
{
    mgl_ctx5 *const x5 = X5(c);
    mode = (GLenum)tr_prim((uint32_t)mode);
    if (x5->dl_cur && count > 0 && first >= 0)
    {
        if (x5->dl_cur == MGL_M2B_LIST && m2b_arrays_rec(c, x5, mode, first, count, 0, 0))
            return;
        if (dl_begin_rec(c, mode))
        {
            for (GLsizei k = 0; k < count; k++)
                dl_element(c, first + k);
            dl_put(c, OP5_GLEnd, 0u);
        }
        if (!(x5->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    draw_arrays_x(c, x5, mode, first, count);
}

static void draw_arrays_x(GLcontext c, mgl_ctx5 *x5, GLenum mode, GLint first, GLsizei count)
{
    if ((x5->en_lighting | x5->tg_on) && count > 0 && first >= 0)   /* lit / texgen: element by element */
    {
        GLBegin(c, mode);
        for (GLsizei k = 0; k < count; k++)
            lit_element(c, first + k);          /* (unit 1: the vertex entry keeps them) */
        GLEnd(c);
        return;
    }
    if (x5->mt_live | c->en_tex2d[1])
        mgl_mtex_arrays(c, mode, first, count, 0, 0);
    GLDrawArrays(c, mode, first, count);
}

static GLint index_at(const GLvoid *idx, GLenum type, GLsizei k)
{
    if (type == GL_UNSIGNED_BYTE)
        return ((const uint8_t *)idx)[k];
    if (type == GL_UNSIGNED_SHORT)
    {
        uint16_t s;
        memcpy(&s, (const uint8_t *)idx + 2 * k, 2);
        return s;
    }
    uint32_t v;
    memcpy(&v, (const uint8_t *)idx + 4 * k, 4);
    return (GLint)v;
}

void a5_GLDrawElements(GLcontext c, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    mgl_ctx5 *const x5 = X5(c);
    mode = (GLenum)tr_prim((uint32_t)mode);
    type = (GLenum)tr((uint32_t)type);
    if (x5->dl_cur && count > 0 && indices &&
        (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT))
    {
        if (x5->dl_cur == MGL_M2B_LIST && m2b_arrays_rec(c, x5, mode, 0, count, type, indices))
            return;
        if (dl_begin_rec(c, mode))
        {
            for (GLsizei k = 0; k < count; k++)
                dl_element(c, index_at(indices, type, k));
            dl_put(c, OP5_GLEnd, 0u);
        }
        if (!(x5->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    if ((x5->en_lighting | x5->tg_on) && count > 0 && indices &&
        (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT))
    {
        GLBegin(c, mode);
        for (GLsizei k = 0; k < count; k++)
            lit_element(c, index_at(indices, type, k));
        GLEnd(c);
        return;
    }
    if ((x5->mt_live | c->en_tex2d[1]) && indices)
        mgl_mtex_arrays(c, mode, 0, count, type, indices);
    GLDrawElements(c, mode, count, type, indices);
}

void a5_GLMultiDrawArrays(GLcontext c, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    if (!first || !count)
        return;
    for (GLsizei k = 0; k < primcount; k++)
        a5_GLDrawArrays(c, mode, first[k], count[k]);
}

#define M2BA_VS(l)  ((l) & 7u)                  /* vertex: 2..4 floats                                         */
#define M2BA_CS(l)  (((l) >> 3) & 7u)           /* colour: 0, 3 or 4 floats, or 4 with M2BA_CUB (one word)       */
#define M2BA_CUB    (1u << 6)                   /* (one word: the 3 or 4 bytes as they are in memory)            */
#define M2BA_TS(l, u) (((l) >> (7u + 3u * (u))) & 7u)   /* unit u's texture coordinates: 0..4 floats            */
#define M2BA_N      (1u << 13)                  /* the normal: 3 floats                                          */
#define M2BA_CT(l)  (((l) >> 14) & 3u)          /* a byte colour's type: GL_UNSIGNED_BYTE, MGL_UBYTE_ARGB, _BGRA  */
typedef uint32_t m2ba_u32u __attribute__((aligned(1), may_alias));
static inline uint32_t m2ba_rd(const uint8_t *p) { return *(const m2ba_u32u *)(const void *)p; }
static inline uint32_t m2ba_bytecol(GLenum t) { return t == GL_UNSIGNED_BYTE || t == MGL_UBYTE_ARGB || t == MGL_UBYTE_BGRA; }
static inline uint32_t m2ba_es(const mgl_array *a)   /* (the element's bytes: a byte colour 1 a component) */
{
    return a->stride ? (uint32_t)a->stride : (m2ba_bytecol(a->type) ? 1u : 4u) * (uint32_t)a->size;
}
static inline uint32_t m2ba_rd3(const uint8_t *p) { uint32_t x = 0; memcpy(&x, p, 3); return x; }
static inline int m2b_arrfb(mgl_ctx5 *x5, uint32_t why) { x5->m2b_arrfb++; x5->m2b_arrwhy |= why; return 0; }

typedef struct m2ba_plan
{
    const mgl_array *ar[5];                     /* vertex, colour, unit 0, unit 1, normal                         */
    uint32_t nw[5], es[5], off[5];              /* words a vertex, bytes an element, offset in the op's vertex    */
    uint32_t lay, wpv, raw, stride;             /* raw: one struct span copied whole (stride bytes apart)         */
    const uint8_t *base;
} m2ba_plan;

static int m2ba_plan_make(GLcontext c, mgl_ctx5 *x5, m2ba_plan *P)    /* 0 = a layout the op does not take */
{
    const mgl_array *const *const ar = P->ar;
    P->ar[0] = &c->va_vertex; P->ar[1] = &c->va_color; P->ar[2] = &c->va_tex[0]; P->ar[3] = &c->va_tex[1];
    P->ar[4] = &x5->va_normal;
    uint32_t *const nw = P->nw, *const off = P->off;
    uint32_t lay = 0;
    for (uint32_t a = 0; a < 5u; a++)
        nw[a] = off[a] = P->es[a] = 0;
    const mgl_array *const va = ar[0];
    if (!va->enabled || !va->ptr || va->type != GL_FLOAT || va->size < 2 || va->size > 4)
        return m2b_arrfb(x5, 1u);
    lay = (uint32_t)va->size;
    nw[0] = (uint32_t)va->size;
    const mgl_array *const ca = ar[1];
    if (ca->enabled && ca->ptr)
    {
        if (ca->size != 3 && ca->size != 4)
            return m2b_arrfb(x5, 2u);
        if (ca->type == GL_FLOAT)
            nw[1] = (uint32_t)ca->size;
        else if (m2ba_bytecol(ca->type))
        {
            nw[1] = 1u;
            lay |= M2BA_CUB | (ca->type == MGL_UBYTE_ARGB ? 1u << 14 : ca->type == MGL_UBYTE_BGRA ? 2u << 14 : 0u);
        }
        else
            return m2b_arrfb(x5, 2u);
        lay |= (uint32_t)ca->size << 3;
    }
    for (uint32_t u = 0; u < MGL_UNITS; u++)
    {
        const mgl_array *const ta = ar[2u + u];
        if (!ta->enabled || !ta->ptr)
            continue;
        if (ta->type != GL_FLOAT || ta->size < 1 || ta->size > 4)
            return m2b_arrfb(x5, 4u);
        nw[2u + u] = (uint32_t)ta->size;
        lay |= (uint32_t)ta->size << (7u + 3u * u);
    }
    if (ar[4]->enabled && ar[4]->ptr)
    {
        if (ar[4]->type != GL_FLOAT)
            return m2b_arrfb(x5, 8u);
        nw[4] = 3u;
        lay |= M2BA_N;
    }
    const uint8_t *base = 0, *top = 0;
    uint32_t stride = 0, raw = 1, wpv = 0;
    for (uint32_t a = 0; a < 5u && raw; a++)
    {
        if (!nw[a])
            continue;
        const uint32_t es = m2ba_es(ar[a]);
        if (!ar[a]->stride || (stride && es != stride))
            raw = 0;
        stride = es;
        if (!base || ar[a]->ptr < base)
            base = ar[a]->ptr;
        if (ar[a]->ptr + 4u * nw[a] > top)
            top = ar[a]->ptr + 4u * nw[a];
    }
    if (raw && (uint32_t)(top - base) <= stride && !(stride & 3u) && (uint32_t)(top - base) <= 4u * 31u)
    {
        for (uint32_t a = 0; a < 5u; a++)
            if (nw[a])
            {
                if ((uint32_t)(ar[a]->ptr - base) & 3u)
                    raw = 0;
                off[a] = (uint32_t)(ar[a]->ptr - base) >> 2;
            }
        wpv = (uint32_t)(top - base) >> 2;
    }
    else
        raw = 0;
    if (!raw)
    {
        wpv = 0;
        for (uint32_t a = 0; a < 5u; a++)
        {
            off[a] = wpv;
            wpv += nw[a];
            P->es[a] = nw[a] ? m2ba_es(ar[a]) : 0u;
        }
        if (wpv > 31u)
            return m2b_arrfb(x5, 16u);
    }
    P->lay = lay;
    P->wpv = wpv;
    P->raw = raw;
    P->stride = stride;
    P->base = base;
    return 1;
}

static inline __attribute__((always_inline)) uint32_t *m2ba_vertex(const m2ba_plan *P, uint32_t *o, uint32_t i)
{
    if (P->raw)
    {
        const m2ba_u32u *p = (const m2ba_u32u *)(const void *)(P->base + P->stride * i);
        for (uint32_t j = 0; j < P->wpv; j++)
            *o++ = p[j];
        return o;
    }
    for (uint32_t a = 0; a < 5u; a++)
    {
        if (!P->nw[a])
            continue;
        const uint8_t *p = P->ar[a]->ptr + P->es[a] * i;
        if (a == 1u && (P->lay & M2BA_CUB))
            *o++ = P->ar[1]->size == 3 ? m2ba_rd3(p) : m2ba_rd(p);  /* (the bytes as they are in memory) */
        else
            for (uint32_t j = 0; j < P->nw[a]; j++)
                *o++ = m2ba_rd(p + 4u * j);
    }
    return o;
}

static int m2ba_op(GLcontext c, mgl_ctx5 *x5, const m2ba_plan *P, GLenum mode, GLint first, GLenum itype,
                   const GLvoid *idx, uint32_t k0, uint32_t n, uint32_t pre)
{
    const uint32_t nv = n + pre, wpv = P->wpv;
    uint32_t *w = dl_put(c, OP5_M2BARR, 5u + nv * wpv);
    if (!w)
        return 0;
    x5->m2b_arrops++;
    w[0] = mode;
    w[1] = nv;
    w[2] = P->lay;
    w[3] = wpv;
    w[4] = P->off[0] | P->off[1] << 5 | P->off[2] << 10 | P->off[3] << 15 | P->off[4] << 20;
    uint32_t *o = w + 5;
    if (pre)
        o = m2ba_vertex(P, o, (uint32_t)(idx ? index_at(idx, itype, 0) : first));
    if (!idx && P->raw && P->stride == 4u * wpv)
    {
        const m2ba_u32u *p = (const m2ba_u32u *)(const void *)(P->base + P->stride * ((uint32_t)first + k0));
        uint32_t m = n * wpv;
        for (; m >= 4u; m -= 4u, o += 4, p += 4)
        {
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = p[3];
        }
        for (; m; m--)
            *o++ = *p++;
        return 1;
    }
    for (uint32_t k = k0; k < k0 + n; k++)
        o = m2ba_vertex(P, o, (uint32_t)(idx ? index_at(idx, itype, (GLsizei)k) : first + (GLint)k));
    return 1;
}

static int m2ba_split(GLenum mode, uint32_t cap, uint32_t *room, uint32_t *ovl, uint32_t *fan)
{
    *ovl = 0;
    *fan = 0;
    switch (mode)
    {
    case GL_POINTS:         *room = cap; break;
    case GL_LINES:          *room = cap & ~1u; break;
    case GL_TRIANGLES:      *room = cap - cap % 3u; break;
    case GL_QUADS:          *room = cap & ~3u; break;
    case GL_LINE_STRIP:     *room = cap; *ovl = 1; break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
    case MGL_FLATSTRIP:     *room = cap & ~1u; *ovl = 2; break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
    case MGL_FLATFAN:       *room = cap; *ovl = 1; *fan = 1; break;
    default:                return 0;
    }
    return *room > *ovl + 1u + *fan;            /* (each op moves on: always, cap >= 528) */
}

static int m2b_arrays_rec(GLcontext c, mgl_ctx5 *x5, GLenum mode, GLint first, GLsizei count, GLenum itype,
                          const GLvoid *idx)
{
    m2ba_plan P;
    if (!x5->m2b || count <= 0 || !m2ba_plan_make(c, x5, &P))
        return 0;
    const uint32_t cap = (MGL_M2B_WORDS - 8u) / P.wpv, total = (uint32_t)count;
    uint32_t room = cap, ovl = 0, fan = 0;
    if (total > cap && !m2ba_split(mode, cap, &room, &ovl, &fan))
        return m2b_arrfb(x5, 16u);              /* (longer than a buffer, not split: the call-per-attribute recording) */
    M2B_TPEND(c, x5);
    for (uint32_t s = 0;;)
    {
        const uint32_t pre = fan && s ? 1u : 0u;
        uint32_t n = room - pre;
        if (n >= total - s)
            n = total - s;
        if (!m2ba_op(c, x5, &P, mode, first, itype, idx, s, n, pre))
            return 1;                           /* (out of memory: dl_put reported it) */
        if (s + n >= total)
            return 1;
        s += n - ovl;
    }
}

static void m2b_arrays_replay(GLcontext c, const uint32_t *w, uint32_t n)
{
    if (n < 5u)
        return;
    const uint32_t count = w[1], lay = w[2], wpv = w[3], offs = w[4];
    const uint32_t vs = M2BA_VS(lay), cs = M2BA_CS(lay), cw = cs ? ((lay & M2BA_CUB) ? 1u : cs) : 0u;
    const uint32_t ts0 = M2BA_TS(lay, 0), ts1 = M2BA_TS(lay, 1), ns = (lay & M2BA_N) ? 3u : 0u;
    const uint32_t nw[5] = { vs, cw, ts0, ts1, ns };
    if (vs < 2u || vs > 4u || (cs && cs != 3u && cs != 4u) || ((lay & M2BA_CUB) && !cs) || M2BA_CT(lay) == 3u || ts0 > 4u || ts1 > 4u ||
        !wpv || wpv > 31u || !count || count > (n - 5u) / wpv || n != 5u + count * wpv)
        return;
    for (uint32_t a = 0; a < 5u; a++)
        if (nw[a] && ((offs >> (5u * a)) & 31u) + nw[a] > wpv)
            return;                             /* (a field past the vertex's words) */
    mgl_ctx5 *const x5 = X5(c);
    const mgl_array sv = c->va_vertex, sc = c->va_color, st0 = c->va_tex[0], st1 = c->va_tex[1], sn = x5->va_normal;
    const uint8_t *b = (const uint8_t *)(w + 5);
    const GLsizei stride = (GLsizei)(4u * wpv);
#define M2BA_AT(a) (b + 4u * ((offs >> (5u * (a))) & 31u))
    c->va_vertex = (mgl_array){ (GLint)vs, GL_FLOAT, stride, M2BA_AT(0), 1u };
    const GLenum ct = !(lay & M2BA_CUB) ? GL_FLOAT : M2BA_CT(lay) == 1u ? MGL_UBYTE_ARGB : M2BA_CT(lay) == 2u ? MGL_UBYTE_BGRA
                                                                                   : GL_UNSIGNED_BYTE;
    c->va_color = (mgl_array){ (GLint)(cs ? cs : 4u), ct, stride, cs ? M2BA_AT(1) : 0, cs ? 1u : 0u };
    c->va_tex[0] = (mgl_array){ (GLint)(ts0 ? ts0 : 4u), GL_FLOAT, stride, ts0 ? M2BA_AT(2) : 0, ts0 ? 1u : 0u };
    c->va_tex[1] = (mgl_array){ (GLint)(ts1 ? ts1 : 4u), GL_FLOAT, stride, ts1 ? M2BA_AT(3) : 0, ts1 ? 1u : 0u };
    x5->va_normal = (mgl_array){ 3, GL_FLOAT, stride, ns ? M2BA_AT(4) : 0, ns ? 1u : 0u };
#undef M2BA_AT
    draw_arrays_x(c, x5, (GLenum)w[0], 0, (GLsizei)count);
    c->va_vertex = sv;
    c->va_color = sc;
    c->va_tex[0] = st0;
    c->va_tex[1] = st1;
    x5->va_normal = sn;
}

static uint32_t pixel_bytes(GLenum format, GLenum type)
{
    uint32_t n;
    switch (format)
    {
    case GL_RGBA: n = 4; break;
    case GL_RGB: n = 3; break;
    case GL_LUMINANCE_ALPHA: n = 2; break;
    case GL_LUMINANCE: case GL_ALPHA: case GL_COLOR_INDEX: case GL_RED: case GL_GREEN: case GL_BLUE: n = 1; break;
    default: n = 4; break;
    }
    if (type == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_4_4_4_4)
        return 2;
    if (type == MGL_UBYTE_ARGB || type == MGL_UBYTE_BGRA)
        return 4;
    if (type == GL_UNSIGNED_SHORT || type == GL_SHORT)
        return 2 * n;
    if (type == GL_FLOAT || type == GL_UNSIGNED_INT || type == GL_INT)
        return 4 * n;
    return n;
}

static uint32_t image_bytes(GLcontext c, GLsizei w, GLsizei h, GLenum format, GLenum type)
{
    const uint32_t bpp = pixel_bytes(format, type);
    const uint32_t rowpx = c->unpack_row > 0 ? (uint32_t)c->unpack_row : (uint32_t)w;
    const uint32_t al = c->unpack_align > 0 ? (uint32_t)c->unpack_align : 4u;
    const uint32_t row = (rowpx * bpp + al - 1u) / al * al;
    return h > 0 ? row * (uint32_t)(h - 1) + (uint32_t)w * bpp : 0u;
}

static void tex_image_rec(GLcontext c, uint32_t op, const uint32_t a[8], GLsizei w, GLsizei h, GLenum format,
                          GLenum type, const GLvoid *pixels)
{
    const uint32_t bytes = pixels ? image_bytes(c, w, h, format, type) : 0u;
    uint32_t *r = dl_put(c, op, 11u + (bytes + 3u) / 4u);
    if (!r)
        return;
    memcpy(r, a, 32);
    r[8] = (uint32_t)c->unpack_align;
    r[9] = (uint32_t)c->unpack_row;
    r[10] = bytes;
    if (bytes)
        memcpy(&r[11], pixels, bytes);
}

static void tex_image_replay(GLcontext c, const uint32_t *w, int sub)
{
    const GLint al = c->unpack_align, row = c->unpack_row;
    c->unpack_align = (GLint)w[8];             /* the pixels as they were laid out when compiled */
    c->unpack_row = (GLint)w[9];
    const GLvoid *px = w[10] ? (const GLvoid *)&w[11] : 0;
    if (!sub)
        mgl_ds_TexImage2D(c, (GLenum)w[0], (GLint)w[1], (GLint)w[2], (GLsizei)w[3], (GLsizei)w[4], (GLint)w[5],
                          (GLenum)w[6], (GLenum)w[7], px);
    else
        mgl_ds_TexSubImage2D(c, (GLenum)w[0], (GLint)w[1], (GLint)w[2], (GLint)w[3], (GLsizei)w[4], (GLsizei)w[5],
                             (GLenum)w[6], (GLenum)w[7], px);
    c->unpack_align = al;
    c->unpack_row = row;
}

void a5_GLTexImage2D(GLcontext c, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                     GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    mgl_ctx5 *const x5 = X5(c);
    target = (GLenum)tr((uint32_t)target);
    internalformat = (GLint)tr_ifmt((uint32_t)internalformat);
    format = (GLenum)tr((uint32_t)format);
    type = (GLenum)tr((uint32_t)type);
    if (x5->m2b)
    {
        mgl_m2b_submit(c, 0);
        mgl_ds_TexImage2D(c, target, level, internalformat, width, height, border, format, type, pixels);
        m2b_tex_changed(c, x5);
        return;
    }
    if (x5->dl_cur)
    {
        const uint32_t a[8] = { target, (uint32_t)level, (uint32_t)internalformat, (uint32_t)width, (uint32_t)height,
                                (uint32_t)border, format, type };
        tex_image_rec(c, OP5_TEXIMAGE, a, width, height, format, type, pixels);
        if (!(x5->dl_exec & MGL_DLX_STATE))
            return;
    }
    mgl_ds_TexImage2D(c, target, level, internalformat, width, height, border, format, type, pixels);
}

void a5_GLTexSubImage2D(GLcontext c, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                        GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    mgl_ctx5 *const x5 = X5(c);
    target = (GLenum)tr((uint32_t)target);
    format = (GLenum)tr((uint32_t)format);
    type = (GLenum)tr((uint32_t)type);
    if (x5->m2b)
    {
        mgl_m2b_submit(c, 0);
        mgl_ds_TexSubImage2D(c, target, level, xoffset, yoffset, width, height, format, type, pixels);
        m2b_tex_changed(c, x5);
        return;
    }
    if (x5->dl_cur)
    {
        const uint32_t a[8] = { target, (uint32_t)level, (uint32_t)xoffset, (uint32_t)yoffset, (uint32_t)width,
                                (uint32_t)height, format, type };
        tex_image_rec(c, OP5_TEXSUBIMAGE, a, width, height, format, type, pixels);
        if (!(x5->dl_exec & MGL_DLX_STATE))
            return;
    }
    mgl_ds_TexSubImage2D(c, target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void a5_GLColorTable(GLcontext c, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type,
                     GLvoid *data)
{
    mgl_ctx5 *const x5 = X5(c);
    target = (GLenum)tr((uint32_t)target);
    internalformat = (GLenum)tr_ifmt((uint32_t)internalformat);
    format = (GLenum)tr((uint32_t)format);
    type = (GLenum)tr((uint32_t)type);
    if (x5->dl_cur && !x5->m2b)
    {
        const uint32_t bytes = data && width > 0 && width <= 256 ? (uint32_t)width * (format == GL_RGBA ? 4u : 3u) : 0u;
        uint32_t *w = dl_put(c, OP5_COLORTABLE, 5u + (bytes + 3u) / 4u);
        if (w)
        {
            w[0] = target; w[1] = internalformat; w[2] = (uint32_t)width; w[3] = format; w[4] = type;
            if (bytes)
                memcpy(&w[5], data, bytes);
        }
        if (!(x5->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLColorTable(c, target, internalformat, width, format, type, data);
}

GLenum a5_GLGetError(GLcontext c)
{
    GLenum e = GLGetError(c);
    mgl_ctx5 *const x5 = X5(c);
    if (e == GL_NO_ERROR && x5->m2b && x5->m2b_query)
    {
        uint32_t q[16];
        mgl_m2b_submit(c, 0);
        if (x5->m2b_query(c, q))
            e = (GLenum)q[0];
    }
    return (GLenum)rtr((uint32_t)e);
}

const GLubyte *a5_GLGetString(GLcontext c, GLenum name)
{
    return GLGetString(c, (GLenum)tr((uint32_t)name));
}

/* an enum-valued state: its answer is a token (translated back) */
static int enum_valued(uint32_t p)
{
    return p == GL_MATRIX_MODE || p == GL_SHADE_MODEL || p == GL_CULL_FACE_MODE || p == GL_FRONT_FACE ||
           p == GL_DEPTH_FUNC || p == GL_ALPHA_TEST_FUNC || p == GL_BLEND_SRC || p == GL_BLEND_DST ||
           p == GL_FOG_MODE || p == GL_BLEND_EQUATION || p == GL_COLOR_MATERIAL_FACE ||
           p == GL_COLOR_MATERIAL_PARAMETER || p == GL_LIST_MODE || p == GL_NORMAL_ARRAY_TYPE ||
           p == GL_VERTEX_ARRAY_TYPE || p == GL_COLOR_ARRAY_TYPE || p == GL_TEXTURE_COORD_ARRAY_TYPE ||
           p == GL_PERSPECTIVE_CORRECTION_HINT || p == GL_FOG_HINT || p == GL_DRAW_BUFFER || p == GL_READ_BUFFER ||
           p == GL_POLYGON_MODE;
}

/* the final standard's own state as floats; 0 = not one of them (the core answers) */
static int get5(GLcontext c, uint32_t p, float *v)
{
    mgl_ctx5 *const x5 = X5(c);
    uint32_t *e = cap5(c, p);
    if (e)
    {
        v[0] = *e ? 1.0f : 0.0f;
        return 1;
    }
    switch (p)
    {
    case GL_CURRENT_NORMAL:       memcpy(v, c->cur_normal, 12); return 3;
    case GL_PACK_ALIGNMENT:       v[0] = (float)x5->pack_align; return 1;
    case GL_PACK_ROW_LENGTH:      v[0] = (float)x5->pack_row; return 1;
    case GL_PACK_SKIP_ROWS:       v[0] = (float)x5->pack_skip_rows; return 1;
    case GL_PACK_SKIP_PIXELS:     v[0] = (float)x5->pack_skip_pixels; return 1;
    case GL_NORMAL_ARRAY:         v[0] = x5->va_normal.enabled ? 1.0f : 0.0f; return 1;
    case GL_NORMAL_ARRAY_TYPE:    v[0] = (float)x5->va_normal.type; return 1;
    case GL_NORMAL_ARRAY_STRIDE:  v[0] = (float)x5->va_normal.stride; return 1;
    case GL_VERTEX_ARRAY_TYPE:    v[0] = (float)c->va_vertex.type; return 1;
    case GL_VERTEX_ARRAY_SIZE:    v[0] = (float)c->va_vertex.size; return 1;
    case GL_VERTEX_ARRAY_STRIDE:  v[0] = (float)c->va_vertex.stride; return 1;
    case GL_COLOR_ARRAY_TYPE:     v[0] = (float)c->va_color.type; return 1;
    case GL_COLOR_ARRAY_SIZE:     v[0] = (float)c->va_color.size; return 1;
    case GL_COLOR_ARRAY_STRIDE:   v[0] = (float)c->va_color.stride; return 1;
    case GL_TEXTURE_COORD_ARRAY_TYPE:   v[0] = (float)c->va_tex[c->client_unit].type; return 1;
    case GL_TEXTURE_COORD_ARRAY_SIZE:   v[0] = (float)c->va_tex[c->client_unit].size; return 1;
    case GL_TEXTURE_COORD_ARRAY_STRIDE: v[0] = (float)c->va_tex[c->client_unit].stride; return 1;
    case GL_VERTEX_ARRAY:         v[0] = c->va_vertex.enabled ? 1.0f : 0.0f; return 1;
    case GL_COLOR_ARRAY:          v[0] = c->va_color.enabled ? 1.0f : 0.0f; return 1;
    case GL_TEXTURE_COORD_ARRAY:  v[0] = c->va_tex[c->client_unit].enabled ? 1.0f : 0.0f; return 1;
    case GL_COLOR_MATERIAL_FACE:  v[0] = (float)x5->cm_face; return 1;
    case GL_COLOR_MATERIAL_PARAMETER: v[0] = (float)x5->cm_mode; return 1;
    case GL_LIGHT_MODEL_AMBIENT:  memcpy(v, x5->lm_ambient, 16); return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = x5->lm_local_viewer ? 1.0f : 0.0f; return 1;
    case GL_LIGHT_MODEL_TWO_SIDE: v[0] = x5->lm_two_side ? 1.0f : 0.0f; return 1;
    case GL_LIST_INDEX:           v[0] = x5->m2b ? 0.0f : (float)x5->dl_cur; return 1;
    case GL_LIST_MODE:            v[0] = (float)(x5->dl_cur && !x5->m2b ? (x5->dl_exec ? GL_COMPILE_AND_EXECUTE : GL_COMPILE) : 0u); return 1;
    case GL_LIST_BASE:            v[0] = 0.0f; return 1;
    case GL_PERSPECTIVE_CORRECTION_HINT: v[0] = (float)c->persp_hint; return 1;
    case GL_FOG_HINT:             v[0] = (float)c->fog_hint; return 1;
    case GL_DRAW_BUFFER:
    case GL_READ_BUFFER:          v[0] = (float)GL_BACK; return 1;
    case GL_POLYGON_MODE:         v[0] = (float)GL_FILL; v[1] = (float)GL_FILL; return 2;
    case GL_POINT_SIZE:           v[0] = c->point_size; return 1;
    default:                      return 0;
    }
}

static int get_any(GLcontext c, uint32_t p5, float *v)
{
    const uint32_t p = tr(p5);
    int n = get5(c, p, v);
    if (!n)
        n = mgl_get_values(c, p, v);
    if (!n)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return 0;
    }
    if (enum_valued(p))
        for (int k = 0; k < n; k++)
            v[k] = (float)rtr((uint32_t)v[k]);
    return n;
}

void a5_GLGetFloatv(GLcontext c, GLenum pname, GLfloat *params)
{
    float v[16];
    const int n = get_any(c, pname, v);
    if (n > 0)
        memcpy(params, v, 4u * (uint32_t)n);
}

void a5_GLGetIntegerv(GLcontext c, GLenum pname, GLint *params)
{
    float v[16];
    const int n = get_any(c, pname, v);
    const uint32_t p = tr(pname);
    const int colour = p == GL_CURRENT_COLOR || p == GL_COLOR_CLEAR_VALUE || p == GL_FOG_COLOR ||
                       p == GL_LIGHT_MODEL_AMBIENT;
    for (int k = 0; k < n; k++)
        params[k] = colour ? (GLint)((double)v[k] * 2147483647.0) : (GLint)(v[k] >= 0.0f ? v[k] + 0.5f : v[k] - 0.5f);
}

void a5_GLGetBooleanv(GLcontext c, GLenum pname, GLboolean *params)
{
    float v[16];
    const int n = get_any(c, pname, v);
    for (int k = 0; k < n; k++)
        params[k] = v[k] != 0.0f ? GL_TRUE : GL_FALSE;
}

void a5_GLGetDoublev(GLcontext c, GLenum pname, GLdouble *params)
{
    float v[16];
    const int n = get_any(c, pname, v);
    for (int k = 0; k < n; k++)
        params[k] = (double)v[k];
}

void a5_GLGetPointerv(GLcontext c, GLenum pname, GLvoid **params)
{
    mgl_ctx5 *const x5 = X5(c);
    const uint32_t p = tr(pname);
    if (p == GL_NORMAL_ARRAY_POINTER)
        *params = (GLvoid *)x5->va_normal.ptr;
    else
        GLGetPointerv(c, p, params);
}
