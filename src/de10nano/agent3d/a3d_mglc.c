/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the GL core's ONCALLs (a MiniGL context recorded by the library, executed here). */

#include "a3d_mgl.h"
#include "mgl/mgl_core.h"

/* the core's two outside symbols (the library defines them in its platform code) */
GLcontext mgl_current = 0;
MGLDispatchTable5 *const mgl_abi5_tables[2] = { 0, 0 };

/* the screen triangles / lines / clears into the context's open pass (a3d_draw.c, the tail) */
int32_t a3d_mglc_tris(a3d_ctx *c, const mgl_vtx *v, uint32_t nv, const uint16_t *idx, uint32_t nt);
int32_t a3d_mglc_lines(a3d_ctx *c, const mgl_vtx *v, uint32_t nlines);
int32_t a3d_mglc_clear(a3d_ctx *c, uint32_t what, uint32_t argb, double z);

typedef struct a3d_mglc
{
    uint32_t used, gen;
    a3d_ctx *c;
    struct mgl_context *gl;                 /* the core's context (the pool)                                     */
    uint32_t submits, words, bad;
} a3d_mglc;
static a3d_mglc g_mglc[A3D_MAX_CTX];
#define A3D_MGLC_PAD 64u                    /* words readable past a buffer's end (mgl_m2b_replay's rule)        */
static uint32_t g_mglc_buf[A3D_MGLC_MAXWORDS + A3D_MGLC_PAD];

#define A3D_MGLC_ARENA (1536u << 10)        /* one GL context (~730 KB, most of it the 2048 texture objects) and its blocks */
#ifdef EMU68_A3D
#include "tlsf.h"
#if defined(DE10NANO) && defined(CHIPSET_AXI)
#include "amiga_memmap.h"
_Static_assert(A3D_MGLC_ARENA_BYTES == A3D_MGLC_ARENA, "amiga_memmap.h's arena is this file's");
#define MGLC_ARENA_GET() ((uint8_t *)(uintptr_t)A3D_MGLC_ARENA_DDR_BASE)
#else
extern void *tlsf;
#define MGLC_ARENA_GET() ((uint8_t *)tlsf_malloc_aligned(tlsf, A3D_MGLC_ARENA, 64u))
#endif
#else                                       /* (the host's tests) */
static uint8_t g_mglc_host_arena[A3D_MGLC_ARENA] __attribute__((aligned(64)));
#define MGLC_ARENA_GET() (g_mglc_host_arena)
#endif
static uint8_t *g_mglc_arena;
static uint32_t g_mglc_brk;
static uint32_t g_mglc_live;                /* GL contexts made and not dropped (the arena is reset at 0) */

static void *mglc_alloc(uint32_t n)
{
    n = (n + 63u) & ~63u;
    if (!g_mglc_arena)
    {
        g_mglc_arena = MGLC_ARENA_GET();
        g_mglc_brk = 0;
    }
    if (g_mglc_arena && n && n <= A3D_MGLC_ARENA - g_mglc_brk)
    {
        void *p = g_mglc_arena + g_mglc_brk;
        g_mglc_brk += n;
        __builtin_memset(p, 0, n);
        return p;
    }
    void *p = a3d_pool_alloc(n ? n : 64u);
    if (p)
        __builtin_memset(p, 0, n ? n : 64u);
    return p;
}

static void mglc_free(void *p)
{
    if (p && (!g_mglc_arena || (uint8_t *)p < g_mglc_arena || (uint8_t *)p >= g_mglc_arena + A3D_MGLC_ARENA))
        a3d_pool_free(p);
}

static void *ms_alloc(void *ud, uint32_t n)
{
    (void)ud;
    return mglc_alloc(n);
}

static void ms_free(void *ud, void *p, uint32_t n)
{
    (void)ud;
    (void)n;
    mglc_free(p);
}

/* the textures are the library's: nothing made, updated or freed here */
static uint32_t ms_tex_create(void *ud, uint32_t fmt, uint32_t nl, const mgl_level *lv)
{
    (void)ud; (void)fmt; (void)nl; (void)lv;
    return 0;
}
static void ms_tex_update(void *ud, uint32_t h, uint32_t fmt, uint32_t level, const mgl_level *lv,
                                        uint32_t x, uint32_t y, uint32_t w, uint32_t hh)
{
    (void)ud; (void)h; (void)fmt; (void)level; (void)lv; (void)x; (void)y; (void)w; (void)hh;
}
static void ms_tex_free(void *ud, uint32_t h)
{
    (void)ud; (void)h;
}

/* GL -> Warp3D, the library's own (src/mgl_amiga.c w3d_cmp / w3d_factor / w3d_filter / w3d_env) */
static uint32_t ms_cmp(uint32_t f)
{
    switch (f)
    {
    case GL_NEVER:    return W3D_Z_NEVER;
    case GL_LESS:     return W3D_Z_LESS;
    case GL_EQUAL:    return W3D_Z_EQUAL;
    case GL_LEQUAL:   return W3D_Z_LEQUAL;
    case GL_GREATER:  return W3D_Z_GREATER;
    case GL_NOTEQUAL: return W3D_Z_NOTEQUAL;
    case GL_GEQUAL:   return W3D_Z_GEQUAL;
    default:          return W3D_Z_ALWAYS;
    }
}
static uint32_t ms_factor(uint32_t f)
{
    switch (f)
    {
    case GL_ZERO:                return W3D_ZERO;
    case GL_ONE:                 return W3D_ONE;
    case GL_SRC_COLOR:           return W3D_SRC_COLOR;
    case GL_DST_COLOR:           return W3D_DST_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return W3D_ONE_MINUS_SRC_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return W3D_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA:           return W3D_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return W3D_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA:           return W3D_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return W3D_ONE_MINUS_DST_ALPHA;
    case GL_SRC_ALPHA_SATURATE:  return W3D_SRC_ALPHA_SATURATE;
    default:                     return W3D_ONE;
    }
}
static uint32_t ms_filter(uint32_t f)
{
    switch (f)
    {
    case GL_NEAREST:                return W3D_NEAREST;
    case GL_LINEAR:                 return W3D_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST: return W3D_NEAREST_MIP_NEAREST;
    case GL_LINEAR_MIPMAP_NEAREST:  return W3D_LINEAR_MIP_NEAREST;
    case GL_NEAREST_MIPMAP_LINEAR:  return W3D_NEAREST_MIP_LINEAR;
    case GL_LINEAR_MIPMAP_LINEAR:   return W3D_LINEAR_MIP_LINEAR;
    default:                        return W3D_LINEAR;
    }
}
static uint32_t ms_env(uint32_t e)
{
    switch (e)
    {
    case GL_REPLACE: return W3D_REPLACE;
    case GL_DECAL:   return W3D_DECAL;
    case GL_BLEND:   return W3D_BLEND;
    default:         return W3D_MODULATE;
    }
}

static uint32_t ms_fbits(float f)
{
    uint32_t w;
    __builtin_memcpy(&w, &f, 4);
    return w;
}

static void ms_onoff(uint8_t *g, uint32_t bit, uint32_t on)
{
    const uint32_t s = a3d_be32(g + W3D_CTX_STATE);
    a3d_wbe32(g + W3D_CTX_STATE, on ? (s | bit) : (s & ~bit));
}

static void ms_state(void *ud, const mgl_raster *r)
{
    a3d_mglc *m = (a3d_mglc *)ud;
    a3d_ctx *const c = m->c;
    if (!a3d_g_ok(c->gctx, W3D_CTX_SIZE, A3D_GR_WRITE) || !a3d_g_ok(c->gpriv, A3D_PRIV_SIZE, A3D_GR_WRITE))
        return;
    uint8_t *const g = a3d_g_ptr(c->gctx), *const p = a3d_g_ptr(c->gpriv);
    ms_onoff(g, W3D_TEXMAPPING, r->tex != 0);
    if (r->tex)
    {
        a3d_wbe32(g + W3D_CTX_CURRENTTEX, r->tex);
        uint32_t tp = 0;
        if (a3d_g_ok(r->tex, W3D_TEX_SIZE, A3D_GR_READ))
            tp = a3d_be32(a3d_g_ptr(r->tex + W3D_TEX_DRIVER));
        if (tp && a3d_g_ok(tp, A3D_TPRIV_SIZE, A3D_GR_WRITE))
        {
            uint8_t *const t = a3d_g_ptr(tp);
            a3d_wbe32(t + A3D_T_MINFILTER, ms_filter(r->min_filter));
            a3d_wbe32(t + A3D_T_MAGFILTER, ms_filter(r->mag_filter));
            a3d_wbe32(t + A3D_T_WRAP_S, r->wrap_s == GL_CLAMP ? W3D_CLAMP : W3D_REPEAT);
            a3d_wbe32(t + A3D_T_WRAP_T, r->wrap_t == GL_CLAMP ? W3D_CLAMP : W3D_REPEAT);
            if (a3d_be32(g + W3D_CTX_STATE) & W3D_GLOBALTEXENV)
            {
                a3d_wbe32(g + W3D_CTX_GLOBALTEXENVMODE, ms_env(r->texenv));
                for (uint32_t i = 0; i < 4u; i++)
                    a3d_wbe32(g + W3D_CTX_GLOBALTEXENVCOLOR + 4u * i, ms_fbits(r->envcolor[i]));
            }
            else
            {
                a3d_wbe32(t + A3D_T_ENVMODE, ms_env(r->texenv));
                for (uint32_t i = 0; i < 4u; i++)
                    a3d_wbe32(t + A3D_T_ENVCOLOR + 4u * i, ms_fbits(r->envcolor[i]));
            }
        }
    }
    ms_onoff(g, W3D_BLENDING, r->blend);
    a3d_wbe32(p + A3D_P_BLEND_SRC, ms_factor(r->blend_src));
    a3d_wbe32(p + A3D_P_BLEND_DST, ms_factor(r->blend_dst));
    ms_onoff(g, W3D_ZBUFFER, r->depth_test);
    ms_onoff(g, W3D_ZBUFFERUPDATE, r->depth_test && r->depth_mask);     /* GL writes depth only while it tests */
    a3d_wbe32(p + A3D_P_ZCOMPARE, ms_cmp(r->depth_func));
    ms_onoff(g, W3D_ALPHATEST, r->alpha_test);
    a3d_wbe32(p + A3D_P_ALPHA_FUNC, ms_cmp(r->alpha_func));             /* (W3D_A_* number as W3D_Z_*) */
    a3d_wbe32(p + A3D_P_ALPHA_REF, ms_fbits(r->alpha_ref));
    ms_onoff(g, W3D_FOGGING, r->fog);
    {
        const float fog[6] = { r->fog_wstart, r->fog_wend, r->fog_density, r->fog_color[0], r->fog_color[1],
                               r->fog_color[2] };
        for (uint32_t i = 0; i < 6u; i++)
            a3d_wbe32(g + W3D_CTX_FOG + 4u * i, ms_fbits(fog[i]));
        a3d_wbe32(p + A3D_P_FOGMODE, r->fog_mode == GL_LINEAR ? W3D_FOG_LINEAR
                                     : r->fog_mode == GL_EXP2 ? W3D_FOG_EXP_2 : W3D_FOG_EXP);
    }
    ms_onoff(g, W3D_SCISSOR, r->scissor);
    a3d_wbe32(g + W3D_CTX_SCISSOR, (uint32_t)r->sc_x);
    a3d_wbe32(g + W3D_CTX_SCISSOR + 4u, (uint32_t)r->sc_y);
    a3d_wbe32(g + W3D_CTX_SCISSOR + 8u, (uint32_t)r->sc_w);
    a3d_wbe32(g + W3D_CTX_SCISSOR + 12u, (uint32_t)r->sc_h);
    a3d_wbe32(p + A3D_P_COLORMASK, r->color_mask & 15u);
    ms_onoff(g, W3D_DITHERING, r->dither);
    ms_onoff(g, W3D_PERSPECTIVE, r->persp);
}

static void ms_tris(void *ud, const mgl_vtx *v, uint32_t nv, const uint16_t *idx, uint32_t nt)
{
    a3d_mglc *m = (a3d_mglc *)ud;
    (void)a3d_mglc_tris(m->c, v, nv, idx, nt);
}

static void ms_lines(void *ud, const mgl_vtx *v, uint32_t n)
{
    a3d_mglc *m = (a3d_mglc *)ud;
    (void)a3d_mglc_lines(m->c, v, n);
}

static void ms_points(void *ud, const mgl_vtx *v, uint32_t n, float size)
{
    (void)ud; (void)v; (void)n; (void)size;     /* (the core makes points squares in the triangle batch) */
}

/* the library's s_clear: W3D_ClearDrawRegion's ARGB, W3D_ClearZBuffer's double; the whole target */
static void ms_clear(void *ud, uint32_t mask, const float rgba[4], float depth, const mgl_raster *r)
{
    a3d_mglc *m = (a3d_mglc *)ud;
    (void)r;
    if (mask & GL_COLOR_BUFFER_BIT)
    {
        const uint32_t argb = ((uint32_t)(rgba[3] * 255.0f + 0.5f) << 24) | ((uint32_t)(rgba[0] * 255.0f + 0.5f) << 16) |
                              ((uint32_t)(rgba[1] * 255.0f + 0.5f) << 8) | (uint32_t)(rgba[2] * 255.0f + 0.5f);
        (void)a3d_mglc_clear(m->c, 1u, argb, 0.0);
    }
    if (mask & GL_DEPTH_BUFFER_BIT)
        (void)a3d_mglc_clear(m->c, 2u, 0u, (double)depth);
}

static void ms_flush(void *ud, uint32_t finish)
{
    (void)ud; (void)finish;                     /* (the library waits through Warp3D: W3D_Flush / W3D_WaitIdle) */
}

static a3d_mglc *mglc_of(a3d_ctx *c, uint32_t h)
{
    const uint32_t i = (h & 0xFFu) - 1u;
    if (!c || i >= A3D_MAX_CTX)
        return 0;
    a3d_mglc *m = &g_mglc[i];
    return m->used && m->gen == (h >> 8) && m->c == c ? m : 0;
}

static void mglc_drop(a3d_mglc *m)
{
    if (m->gl)
    {
        mgl_abi5_drop(m->gl);
        mgl_core_free(m->gl);
        mglc_free(m->gl);
        if (g_mglc_live && !--g_mglc_live)
            g_mglc_brk = 0;                     /* (no GL context left: the arena is whole again) */
    }
    m->gl = 0;
    m->used = 0;
    m->c = 0;
}

void a3d_mglc_reset(void)
{
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        g_mglc[i].used = 0;
        g_mglc[i].gl = 0;
        g_mglc[i].c = 0;
    }
    mgl_abi5_forget();
    mgl_current = 0;
    g_mglc_brk = 0;
    g_mglc_live = 0;
}

static void mglc_reap(void)
{
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
        if (g_mglc[i].used && (!g_mglc[i].c->used || g_mglc[i].c != &a3d_ctxs[i]))
            mglc_drop(&g_mglc[i]);
}

static uint64_t mglc_ctx(a3d_ctx *c, uint32_t op, uint32_t a1)
{
    mglc_reap();
    const uint32_t ci = (uint32_t)(c - a3d_ctxs);
    a3d_mglc *m = &g_mglc[ci];                  /* one GL a context: its index */
    if (op == 1u)
    {
        if (mglc_of(c, a1) != m)
            return (uint32_t)W3D_ILLEGALINPUT;
        mglc_drop(m);
        return W3D_SUCCESS;
    }
    if (op != 0u)
        return (uint32_t)W3D_ILLEGALINPUT;
    const uint32_t w = a1 >> 16, h = a1 & 0xFFFFu;
    if (w == 0 || h == 0 || w > 4096u || h > 4096u)
        return (uint32_t)W3D_ILLEGALINPUT;
    if (m->used)
        mglc_drop(m);                           /* (a context makes its GL once: an older one goes) */
    struct mgl_context *gl = (struct mgl_context *)mglc_alloc((uint32_t)sizeof(struct mgl_context));
    if (!gl)
        return (uint32_t)W3D_NOMEMORY;
    g_mglc_live++;
    m->used = 1;
    m->gen = ((m->gen + 1u) & 0xFFFFFFu) ? (m->gen + 1u) & 0xFFFFFFu : 1u;
    m->c = c;
    m->gl = gl;
    m->submits = m->words = m->bad = 0;
    const mgl_sink sink = { m, ms_alloc, ms_free, ms_tex_create, ms_tex_update, ms_tex_free, ms_state, ms_tris, ms_lines,
                            ms_points, ms_clear, ms_flush, 0 };
    mgl_core_init(gl, &sink, (int32_t)w, (int32_t)h);
    mgl_abi5_attach(gl);
    const uint32_t handle = (m->gen << 8) | (ci + 1u);
    return W3D_SUCCESS | ((uint64_t)handle << 32);
}

static uint64_t mglc_submit(a3d_ctx *c, uint32_t d0, uint32_t a1)
{
    a3d_mglc *m = mglc_of(c, d0 & 0xFFFFFFu);
    if (!m || (a1 & 3u) || !a3d_g_ok(a1, 8, A3D_GR_READ))
        return (uint32_t)W3D_ILLEGALINPUT;
    const uint8_t *blk = a3d_g_ptr(a1);
    const uint32_t n = a3d_be32(blk), wa = a3d_be32(blk + 4);
    if (n > A3D_MGLC_MAXWORDS || (n && ((wa & 3u) || !a3d_g_ok(wa, 4u * n, A3D_GR_READ))))
    {
        m->bad++;
        return (uint32_t)W3D_ILLEGALINPUT;
    }
    const uint8_t *src = n ? a3d_g_ptr(wa) : 0;
    for (uint32_t i = 0; i < n; i++)            /* the padded copy (and the host's byte order) */
        g_mglc_buf[i] = a3d_be32(src + 4u * i);
    for (uint32_t i = 0; i < A3D_MGLC_PAD; i++)
        g_mglc_buf[n + i] = 0;
    const int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return (uint32_t)e;
    mgl_current = m->gl;
    const uint32_t f = d0 >> 24;
    const int ok = mgl_m2b_replay(m->gl, g_mglc_buf, n, (f & A3D_MGLC_F_SWAP) ? MGL_M2B_SWAP : 0u);
    m->submits++;
    m->words += n;
    a3d_mcnt.batches++;
    if (!ok)
    {
        m->bad++;
        return (uint32_t)W3D_ILLEGALINPUT;
    }
    return W3D_SUCCESS | ((uint64_t)n << 32);
}

static uint64_t mglc_query(a3d_ctx *c, uint32_t d0, uint32_t a1)
{
    a3d_mglc *m = mglc_of(c, d0 & 0xFFFFFFu);
    if (!m || (a1 & 3u) || !a3d_g_ok(a1, 64, A3D_GR_WRITE))
        return (uint32_t)W3D_ILLEGALINPUT;
    uint32_t q[16];
    mgl_m2b_query(m->gl, q);
    uint8_t *o = a3d_g_ptr(a1);
    for (uint32_t i = 0; i < 16u; i++)
        a3d_wbe32(o + 4u * i, q[i]);
    return W3D_SUCCESS;
}

/* a3d_mgl.c's a3d_oncall_more: every id past A3D_MGL_TRIS */
uint64_t a3d_mglc_oncall(a3d_ctx *c, uint32_t id, uint32_t d0, uint32_t a1)
{
    if (!c)
        return (uint32_t)W3D_ILLEGALINPUT;
    switch (id)
    {
    case A3D_MGLC_HELLO:  return W3D_SUCCESS | ((uint64_t)A3D_MGLC_VERSION << 32);
    case A3D_MGLC_CTX:    return mglc_ctx(c, d0, a1);
    case A3D_MGLC_SUBMIT:
    {
        a3d_prof_begin(A3D_PROF_DRAW);
        const uint64_t r = mglc_submit(c, d0, a1);
        a3d_prof_end(A3D_PROF_DRAW, &a3d_cnt.prof_draw);
        return r;
    }
    case A3D_MGLC_QUERY:  return mglc_query(c, d0, a1);
    default:              return (uint32_t)W3D_ILLEGALINPUT;
    }
}
