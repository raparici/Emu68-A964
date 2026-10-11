/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the Warp3D draw path (vertex fetch, triangle setup, binning). */

#include "a3d_agent.h"
#include "a3d_fint.h"
#include "a3d_mgl.h"

static const uint8_t a3d_blend_map[16] = {
    1u, 0u /*ZERO*/, 1u /*ONE*/, 3u /*SRC_COLOR*/, 2u /*DST_COLOR*/, 5u /*1-SRC_COLOR*/, 4u /*1-DST_COLOR*/,
    6u /*SRC_ALPHA*/, 7u /*1-SRC_ALPHA*/,
    1u /*DST_ALPHA -> ONE*/, 0u /*1-DST_ALPHA -> ZERO*/, 0u  ,
    1u, 1u, 1u, 1u /* constant colours: no blend constant (refused by the stub) */
};
static const uint8_t a3d_cmp_map[9] = { 0u, 1u /*NEVER*/, 2u /*LESS*/, 7u /*GEQUAL*/, 4u /*LEQUAL*/,
                                        5u /*GREATER*/, 6u /*NOTEQUAL*/, 3u /*EQUAL*/, 0u /*ALWAYS*/ };
static const uint8_t a3d_lop_map[17] = { 2u, 0u, 6u, 12u, 2u, 13u, 4u, 10u, 8u, 9u, 11u, 5u, 14u, 3u, 15u, 7u, 1u };

/* RasterIX TexEnv fields (TexEnvReg.hpp) */
#define TE_COMB_RGB(x)  ((uint32_t)(x) << 0)
#define TE_COMB_A(x)    ((uint32_t)(x) << 3)
#define TE_SRGB0(x)     ((uint32_t)(x) << 6)
#define TE_SRGB1(x)     ((uint32_t)(x) << 8)
#define TE_SRGB2(x)     ((uint32_t)(x) << 10)
#define TE_SA0(x)       ((uint32_t)(x) << 12)
#define TE_SA1(x)       ((uint32_t)(x) << 14)
#define TE_SA2(x)       ((uint32_t)(x) << 16)
#define TE_ORGB0(x)     ((uint32_t)(x) << 18)
#define TE_ORGB1(x)     ((uint32_t)(x) << 20)
#define TE_ORGB2(x)     ((uint32_t)(x) << 22)
#define TE_OA0(x)       ((uint32_t)(x) << 24)
#define TE_OA1(x)       ((uint32_t)(x) << 25)
#define TE_OA2(x)       ((uint32_t)(x) << 26)
enum { CB_REPLACE = 0, CB_MODULATE = 1, CB_ADD = 2, CB_ADD_SIGNED = 3, CB_INTERPOLATE = 4, CB_SUBTRACT = 5 };
enum { SR_TEXTURE = 0, SR_CONSTANT = 1, SR_PRIMARY = 2, SR_PREVIOUS = 3 };
enum { OP_SRC_ALPHA = 0, OP_ONE_MINUS_SRC_ALPHA = 1, OP_SRC_COLOR = 2, OP_ONE_MINUS_SRC_COLOR = 3 };

typedef struct a3d_vtx
{
    float x, y, z, w, sw, tw, r, g, b, a;
    float s, t;
} a3d_vtx;

/* per draw call */
typedef struct a3d_dc
{
    a3d_ctx *c;
    uint32_t state;             /* the guest's W3D_Context.state                                    */
    uint32_t textured;          /* TMU 0 enabled for this call                                      */
    float texw, texh;           /* the texture's source size (texel coordinates -> normalised)      */
    uint32_t persp, gouraud, cull, front_ccw, use_depth, depth_write, color_write;
    float cur[4];               /* W3D_SetCurrentColor                                              */
    a3d_texobj *tex;            /* the texture the triangles sample (its block lives to their fence) */
    float wscale;
} a3d_dc;

static float a3d_clampf(float v, float lo, float hi)
{
    return v > lo ? (v < hi ? v : hi) : lo;
}

static double a3d_clamp01d(double v)
{
    if (!(v >= 0.0))
        return 0.0;
    return v > 1.0 ? 1.0 : v;
}

static int a3d_finite(float v)
{
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    return (u & 0x7FFFFFFFu) < 0x60AD78ECu;
}

static uint32_t a3d_u8(float v)
{
    return a3d_u8i(v);
}

static uint32_t a3d_fbits(float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, 4);
    return u;
}

static float a3d_expf(float x)
{
    if (!(x > -87.0f))
        return 0.0f;                        /* also NaN */
    if (x >= 0.0f)
        return 1.0f;
    const float n = (float)(int32_t)(x * 1.44269504f - 0.5f);   /* round to nearest, x < 0 */
    const float r = x - n * 0.693147181f;
    float e = 1.0f, t = 1.0f;
    for (int k = 1; k <= 8; k++)
    {
        t *= r / (float)k;
        e += t;
    }
    const int32_t ni = (int32_t)n;
    if (ni < -126)
        return 0.0f;
    const uint32_t bits = (uint32_t)(ni + 127) << 23;
    float p;
    __builtin_memcpy(&p, &bits, 4);
    return e * p;
}

/* the fog factor at eye distance d (1 = no fog, 0 = the fog colour), OpenGL's three functions on d */
static float a3d_fog_f(uint32_t mode, float d, float ds, float de, float dens)
{
    float f;
    switch (mode)
    {
    case W3D_FOG_LINEAR:
        f = de == ds ? (d < ds ? 1.0f : 0.0f) : (de - d) / (de - ds);
        break;
    case W3D_FOG_EXP_2:
    {
        const float x = dens * d;
        f = a3d_expf(-(x * x));
        break;
    }
    default:                                /* EXP, and INTERPOLATED drawn as EXP */
        f = a3d_expf(-(dens * d));
        break;
    }
    return f > 0.0f ? (f < 1.0f ? f : 1.0f) : 0.0f;
}

/* a w-space value as an eye distance: d = 1/w, the reference's guard for 0 (Wazp3D56.c:4768-4771) */
static float a3d_wdist(float w)
{
    if (!(w > 1e-6f))
        return 1e6f;
    return w > 1e6f ? 1e-6f : 1.0f / w;
}

void a3d_fog_build(uint32_t mode, float ds, float de, float dens, uint32_t k, uint32_t lut[RIX_FOG_LUT_WORDS])
{
    float f[33];
    float d = 1.0f / (float)(1u << k);      /* d' = 2^i  ->  d = 2^(i - k) */
    for (uint32_t i = 0; i < 33u; i++, d *= 2.0f)
        f[i] = a3d_fog_f(mode, d, ds, de, dens);
    for (uint32_t i = 0; i < 32u; i++)
    {
        const int32_t m = (int32_t)((f[i + 1u] - f[i]) * 4194304.0f);
        const int32_t b = (int32_t)(f[i] * 1073741824.0f);
        lut[2u * i] = (uint32_t)m;
        lut[2u * i + 1u] = (uint32_t)b;
    }
}

uint32_t a3d_fog_scale_k(uint32_t mode, float ds, float de, float dens)
{
    float dlo;
    if (mode == W3D_FOG_LINEAR)
        dlo = ds < de ? ds : de;
    else if (!(dens > 1e-12f))
        return 0;                           /* no density: f = 1 everywhere */
    else
        dlo = (mode == W3D_FOG_EXP_2 ? 0.063f : 0.004f) / dens;    /* f drops below 0.996 */
    uint32_t k = 0;
    while (k < 24u && dlo * (float)(1u << k) < 2.0f)
        k++;
    return k;
}

/* the TexEnv register for a Warp3D environment mode on TMU 0 */
static uint32_t a3d_texenv(uint32_t mode)
{
    switch (mode)
    {
    case W3D_REPLACE:
        return TE_COMB_RGB(CB_REPLACE) | TE_COMB_A(CB_REPLACE) | TE_SRGB0(SR_TEXTURE) | TE_SA0(SR_TEXTURE)
             | TE_ORGB0(OP_SRC_COLOR) | TE_OA0(0);
    case W3D_DECAL:
        return TE_COMB_RGB(CB_INTERPOLATE) | TE_SRGB0(SR_TEXTURE) | TE_SRGB1(SR_PRIMARY) | TE_SRGB2(SR_TEXTURE)
             | TE_ORGB0(OP_SRC_COLOR) | TE_ORGB1(OP_SRC_COLOR) | TE_ORGB2(OP_SRC_ALPHA)
             | TE_COMB_A(CB_REPLACE) | TE_SA0(SR_PRIMARY) | TE_OA0(0);
    case W3D_BLEND:
        return TE_COMB_RGB(CB_INTERPOLATE) | TE_SRGB0(SR_CONSTANT) | TE_SRGB1(SR_PRIMARY) | TE_SRGB2(SR_TEXTURE)
             | TE_ORGB0(OP_SRC_COLOR) | TE_ORGB1(OP_SRC_COLOR) | TE_ORGB2(OP_SRC_COLOR)
             | TE_COMB_A(CB_MODULATE) | TE_SA0(SR_TEXTURE) | TE_SA1(SR_PRIMARY);
    case W3D_ADD:
        return TE_COMB_RGB(CB_ADD) | TE_SRGB0(SR_TEXTURE) | TE_SRGB1(SR_PRIMARY) | TE_ORGB0(OP_SRC_COLOR)
             | TE_ORGB1(OP_SRC_COLOR) | TE_COMB_A(CB_MODULATE) | TE_SA0(SR_TEXTURE) | TE_SA1(SR_PRIMARY);
    case W3D_SUB:
        return TE_COMB_RGB(CB_SUBTRACT) | TE_SRGB0(SR_PRIMARY) | TE_SRGB1(SR_TEXTURE) | TE_ORGB0(OP_SRC_COLOR)
             | TE_ORGB1(OP_SRC_COLOR) | TE_COMB_A(CB_MODULATE) | TE_SA0(SR_TEXTURE) | TE_SA1(SR_PRIMARY);
    case W3D_OFF:       /* the primary colour passes */
        return TE_COMB_RGB(CB_REPLACE) | TE_COMB_A(CB_REPLACE) | TE_SRGB0(SR_PRIMARY) | TE_SA0(SR_PRIMARY)
             | TE_ORGB0(OP_SRC_COLOR);
    case W3D_MODULATE:
    default:
        return TE_COMB_RGB(CB_MODULATE) | TE_COMB_A(CB_MODULATE) | TE_SRGB0(SR_TEXTURE) | TE_SRGB1(SR_PRIMARY)
             | TE_SA0(SR_TEXTURE) | TE_SA1(SR_PRIMARY) | TE_ORGB0(OP_SRC_COLOR) | TE_ORGB1(OP_SRC_COLOR);
    }
}

static uint32_t a3d_rgba(const float v[4])
{
    return (a3d_u8(v[0]) << 24) | (a3d_u8(v[1]) << 16) | (a3d_u8(v[2]) << 8) | a3d_u8(v[3]);
}

/* translate the Warp3D state for a draw call that binds `gtex` (0 = none) */
static int32_t a3d_state(a3d_dc *d, uint32_t gtex, uint32_t lines_or_points)
{
    a3d_ctx *c = d->c;
    const uint8_t *g = a3d_g_ptr(c->gctx);
    const uint8_t *p = a3d_g_ptr(c->gpriv);
    const uint32_t st = a3d_be32(g + W3D_CTX_STATE);
    d->state = st;
    d->persp = (st & W3D_PERSPECTIVE) != 0;
    d->gouraud = (st & W3D_GOURAUD) != 0;
    d->cull = !lines_or_points && (st & W3D_CULLFACE) != 0;
    d->front_ccw = a3d_be32(g + W3D_CTX_FRONTFACEORDER) == W3D_CCW;
    const uint32_t zalloc = a3d_be16(g + W3D_CTX_ZBUFFERALLOC) != 0;
    d->use_depth = zalloc && (st & W3D_ZBUFFER) != 0;
    d->depth_write = d->use_depth && (st & W3D_ZBUFFERUPDATE) != 0;
    for (uint32_t i = 0; i < 4u; i++)
        d->cur[i] = a3d_bef32(p + A3D_P_CURCOLOR + 4u * i);

    /* the texture */
    a3d_texobj *t = 0;
    d->textured = 0;
    d->texw = d->texh = 1.0f;
    if ((st & W3D_TEXMAPPING) && gtex)
    {
        t = a3d_tex_lookup(gtex);
        if (t)
        {
            d->textured = 1;
            a3d_tex_size(t, &d->texw, &d->texh);
        }
    }

    uint32_t fog = 0;
    d->wscale = 1.0f;
    const uint32_t fmode = a3d_be32(p + A3D_P_FOGMODE);
    if ((st & W3D_FOGGING) && fmode >= W3D_FOG_LINEAR && fmode <= W3D_FOG_INTERPOLATED)
    {
        const uint32_t ws = a3d_be32(g + W3D_CTX_FOG + W3D_FG_START), we = a3d_be32(g + W3D_CTX_FOG + W3D_FG_END);
        const uint32_t wd = a3d_be32(g + W3D_CTX_FOG + W3D_FG_DENSITY);
        float fs, fe0, fd;
        __builtin_memcpy(&fs, &ws, 4);
        __builtin_memcpy(&fe0, &we, 4);
        __builtin_memcpy(&fd, &wd, 4);
        const float ds = a3d_wdist(fs), de = a3d_wdist(fe0);
        const float dens = fd == fd ? (fd < 1e12f ? fd : 1e12f) : 0.0f;
        const uint32_t k = a3d_fog_scale_k(fmode, ds, de, dens);
        const uint32_t key[5] = { fmode, ws, we, wd, k };
        uint32_t same = c->fog_key[5];
        for (uint32_t i = 0; i < 5u; i++)
            same &= key[i] == c->fog_key[i];
        if (!same)
        {
            a3d_fog_build(fmode, ds, de, dens, k, c->fog_lut);
            __builtin_memcpy(c->fog_key, key, sizeof(key));
            c->fog_key[5] = 1;
            a3d_bin_fog(&a3d_B, c->fog_lut);
            a3d_cnt.fog_luts++;
        }
        float fc[4] = { a3d_bef32(g + W3D_CTX_FOG + W3D_FG_COLOR), a3d_bef32(g + W3D_CTX_FOG + W3D_FG_COLOR + 4u),
                        a3d_bef32(g + W3D_CTX_FOG + W3D_FG_COLOR + 8u), 1.0f };
        a3d_reg(c, RIX_REG_FOG_COLOR, a3d_rgba(fc));
        d->wscale = 1.0f / (float)(1u << k);
        fog = 1;
    }

    uint32_t fe = 0;
    if (fog)                 fe |= 1u << 0;
    if (st & W3D_BLENDING)   fe |= 1u << 1;
    if (d->use_depth)        fe |= 1u << 2;
    if (st & W3D_ALPHATEST)  fe |= 1u << 3;
    if (st & W3D_SCISSOR)    fe |= 1u << 5;
    if (d->textured)         fe |= 1u << 6;
    if (st & W3D_LOGICOP)    fe |= 1u << 8;
    if (st & W3D_DITHERING)  fe |= RIX_FE_DITHER;
    fe |= RIX_FE_CLEAR_ON_COMMIT;

    uint32_t zc = a3d_be32(p + A3D_P_ZCOMPARE);
    uint32_t af = a3d_be32(p + A3D_P_ALPHA_FUNC);
    uint32_t bs = a3d_be32(p + A3D_P_BLEND_SRC);
    uint32_t bd = a3d_be32(p + A3D_P_BLEND_DST);
    uint32_t lo = a3d_be32(p + A3D_P_LOGICOP);
    uint32_t cm = a3d_be32(p + A3D_P_COLORMASK);
    if (zc == 0 || zc > 8u) zc = W3D_Z_LESS;
    if (af == 0 || af > 8u) af = W3D_Z_ALWAYS;
    if (bs == 0 || bs > 15u) bs = W3D_ONE;
    if (bd == 0 || bd > 15u) bd = W3D_ZERO;
    if (lo == 0 || lo > 16u) lo = W3D_LO_COPY;
    cm = cm ? (cm & 0xFu) : 0xFu;
    const uint32_t aref = a3d_u8(a3d_bef32(p + A3D_P_ALPHA_REF));
    const uint32_t fp = (uint32_t)a3d_cmp_map[zc] | ((uint32_t)a3d_cmp_map[af] << 3) | (aref << 6)
                      | ((d->depth_write ? 1u : 0u) << 14)
                      | (((cm >> 3) & 1u) << 15) | (((cm >> 2) & 1u) << 16) | (((cm >> 1) & 1u) << 17)
                      | ((cm & 1u) << 18)
                      | ((uint32_t)a3d_blend_map[bs] << 19) | ((uint32_t)a3d_blend_map[bd] << 23)
                      | ((uint32_t)a3d_lop_map[lo] << 27);
    d->color_write = (cm & 7u) != 0;

    a3d_reg(c, RIX_REG_FEATURE_ENABLE, fe);
    a3d_reg(c, RIX_REG_FRAGMENT_PIPELINE, fp);
    if (st & W3D_SCISSOR)
    {
        const int64_t sl = (int32_t)a3d_be32(g + W3D_CTX_SCISSOR + 0);
        const int64_t stp = (int32_t)a3d_be32(g + W3D_CTX_SCISSOR + 4);
        const int64_t sw = (int32_t)a3d_be32(g + W3D_CTX_SCISSOR + 8);
        const int64_t sh = (int32_t)a3d_be32(g + W3D_CTX_SCISSOR + 12);
        int64_t x0 = sl < 0 ? 0 : sl, y0 = stp < 0 ? 0 : stp;
        int64_t x1 = sl + sw, y1 = stp + sh;
        if (x0 > (int64_t)c->tgt.width) x0 = c->tgt.width;
        if (y0 > (int64_t)c->tgt.height) y0 = c->tgt.height;
        if (x1 > (int64_t)c->tgt.width) x1 = c->tgt.width;
        if (y1 > (int64_t)c->tgt.height) y1 = c->tgt.height;
        if (x1 < x0) x1 = x0;
        if (y1 < y0) y1 = y0;
        const int64_t H = (int64_t)c->tgt.height;
        a3d_reg(c, RIX_REG_SCISSOR_START, rix_xy((uint32_t)x0, (uint32_t)(H - y1)));
        a3d_reg(c, RIX_REG_SCISSOR_END, rix_xy((uint32_t)x1, (uint32_t)(H - y0)));
    }
    if (d->textured)
    {
        /* the environment: the texture's own, or the global one (GLOBALTEXENV, or never set) */
        uint32_t env = 0;
        float envc[4];
        uint32_t tpriv = 0;
        if (a3d_g_ok(gtex + W3D_TEX_DRIVER, 4, A3D_GR_READ))
            tpriv = a3d_be32(a3d_g_ptr(gtex + W3D_TEX_DRIVER));
        const int tp_ok = tpriv && a3d_g_ok(tpriv, A3D_TPRIV_SIZE, A3D_GR_READ)
                          && a3d_be32(a3d_g_ptr(tpriv)) == A3D_TPRIV_MAGIC;
        if (tp_ok && !(st & W3D_GLOBALTEXENV))
        {
            env = a3d_be32(a3d_g_ptr(tpriv + A3D_T_ENVMODE));
            for (uint32_t i = 0; i < 4u; i++)
                envc[i] = a3d_bef32(a3d_g_ptr(tpriv + A3D_T_ENVCOLOR + 4u * i));
        }
        if (env == 0 || env > W3D_OFF)
        {
            env = a3d_be32(g + W3D_CTX_GLOBALTEXENVMODE);
            for (uint32_t i = 0; i < 4u; i++)
                envc[i] = a3d_bef32(g + W3D_CTX_GLOBALTEXENVCOLOR + 4u * i);
        }
        if (env == 0 || env > W3D_OFF)
            env = W3D_MODULATE;
        a3d_reg(c, RIX_REG_TEX_ENV(0), a3d_texenv(env));
        a3d_reg(c, RIX_REG_TEX_ENV_COLOR(0), a3d_rgba(envc));
        a3d_reg(c, RIX_REG_TMU_TEXTURE(0), a3d_tex_tmureg(t, gtex));
        if (c->tex_bound != a3d_tex_handle(t) || c->tex_ugen != a3d_tex_ugen(t))
        {
            uint32_t pages[A3D_MAX_PAGES];
            const uint32_t n = a3d_tex_stream(t, pages, A3D_MAX_PAGES);
            a3d_bin_tex(&a3d_B, rix_tex_op(0, n), pages, n);
            c->tex_bound = a3d_tex_handle(t);
            c->tex_ugen = a3d_tex_ugen(t);
        }
        d->tex = t;
    }
    return W3D_SUCCESS;
}

#define A3D_SC_WORDS    48u
typedef struct a3d_scache
{
    uint32_t valid, epoch, n;
    uint32_t w[A3D_SC_WORDS];       /* the key                                                          */
    a3d_dc out;                     /* the translation's results (c excluded)                           */
} a3d_scache;
static a3d_scache g_scache[A3D_MAX_CTX] A3D_R8_BSS;
uint32_t a3d_state_epoch A3D_R8_BSS;
uint32_t a3d_state_full_on A3D_R8_BSS;

/* every word a3d_state reads, in a fixed order; the count */
A3D_TAIL_TEXT static uint32_t a3d_state_key(const a3d_dc *d, uint32_t gtex, uint32_t lp, uint32_t w[A3D_SC_WORDS])
{
    const a3d_ctx *c = d->c;
    const uint8_t *g = a3d_g_ptr(c->gctx);
    const uint8_t *p = a3d_g_ptr(c->gpriv);
    uint32_t n = 0;
    w[n++] = gtex;
    w[n++] = lp;
    w[n++] = c->tgt.width;
    w[n++] = c->tgt.height;
    w[n++] = a3d_be32(g + W3D_CTX_STATE);
    w[n++] = a3d_be32(g + W3D_CTX_FRONTFACEORDER);
    w[n++] = a3d_be16(g + W3D_CTX_ZBUFFERALLOC);
    for (uint32_t i = 0; i < W3D_FG_SIZE / 4u; i++)
        w[n++] = a3d_be32(g + W3D_CTX_FOG + 4u * i);
    for (uint32_t i = 0; i < 4u; i++)
        w[n++] = a3d_be32(g + W3D_CTX_SCISSOR + 4u * i);
    w[n++] = a3d_be32(g + W3D_CTX_GLOBALTEXENVMODE);
    for (uint32_t i = 0; i < 4u; i++)
        w[n++] = a3d_be32(g + W3D_CTX_GLOBALTEXENVCOLOR + 4u * i);
    for (uint32_t o = A3D_P_BLEND_SRC; o < A3D_P_FOGMODE + 4u; o += 4u)   /* blend .. alpha, Z, logic, mask, colour, fog */
        w[n++] = a3d_be32(p + o);
    uint32_t tp = 0;
    if (gtex && a3d_g_ok(gtex + W3D_TEX_DRIVER, 4, A3D_GR_READ))
        tp = a3d_be32(a3d_g_ptr(gtex + W3D_TEX_DRIVER));
    w[n++] = tp;
    const int tok = tp && a3d_g_ok(tp, A3D_TPRIV_SIZE, A3D_GR_READ);
    for (uint32_t o = 0; o < A3D_T_CHROMA_LO; o += 4u)                    /* magic, handle, gen, filters, wraps, env */
        w[n++] = tok ? a3d_be32(a3d_g_ptr(tp) + o) : 0xFFFFFFFFu;
    return n;
}

static int32_t a3d_state(a3d_dc *d, uint32_t gtex, uint32_t lines_or_points);
A3D_TAIL_CALL static int32_t a3d_state_c(a3d_dc *d, uint32_t gtex, uint32_t lines_or_points)
{
    if (a3d_state_full_on)
        return a3d_state(d, gtex, lines_or_points);
    a3d_ctx *const c = d->c;
    a3d_scache *s = &g_scache[(uint32_t)(c - a3d_ctxs)];
    uint32_t w[A3D_SC_WORDS];
    const uint32_t n = a3d_state_key(d, gtex, lines_or_points, w);
    if (s->valid && s->epoch == a3d_state_epoch && s->n == n)
    {
        uint32_t same = 1;
        for (uint32_t i = 0; i < n; i++)
            same &= w[i] == s->w[i];
        if (same)
        {
            *d = s->out;
            d->c = c;
            a3d_r8cnt.state_skips++;
            return W3D_SUCCESS;
        }
    }
    const int32_t e = a3d_state(d, gtex, lines_or_points);
    s->valid = e == W3D_SUCCESS;
    s->epoch = a3d_state_epoch;
    s->n = n;
    for (uint32_t i = 0; i < n; i++)
        s->w[i] = w[i];
    s->out = *d;
    a3d_r8cnt.state_full++;
    return e;
}

static void a3d_tv_words(const a3d_dc *d, const a3d_vtx *v, int32_t h16, uint32_t o[6])
{
    const uint32_t z30 = a3d_z30i(v->z);
    const uint32_t x = (uint32_t)a3d_sn4i(v->x) & 0xFFFFu;
    const uint32_t y = (uint32_t)(h16 - a3d_sn4i(v->y)) & 0xFFFFu;
    o[0] = (y << 16) | x;
    o[1] = z30;
    o[2] = a3d_fbits(v->w * d->wscale);
    o[3] = a3d_fbits(v->s);
    o[4] = a3d_fbits(v->t);
    o[5] = (a3d_u8(v->r) << 24) | (a3d_u8(v->g) << 16) | (a3d_u8(v->b) << 8) | a3d_u8(v->a);
}

static void a3d_bin_tail(a3d_dc *d, uint32_t y0, uint32_t y1, const uint32_t w[A3D_TV_WORDS]);

static void a3d_bin_tv(a3d_dc *d, const a3d_vtx v[3])
{
    a3d_ctx *c = d->c;
    float miny = v[0].y, maxy = v[0].y, minx = v[0].x, maxx = v[0].x;
    int32_t kminy = a3d_fkey(miny), kmaxy = kminy, kminx = a3d_fkey(minx), kmaxx = kminx;
    for (uint32_t i = 1; i < 3u; i++)
    {
        const int32_t ky = a3d_fkey(v[i].y), kx = a3d_fkey(v[i].x);
        if (ky < kminy) { kminy = ky; miny = v[i].y; }
        if (ky > kmaxy) { kmaxy = ky; maxy = v[i].y; }
        if (kx < kminx) { kminx = kx; minx = v[i].x; }
        if (kx > kmaxx) { kmaxx = kx; maxx = v[i].x; }
    }
    (void)minx;
    (void)maxx;
    if (kmaxy < 0 || kminy >= a3d_fkey((float)c->tgt.height) || kmaxx < 0 || kminx >= a3d_fkey((float)c->tgt.width))
        return;                                 /* entirely off the target */
    const uint32_t y0 = kminy <= 0 ? 0u : (uint32_t)miny;
    uint32_t y1 = kmaxy >= a3d_fkey((float)(c->tgt.height - 1u)) ? c->tgt.height - 1u : (uint32_t)maxy;
    uint32_t w[A3D_TV_WORDS];
    const int32_t h16 = (int32_t)(16u * c->tgt.height);
    for (uint32_t i = 0; i < 3u; i++)
        a3d_tv_words(d, &v[i], h16, &w[6u * i]);
    a3d_bin_tail(d, y0, y1, w);
}

static void a3d_bin_tail(a3d_dc *d, uint32_t y0, uint32_t y1, const uint32_t w[A3D_TV_WORDS])
{
    a3d_ctx *c = d->c;
    uint32_t f, l;
    if (!a3d_room(c))
    {
        /* the pass was cut: the new list set starts with unknown shadows, the lazy binner re-sends */
    }
    if (!a3d_bin_tri_bands(&a3d_B, y0, y1, &f, &l))
        return;
    a3d_tex_used(d->tex, a3d_P.fence);          /* after a cut: the NEW pass's fence */
    const uint32_t ready = A3D_BT_HEADER | A3D_BT_COLOR | (d->use_depth ? A3D_BT_DEPTH : 0u);
    const uint32_t fl = A3D_BT_CDIRTY | (d->depth_write ? A3D_BT_ZDIRTY : 0u);
    a3d_P.work = 1;
    for (uint32_t i = f; i <= l; i++)
    {
        if ((a3d_P.band[i] & ready) != ready)
            a3d_band_prepare(c, i, 1u, d->use_depth, 0u, 0u);
        a3d_bin_tri_band(&a3d_B, i, A3D_TV_HDR, w, A3D_TV_WORDS);
        a3d_P.band[i] |= fl;
        a3d_cnt.tri_bands++;
    }
    a3d_cnt.tris_binned++;
}

static uint32_t a3d_clip_axis(const a3d_vtx *in, uint32_t n, a3d_vtx *out, uint32_t axis, float lim, int max)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const a3d_vtx *a = &in[i], *b = &in[(i + 1u) % n];
        const float va = axis ? a->y : a->x, vb = axis ? b->y : b->x;
        const int ina = max ? va <= lim : va >= lim;
        const int inb = max ? vb <= lim : vb >= lim;
        if (ina)
            out[m++] = *a;
        if (ina != inb)
        {
            const float tt = (lim - va) / (vb - va);
            a3d_vtx r;
            const float *pa = &a->x, *pb = &b->x;
            float *pr = &r.x;
            for (uint32_t k = 0; k < 10u; k++)          /* x .. a: the screen-linear ten */
                pr[k] = pa[k] + (pb[k] - pa[k]) * tt;
            r.s = r.w != 0.0f ? r.sw / r.w : 0.0f;
            r.t = r.w != 0.0f ? r.tw / r.w : 0.0f;
            out[m++] = r;
        }
    }
    return m;
}

static void a3d_emit_clip(a3d_dc *d, const a3d_vtx v[3]);

static void a3d_emit(a3d_dc *d, const a3d_vtx *v0, const a3d_vtx *v1, const a3d_vtx *v2, uint32_t lines_or_points)
{
    a3d_cnt.tris_in++;
    a3d_vtx v[3] = { *v0, *v1, *v2 };
    if (!d->gouraud)
        for (uint32_t i = 0; i < 3u; i++)
        {
            v[i].r = d->cur[0]; v[i].g = d->cur[1]; v[i].b = d->cur[2]; v[i].a = d->cur[3];
        }
    for (uint32_t i = 0; i < 3u; i++)
        if (!a3d_finite(v[i].x) || !a3d_finite(v[i].y) || !a3d_finite(v[i].z) || !a3d_finite(v[i].w))
            return;                             /* NaN or infinite: drop it (the hardware would draw garbage) */
    if (!lines_or_points)
    {
        const float cross = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (a3d_fzero(cross))
        {
            a3d_cnt.tris_culled++;
            return;
        }
        /* Wazp3D's rule (soft3d56.c): counter-clockwise on screen (y down) <=> cross < 0 */
        const int ccw = a3d_fneg(cross);
        if (d->cull && (d->front_ccw ? !ccw : ccw))
        {
            a3d_cnt.tris_culled++;
            return;
        }
    }
    const float gx0 = 0.5f * (float)d->c->tgt.width - A3D_GUARD_HALF, gx1 = gx0 + 2.0f * A3D_GUARD_HALF;
    const float gy0 = 0.5f * (float)d->c->tgt.height - A3D_GUARD_HALF, gy1 = gy0 + 2.0f * A3D_GUARD_HALF;
    const int32_t kgx0 = a3d_fkey(gx0), kgx1 = a3d_fkey(gx1), kgy0 = a3d_fkey(gy0), kgy1 = a3d_fkey(gy1);
    int outside = 0;
    for (uint32_t i = 0; i < 3u; i++)
    {
        const int32_t kx = a3d_fkey(v[i].x), ky = a3d_fkey(v[i].y);
        if (kx < kgx0 || kx > kgx1 || ky < kgy0 || ky > kgy1)
            outside = 1;
    }
    if (!outside)
    {
        a3d_bin_tv(d, v);
        return;
    }
    a3d_emit_clip(d, v);
}

static void a3d_emit_clip(a3d_dc *d, const a3d_vtx v[3])
{
    const float gx0 = 0.5f * (float)d->c->tgt.width - A3D_GUARD_HALF, gx1 = gx0 + 2.0f * A3D_GUARD_HALF;
    const float gy0 = 0.5f * (float)d->c->tgt.height - A3D_GUARD_HALF, gy1 = gy0 + 2.0f * A3D_GUARD_HALF;
    a3d_cnt.tris_clipped++;
    a3d_vtx p0[9], p1[9];
    uint32_t n = 3;
    __builtin_memcpy(p0, v, 3u * sizeof(a3d_vtx));
    n = a3d_clip_axis(p0, n, p1, 0, gx0, 0);
    n = a3d_clip_axis(p1, n, p0, 0, gx1, 1);
    n = a3d_clip_axis(p0, n, p1, 1, gy0, 0);
    n = a3d_clip_axis(p1, n, p0, 1, gy1, 1);
    for (uint32_t i = 0; i < n; i++)
        if (!a3d_finite(p0[i].x) || !a3d_finite(p0[i].y))
            return;
    for (uint32_t i = 1; i + 1u < n; i++)
    {
        const a3d_vtx t[3] = { p0[0], p0[i], p0[i + 1u] };
        a3d_bin_tv(d, t);
    }
}

/* a line as a quad of `width` pixels (at least 1); a point as a square */
#define A3D_MAX_PRIMSIZE 256.0f    /* widest line / largest point, pixels (a guest may ask for 1e30) */

static void a3d_line(a3d_dc *d, const a3d_vtx *a, const a3d_vtx *b, float width)
{
    width = a3d_clampf(width, 1.0f, A3D_MAX_PRIMSIZE);
    float dx = b->x - a->x, dy = b->y - a->y;
    float len2 = dx * dx + dy * dy;
    float nx, ny;
    if (len2 <= 0.0f)
    {
        nx = 0.0f; ny = 0.5f * width;
    }
    else
    {
        /* the normal, scaled to half the width */
        const float inv = 1.0f / __builtin_sqrtf(len2);
        nx = -dy * inv * 0.5f * width;
        ny = dx * inv * 0.5f * width;
    }
    a3d_vtx q[4] = { *a, *a, *b, *b };
    q[0].x += nx; q[0].y += ny;
    q[1].x -= nx; q[1].y -= ny;
    q[2].x -= nx; q[2].y -= ny;
    q[3].x += nx; q[3].y += ny;
    a3d_emit(d, &q[0], &q[1], &q[2], 1);
    a3d_emit(d, &q[0], &q[2], &q[3], 1);
}

static void a3d_point(a3d_dc *d, const a3d_vtx *v, float size)
{
    size = a3d_clampf(size, 1.0f, A3D_MAX_PRIMSIZE);
    const float h = 0.5f * size;
    a3d_vtx q[4] = { *v, *v, *v, *v };
    q[0].x -= h; q[0].y -= h;
    q[1].x += h; q[1].y -= h;
    q[2].x += h; q[2].y += h;
    q[3].x -= h; q[3].y += h;
    a3d_emit(d, &q[0], &q[1], &q[2], 1);
    a3d_emit(d, &q[0], &q[2], &q[3], 1);
}

static void a3d_finish_vtx(const a3d_dc *d, a3d_vtx *o, float u, float v, float w)
{
    if (!d->persp || !a3d_wok(w))
        w = 1.0f;
    o->w = w;
    if (!a3d_finite(u))
        u = 0.0f;
    if (!a3d_finite(v))
        v = 0.0f;
    if (d->textured)
    {
        o->sw = u * w;
        o->tw = v * w;
        if (!a3d_finite(o->sw))
            o->sw = 0.0f;
        if (!a3d_finite(o->tw))
            o->tw = 0.0f;
        o->s = a3d_fzero(o->sw) && !a3d_fzero(u) ? 0.0f : u;
        o->t = a3d_fzero(o->tw) && !a3d_fzero(v) ? 0.0f : v;
    }
    else
    {
        o->sw = 0.0f;
        o->tw = 0.0f;
        o->s = 0.0f;
        o->t = 0.0f;
    }
}

/* a W3D_Vertex (64 bytes, texel coordinates) at guest address va; 0 = refused */
static int a3d_fetch_w3dv(const a3d_dc *d, uint32_t va, a3d_vtx *o)
{
    if (!a3d_g_ok(va, W3D_V_SIZE, A3D_GR_READ))
        return 0;
    const uint8_t *p = a3d_g_ptr(va);
    o->x = a3d_bef32(p + W3D_V_X);
    o->y = a3d_bef32(p + W3D_V_Y);
    o->z = (float)a3d_bef64(p + W3D_V_Z);
    o->r = a3d_bef32(p + W3D_V_COLOR + 0);
    o->g = a3d_bef32(p + W3D_V_COLOR + 4);
    o->b = a3d_bef32(p + W3D_V_COLOR + 8);
    o->a = a3d_bef32(p + W3D_V_COLOR + 12);
    const float u = a3d_bef32(p + W3D_V_U) / d->texw;
    const float v = a3d_bef32(p + W3D_V_V) / d->texh;
    a3d_finish_vtx(d, o, u, v, a3d_bef32(p + W3D_V_W));
    return 1;
}

/* the context's vertex arrays, as read at the draw call */
typedef struct a3d_arrays
{
    uint32_t vp, vstride, vmode;
    uint32_t tp, tstride, tvoff, twoff, tflags;
    uint32_t cp, cstride, cmode;
    uint32_t fast;              /* the plan applies */
    int32_t lo;                 /* the record's first byte read, from the position (<= 0) */
    uint32_t span;              /* bytes from there to the last byte read */
    uint32_t ox, ou, ov, ow, oc;    /* x, u, v, w, the first colour component: offsets from the first byte read */
} a3d_arrays;

#define A3D_PLAN_SPAN 128
static void a3d_arrays_plan(a3d_arrays *a)
{
    a->fast = 0;
    if (a->vmode != W3D_VERTEX_F_F_F || a->vstride > 0x10000u)
    {
        return;
    }
    int32_t lo = 0, hi = 12;                    /* x y z at the position, relative to it */
    int32_t du = 0, dc = 0;
    if (a->tp)
    {
        const int32_t vo = (int32_t)a->tvoff, wo = (int32_t)a->twoff;
        du = (int32_t)(a->tp - a->vp);
        if (a->tstride != a->vstride || du < -0x8000 || du > 0x8000 || vo < -0x8000 || vo > 0x8000
            || wo < -0x8000 || wo > 0x8000)
        {
            return;
        }
        const int32_t o[3] = { du, du + vo, du + wo };
        for (uint32_t k = 0; k < (wo ? 3u : 2u); k++)
        {
            if (o[k] < lo) lo = o[k];
            if (o[k] + 4 > hi) hi = o[k] + 4;
        }
    }
    if (a->cp)
    {
        const uint32_t m = a->cmode & 0x1Fu;
        const int32_t n = (m == W3D_CMODE_RGB || m == W3D_CMODE_BGR) ? 3 : 4;
        dc = (int32_t)(a->cp - a->vp);
        if (a->cstride != a->vstride || !(a->cmode & W3D_COLOR_FLOAT) || dc < -0x8000 || dc > 0x8000)
        {
            return;
        }
        if (dc < lo) lo = dc;
        if (dc + 4 * n > hi) hi = dc + 4 * n;
    }
    if (hi - lo > A3D_PLAN_SPAN)
    {
        return;
    }
    a->lo = lo;
    a->span = (uint32_t)(hi - lo);
    a->ox = (uint32_t)-lo;
    a->ou = (uint32_t)(du - lo);
    a->ov = (uint32_t)(du + (int32_t)a->tvoff - lo);
    a->ow = (uint32_t)(du + (int32_t)a->twoff - lo);
    a->oc = (uint32_t)(dc - lo);
    a->fast = 1;
}

static void a3d_arrays_read(const a3d_ctx *c, a3d_arrays *a)
{
    const uint8_t *g = a3d_g_ptr(c->gctx);
    a->vp = a3d_be32(g + W3D_CTX_VERTEXPOINTER);
    a->vstride = a3d_be32(g + W3D_CTX_VPSTRIDE);
    a->vmode = a3d_be32(g + W3D_CTX_VPMODE);
    a->tp = a3d_be32(g + W3D_CTX_TEXCOORDPOINTER);
    a->tstride = a3d_be32(g + W3D_CTX_TPSTRIDE);
    a->tvoff = a3d_be32(g + W3D_CTX_TPVOFFS);
    a->twoff = a3d_be32(g + W3D_CTX_TPWOFFS);
    a->tflags = a3d_be32(g + W3D_CTX_TPFLAGS);
    a->cp = a3d_be32(g + W3D_CTX_COLORPOINTER);
    a->cstride = a3d_be32(g + W3D_CTX_CPSTRIDE);
    a->cmode = a3d_be32(g + W3D_CTX_CPMODE);
    a3d_arrays_plan(a);
}

/* the colour components into a vertex, by the array's mode */
static void a3d_color_set(a3d_vtx *o, uint32_t m, const float ch[4])
{
    switch (m)
    {
    case W3D_CMODE_BGR:  o->r = ch[2]; o->g = ch[1]; o->b = ch[0]; o->a = 1.0f; break;
    case W3D_CMODE_RGB:  o->r = ch[0]; o->g = ch[1]; o->b = ch[2]; o->a = 1.0f; break;
    case W3D_CMODE_ARGB: o->a = ch[0]; o->r = ch[1]; o->g = ch[2]; o->b = ch[3]; break;
    case W3D_CMODE_BGRA: o->b = ch[0]; o->g = ch[1]; o->r = ch[2]; o->a = ch[3]; break;
    case W3D_CMODE_RGBA:
    default:             o->r = ch[0]; o->g = ch[1]; o->b = ch[2]; o->a = ch[3]; break;
    }
}

static void a3d_fetch_rec(const a3d_dc *d, const a3d_arrays *a, const uint8_t *p, a3d_vtx *o);

static int a3d_fetch_fast(const a3d_dc *d, const a3d_arrays *a, uint32_t va, a3d_vtx *o)
{
    const uint32_t base = va + (uint32_t)a->lo;
    if ((a->lo < 0 && base > va) || !a3d_g_ok(base, a->span, A3D_GR_READ))
        return 0;
    const uint8_t *p = a3d_g_ptr(base);
    a3d_fetch_rec(d, a, p, o);
    return 1;
}

static void a3d_fetch_rec(const a3d_dc *d, const a3d_arrays *a, const uint8_t *p, a3d_vtx *o)
{
    o->x = a3d_bef32(p + a->ox);
    o->y = a3d_bef32(p + a->ox + 4u);
    o->z = a3d_bef32(p + a->ox + 8u);
    float u = 0.0f, v = 0.0f, w = 1.0f;
    if (a->tp)
    {
        u = a3d_bef32(p + a->ou);
        v = a3d_bef32(p + a->ov);
        if (a->twoff)
            w = a3d_bef32(p + a->ow);
        if (!(a->tflags & W3D_TEXCOORD_NORMALIZED))
        {
            u /= d->texw;
            v /= d->texh;
        }
    }
    if (a->cp)
    {
        const uint32_t m = a->cmode & 0x1Fu;
        const uint32_t n = (m == W3D_CMODE_RGB || m == W3D_CMODE_BGR) ? 3u : 4u;
        float ch[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        for (uint32_t k = 0; k < n; k++)
            ch[k] = a3d_bef32(p + a->oc + 4u * k);
        a3d_color_set(o, m, ch);
    }
    else
    {
        o->r = d->cur[0]; o->g = d->cur[1]; o->b = d->cur[2]; o->a = d->cur[3];
    }
    a3d_finish_vtx(d, o, u, v, w);
}

static int a3d_fetch_array(const a3d_dc *d, const a3d_arrays *a, uint32_t i, a3d_vtx *o)
{
    if (i > 0x00FFFFFFu || a->vstride > 0x10000u)
        return 0;
    /* position */
    const uint32_t vsz = a->vmode == W3D_VERTEX_F_F_F ? 12u : (a->vmode == W3D_VERTEX_F_F_D ? 16u : 24u);
    if (a->vmode > W3D_VERTEX_D_D_D)
        return 0;
    const uint32_t va = a->vp + i * a->vstride;
    if (va < a->vp && i)
        return 0;
    if (a->fast && a3d_fetch_fast(d, a, va, o))
        return 1;
    if (!a3d_g_ok(va, vsz, A3D_GR_READ))
        return 0;
    const uint8_t *p = a3d_g_ptr(va);
    if (a->vmode == W3D_VERTEX_D_D_D)
    {
        o->x = (float)a3d_bef64(p);
        o->y = (float)a3d_bef64(p + 8);
        o->z = (float)a3d_bef64(p + 16);
    }
    else
    {
        o->x = a3d_bef32(p);
        o->y = a3d_bef32(p + 4);
        o->z = a->vmode == W3D_VERTEX_F_F_D ? (float)a3d_bef64(p + 8) : a3d_bef32(p + 8);
    }
    /* texture coordinates and w */
    float u = 0.0f, v = 0.0f, w = 1.0f;
    if (a->tp)
    {
        if (a->tstride > 0x10000u)
            return 0;
        const uint32_t ta = a->tp + i * a->tstride;
        const int32_t vo = (int32_t)a->tvoff, wo = (int32_t)a->twoff;   /* bytes from u, may be negative */
        if (!a3d_g_ok(ta, 4, A3D_GR_READ) || !a3d_g_ok(ta + (uint32_t)vo, 4, A3D_GR_READ))
            return 0;
        u = a3d_bef32(a3d_g_ptr(ta));
        v = a3d_bef32(a3d_g_ptr(ta + (uint32_t)vo));
        if (wo && a3d_g_ok(ta + (uint32_t)wo, 4, A3D_GR_READ))
        {
            w = a3d_bef32(a3d_g_ptr(ta + (uint32_t)wo));
        }
        if (!(a->tflags & W3D_TEXCOORD_NORMALIZED))
        {
            u /= d->texw;
            v /= d->texh;
        }
    }
    /* colour */
    if (a->cp)
    {
        if (a->cstride > 0x10000u)
            return 0;
        const uint32_t ca = a->cp + i * a->cstride;
        const uint32_t m = a->cmode & 0x1Fu;
        const uint32_t n = (m == W3D_CMODE_RGB || m == W3D_CMODE_BGR) ? 3u : 4u;
        float ch[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (a->cmode & W3D_COLOR_FLOAT)
        {
            if (!a3d_g_ok(ca, 4u * n, A3D_GR_READ))
                return 0;
            for (uint32_t k = 0; k < n; k++)
                ch[k] = a3d_bef32(a3d_g_ptr(ca + 4u * k));
        }
        else
        {
            if (!a3d_g_ok(ca, n, A3D_GR_READ))
                return 0;
            for (uint32_t k = 0; k < n; k++)
                ch[k] = (float)a3d_g_ptr(ca)[k] * (1.0f / 255.0f);
        }
        a3d_color_set(o, m, ch);
    }
    else
    {
        o->r = d->cur[0]; o->g = d->cur[1]; o->b = d->cur[2]; o->a = d->cur[3];
    }
    a3d_finish_vtx(d, o, u, v, w);
    return 1;
}

/* a small direct-mapped cache: each shared vertex of an indexed draw is converted once */
#define A3D_VC 256u
typedef struct a3d_vcache { uint32_t tag[A3D_VC]; a3d_vtx v[A3D_VC]; } a3d_vcache;
static a3d_vcache g_vc;

static int a3d_vertex(const a3d_dc *d, const a3d_arrays *a, uint32_t i, a3d_vtx *o)
{
    const uint32_t s = i & (A3D_VC - 1u);
    if (g_vc.tag[s] == i + 1u)
    {
        *o = g_vc.v[s];
        return 1;
    }
    if (!a3d_fetch_array(d, a, i, o))
        return 0;
    g_vc.tag[s] = i + 1u;
    g_vc.v[s] = *o;
    return 1;
}

#ifdef A3D_HEAP_LIB68K
#define A3D_VP_MAX 512u         /* the 68k library's copy of the engine: 24 KB of BSS                    */
#else
#define A3D_VP_MAX 4096u
#endif
typedef struct a3d_pv
{
    float x, y;                 /* the cross product's operands (a3d_emit)                                */
    int32_t kx, ky;             /* their order keys (the guard band, the bounds: a3d_fint.h a3d_fkey)     */
    uint32_t tv[6];             /* the vertex's six TRIANGLE_VERTS words (a3d_tv_words)                   */
    uint32_t ok;                /* x, y, z and w finite -- else a3d_emit drops the triangle               */
    uint32_t pad;
} a3d_pv;                       /* 48 bytes */
static a3d_pv g_pv[A3D_VP_MAX];
uint32_t a3d_vp_off;
static uint32_t g_pv_mark[A3D_VP_MAX / 32u];    /* the vertices of [lo, hi] an index names */

/* per draw: what a3d_emit and a3d_bin_tv derive from the target for every triangle */
typedef struct a3d_pvk
{
    int32_t h16;                        /* 16 x the target's height (the Sn.4 row mirror)                 */
    int32_t kgx0, kgx1, kgy0, kgy1;     /* the guard band's order keys                                     */
    int32_t kw, kh, kh1;                /* the keys of the width, the height and the height - 1            */
} a3d_pvk;

static void a3d_pvk_make(const a3d_dc *d, a3d_pvk *k)
{
    const a3d_ctx *c = d->c;
    const float gx0 = 0.5f * (float)c->tgt.width - A3D_GUARD_HALF, gx1 = gx0 + 2.0f * A3D_GUARD_HALF;
    const float gy0 = 0.5f * (float)c->tgt.height - A3D_GUARD_HALF, gy1 = gy0 + 2.0f * A3D_GUARD_HALF;
    k->h16 = (int32_t)(16u * c->tgt.height);
    k->kgx0 = a3d_fkey(gx0);
    k->kgx1 = a3d_fkey(gx1);
    k->kgy0 = a3d_fkey(gy0);
    k->kgy1 = a3d_fkey(gy1);
    k->kw = a3d_fkey((float)c->tgt.width);
    k->kh = a3d_fkey((float)c->tgt.height);
    k->kh1 = a3d_fkey((float)(c->tgt.height - 1u));
}

/* one fetched vertex, packed: a3d_emit's flat colour and finite test, a3d_bin_tv's keys and words */
static void a3d_pv_make(const a3d_dc *d, const a3d_pvk *k, a3d_vtx *v, a3d_pv *o)
{
    if (!d->gouraud)
    {
        v->r = d->cur[0]; v->g = d->cur[1]; v->b = d->cur[2]; v->a = d->cur[3];
    }
    o->x = v->x;
    o->y = v->y;
    o->ok = a3d_finite(v->x) && a3d_finite(v->y) && a3d_finite(v->z) && a3d_finite(v->w);
    o->kx = a3d_fkey(v->x);
    o->ky = a3d_fkey(v->y);
    a3d_tv_words(d, v, k->h16, o->tv);
    o->pad = 0;
}

typedef struct a3d_pvb
{
    int32_t kminx, kmaxx, kminy, kmaxy;
    const a3d_pv *ymin, *ymax;
} a3d_pvb;

static void a3d_bin_pv(a3d_dc *d, const a3d_pvk *k, const a3d_pv *p0, const a3d_pv *p1, const a3d_pv *p2,
                       const a3d_pvb *b)
{
    const int32_t kminy = b->kminy, kmaxy = b->kmaxy, kminx = b->kminx, kmaxx = b->kmaxx;
    if (kmaxy < 0 || kminy >= k->kh || kmaxx < 0 || kminx >= k->kw)
        return;                                 /* entirely off the target */
    const uint32_t y0 = kminy <= 0 ? 0u : (uint32_t)b->ymin->y;
    const uint32_t y1 = kmaxy >= k->kh1 ? d->c->tgt.height - 1u : (uint32_t)b->ymax->y;
    uint32_t w[A3D_TV_WORDS];
    __builtin_memcpy(&w[0], p0->tv, sizeof(p0->tv));
    __builtin_memcpy(&w[6], p1->tv, sizeof(p1->tv));
    __builtin_memcpy(&w[12], p2->tv, sizeof(p2->tv));
    a3d_bin_tail(d, y0, y1, w);
}

typedef struct a3d_vsrc
{
    const a3d_arrays *a;        /* an array draw; NULL: W3D_Vertex structures                                   */
    uint32_t vp, byptr;
} a3d_vsrc;

static int a3d_vsrc_fetch(const a3d_dc *d, const a3d_vsrc *src, uint32_t i, a3d_vtx *o)
{
    if (src->a)
        return a3d_fetch_array(d, src->a, i, o);
    const uint32_t va = src->byptr ? a3d_be32(a3d_g_ptr(src->vp + 4u * i)) : src->vp + i * W3D_V_SIZE;
    return a3d_fetch_w3dv(d, va, o);
}

static void a3d_emit_pv(a3d_dc *d, const a3d_pvk *k, const a3d_vsrc *src, uint32_t lo, uint32_t i0, uint32_t i1,
                        uint32_t i2)
{
    const a3d_pv *p0 = &g_pv[i0 - lo], *p1 = &g_pv[i1 - lo], *p2 = &g_pv[i2 - lo];
    a3d_cnt.tris_in++;
    if (!(p0->ok && p1->ok && p2->ok))
        return;                                 /* NaN or infinite: drop it (a3d_emit) */
    const float cross = (p1->x - p0->x) * (p2->y - p0->y) - (p2->x - p0->x) * (p1->y - p0->y);
    if (a3d_fzero(cross))
    {
        a3d_cnt.tris_culled++;
        return;
    }
    const int ccw = a3d_fneg(cross);            /* Wazp3D's rule: counter-clockwise on screen <=> cross < 0 */
    if (d->cull && (d->front_ccw ? !ccw : ccw))
    {
        a3d_cnt.tris_culled++;
        return;
    }
    a3d_pvb b;
    b.kminx = b.kmaxx = p0->kx;
    b.kminy = b.kmaxy = p0->ky;
    b.ymin = b.ymax = p0;
    if (p1->ky < b.kminy) { b.kminy = p1->ky; b.ymin = p1; }
    if (p1->ky > b.kmaxy) { b.kmaxy = p1->ky; b.ymax = p1; }
    if (p1->kx < b.kminx) b.kminx = p1->kx;
    if (p1->kx > b.kmaxx) b.kmaxx = p1->kx;
    if (p2->ky < b.kminy) { b.kminy = p2->ky; b.ymin = p2; }
    if (p2->ky > b.kmaxy) { b.kmaxy = p2->ky; b.ymax = p2; }
    if (p2->kx < b.kminx) b.kminx = p2->kx;
    if (p2->kx > b.kmaxx) b.kmaxx = p2->kx;
    if (b.kminx < k->kgx0 || b.kmaxx > k->kgx1 || b.kminy < k->kgy0 || b.kmaxy > k->kgy1)
    {
        /* the guard-band clip interpolates every attribute: the reference's vertices, from the reference fetch */
        a3d_vtx v[3];
        const uint32_t ix[3] = { i0, i1, i2 };
        for (uint32_t j = 0; j < 3u; j++)
        {
            if (!a3d_vsrc_fetch(d, src, ix[j], &v[j]))
                return;                         /* cannot happen: the vertex pass fetched it */
            if (!d->gouraud)
            {
                v[j].r = d->cur[0]; v[j].g = d->cur[1]; v[j].b = d->cur[2]; v[j].a = d->cur[3];
            }
        }
        a3d_emit_clip(d, v);
        return;
    }
    a3d_bin_pv(d, k, p0, p1, p2, &b);
}

static int32_t a3d_strip_pv(a3d_dc *d, uint32_t vp, uint32_t n, uint32_t byptr, uint32_t fan)
{
    if (a3d_vp_off || n < 3u || n > A3D_VP_MAX)
        return -1;
    a3d_pvk k;
    a3d_pvk_make(d, &k);
    const a3d_vsrc src = { 0, vp, byptr };
    for (uint32_t j = 0; j < n; j++)
    {
        a3d_vtx v;
        if (!a3d_vsrc_fetch(d, &src, j, &v))
        {
            a3d_cnt.vp_refused++;
            return -1;
        }
        a3d_pv_make(d, &k, &v, &g_pv[j]);
    }
    a3d_cnt.vp_draws++;
    a3d_cnt.vp_verts += n;
    for (uint32_t q = 0; q + 2u < n; q++)
    {
        /* fan: (0, q+1, q+2); strip: (q, q+1, q+2), the odd ones swapped to keep the winding (a3d_draw's loop) */
        const uint32_t i0 = fan ? 0u : q, i1 = q + 1u, i2 = q + 2u;
        if (!fan && (q & 1u))
            a3d_emit_pv(d, &k, &src, 0, i1, i0, i2);
        else
            a3d_emit_pv(d, &k, &src, 0, i0, i1, i2);
    }
    return W3D_SUCCESS;
}

#if (defined(__ARM_NEON) || defined(__ARM_NEON__))
#define A3D_PACK_NEON 1
#endif
#if !defined(__arm__) && !defined(A3D_HEAP_LIB68K)
#define A3D_PACK_SELFTEST 1
#endif
#if defined(A3D_PACK_NEON) || defined(A3D_PACK_SELFTEST)
#define A3D_PACK_BATCH 1
#endif
#ifdef A3D_PACK_BATCH
#define A3D_VB 64u
typedef struct a3d_vbatch
{
    uint32_t x[A3D_VB], y[A3D_VB], z[A3D_VB], u[A3D_VB], v[A3D_VB], w[A3D_VB];
    uint32_t ud[A3D_VB], vd[A3D_VB];    /* u / texw, v / texh (the VFP's division) when not normalised     */
    uint32_t c[4][A3D_VB];              /* the colour components, in the record's order (COLOR_FLOAT bits)  */
    uint32_t slot[A3D_VB + 4u];         /* the g_pv index of each (+4: a partial last step reads past n)    */
} a3d_vbatch;
static a3d_vbatch g_vb __attribute__((aligned(16)));

#define A3D_VC_CONST 4u
typedef struct a3d_vcfg
{
    uint32_t tp, twoff, norm;           /* texture coordinates read; w read; already normalised             */
    uint32_t ncol;                      /* colour components read (0: none -- the current colour, or flat)  */
    uint32_t col[4];                    /* r, g, b, a: a column of the batch, or A3D_VC_CONST (cst)         */
    uint32_t cst[4];                    /* their bits when constant                                          */
} a3d_vcfg;

static uint32_t a3d_fbu(float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, 4);
    return u;
}

static float a3d_ubf(uint32_t u)
{
    float f;
    __builtin_memcpy(&f, &u, 4);
    return f;
}

static void a3d_vcfg_make(const a3d_dc *d, const a3d_arrays *a, a3d_vcfg *g)
{
    g->tp = a->tp != 0;
    g->twoff = a->tp && a->twoff;
    g->norm = (a->tflags & W3D_TEXCOORD_NORMALIZED) != 0;
    g->ncol = 0;
    for (uint32_t k = 0; k < 4u; k++)
    {
        g->col[k] = A3D_VC_CONST;
        g->cst[k] = a3d_fbu(d->cur[k]);         /* no colour array: the current colour (a3d_fetch_rec) */
    }
    if (!d->gouraud || !a->cp)
        return;                                 /* flat: a3d_pv_make overrides with the current colour anyway */
    const uint32_t m = a->cmode & 0x1Fu;
    g->ncol = (m == W3D_CMODE_RGB || m == W3D_CMODE_BGR) ? 3u : 4u;
    switch (m)                                  /* a3d_color_set, as columns */
    {
    case W3D_CMODE_BGR:  g->col[0] = 2; g->col[1] = 1; g->col[2] = 0; break;
    case W3D_CMODE_RGB:  g->col[0] = 0; g->col[1] = 1; g->col[2] = 2; break;
    case W3D_CMODE_ARGB: g->col[3] = 0; g->col[0] = 1; g->col[1] = 2; g->col[2] = 3; break;
    case W3D_CMODE_BGRA: g->col[2] = 0; g->col[1] = 1; g->col[0] = 2; g->col[3] = 3; break;
    case W3D_CMODE_RGBA:
    default:             g->col[0] = 0; g->col[1] = 1; g->col[2] = 2; g->col[3] = 3; break;
    }
    if (g->ncol == 3u)
        g->cst[3] = 0x3F800000u;                /* RGB / BGR: alpha 1.0 (a3d_color_set) */
}

static void a3d_pack_ref(const a3d_dc *d, const a3d_pvk *k, const a3d_vcfg *g, const a3d_vbatch *b, uint32_t i,
                         a3d_pv *o)
{
    a3d_vtx v;
    v.x = a3d_ubf(b->x[i]);
    v.y = a3d_ubf(b->y[i]);
    v.z = a3d_ubf(b->z[i]);
    float u = 0.0f, vv = 0.0f, w = 1.0f;
    if (g->tp)
    {
        u = a3d_ubf(b->u[i]);
        vv = a3d_ubf(b->v[i]);
        if (g->twoff)
            w = a3d_ubf(b->w[i]);
        if (!g->norm)
        {
            u /= d->texw;
            vv /= d->texh;
        }
    }
    v.r = a3d_ubf(g->col[0] < A3D_VC_CONST ? b->c[g->col[0]][i] : g->cst[0]);
    v.g = a3d_ubf(g->col[1] < A3D_VC_CONST ? b->c[g->col[1]][i] : g->cst[1]);
    v.b = a3d_ubf(g->col[2] < A3D_VC_CONST ? b->c[g->col[2]][i] : g->cst[2]);
    v.a = a3d_ubf(g->col[3] < A3D_VC_CONST ? b->c[g->col[3]][i] : g->cst[3]);
    a3d_finish_vtx(d, &v, u, vv, w);
    a3d_pv_make(d, k, &v, o);
}

#ifdef A3D_PACK_NEON
#include <arm_neon.h>
static inline uint32x4_t a3d_v_finite(uint32x4_t b)
{
    return vcltq_u32(vandq_u32(b, vdupq_n_u32(0x7FFFFFFFu)), vdupq_n_u32(0x60AD78ECu));
}
static inline uint32x4_t a3d_v_fzero(uint32x4_t b)
{
    return vceqq_u32(vandq_u32(b, vdupq_n_u32(0x7FFFFFFFu)), vdupq_n_u32(0u));
}
static inline uint32x4_t a3d_v_fkey(uint32x4_t b)
{
    const int32x4_t i = vreinterpretq_s32_u32(b);
    const int32x4_t m = vshrq_n_s32(i, 31);
    return vreinterpretq_u32_s32(vsubq_s32(veorq_s32(vandq_s32(i, vdupq_n_s32(0x7FFFFFFF)), m), m));
}
/* "(int32_t)u <= 0 || u > 0x7F800000": +0, a sign bit (negatives, -0, negative NaNs), a positive NaN */
static inline uint32x4_t a3d_v_zero_or_nan(uint32x4_t b)
{
    return vorrq_u32(vcleq_s32(vreinterpretq_s32_u32(b), vdupq_n_s32(0)), vcgtq_u32(b, vdupq_n_u32(0x7F800000u)));
}
static inline uint32x4_t a3d_v_sn4i(uint32x4_t vb)
{
    const float32x4_t p = vmulq_f32(vreinterpretq_f32_u32(vb), vdupq_n_f32(16.0f));
    const uint32x4_t u = vreinterpretq_u32_f32(p);
    const uint32x4_t nan = vcgtq_u32(vandq_u32(u, vdupq_n_u32(0x7FFFFFFFu)), vdupq_n_u32(0x7F800000u));
    const uint32x4_t hi = vcgeq_s32(vreinterpretq_s32_u32(u), vdupq_n_s32(0x46FFFE00));
    const uint32x4_t lo = vcgeq_u32(u, vdupq_n_u32(0xC7000000u));
    const uint32x4_t neg = vcltq_s32(vreinterpretq_s32_u32(u), vdupq_n_s32(0));
    const float32x4_t r = vbslq_f32(neg, vsubq_f32(p, vdupq_n_f32(0.5f)), vaddq_f32(p, vdupq_n_f32(0.5f)));
    uint32x4_t res = vreinterpretq_u32_s32(vcvtq_s32_f32(r));
    res = vbslq_u32(lo, vdupq_n_u32(0xFFFF8000u), res);        /* -32768 */
    res = vbslq_u32(hi, vdupq_n_u32(32767u), res);
    return vbslq_u32(nan, vdupq_n_u32(0xFFFF8000u), res);       /* precedence: NaN, then hi, then lo (a3d_sn4i) */
}
static inline uint32x4_t a3d_v_z30i(uint32x4_t zb)
{
    uint32x4_t r = vcvtq_u32_f32(vmulq_f32(vreinterpretq_f32_u32(zb), vdupq_n_f32(1073741824.0f)));
    r = vbslq_u32(vcgeq_u32(zb, vdupq_n_u32(0x3F800000u)), vdupq_n_u32(0x40000000u), r);
    return vbslq_u32(a3d_v_zero_or_nan(zb), vdupq_n_u32(0u), r);
}
static inline uint32x4_t a3d_v_u8i(uint32x4_t cb)
{
    uint32x4_t r = vcvtq_u32_f32(vaddq_f32(vmulq_f32(vreinterpretq_f32_u32(cb), vdupq_n_f32(255.0f)), vdupq_n_f32(0.5f)));
    r = vbslq_u32(vcgeq_u32(cb, vdupq_n_u32(0x3F800000u)), vdupq_n_u32(255u), r);
    return vbslq_u32(a3d_v_zero_or_nan(cb), vdupq_n_u32(0u), r);
}

static inline uint32x4_t a3d_v_col(const a3d_vcfg *g, const a3d_vbatch *b, uint32_t q, uint32_t i)
{
    return g->col[q] < A3D_VC_CONST ? vld1q_u32(&b->c[g->col[q]][i]) : vdupq_n_u32(g->cst[q]);
}

#if !(defined(__arm__) && !defined(__aarch64__))
static void a3d_pack4(const a3d_dc *d, const a3d_pvk *k, const a3d_vcfg *g, const a3d_vbatch *b, uint32_t i,
                      uint32_t out[11][4], uint32_t *redo)
{
    /* x, y: the cross product's operands, their order keys, Sn.4 -> TRIANGLE_VERTS word 0 */
    const uint32x4_t X = vld1q_u32(&b->x[i]), Y = vld1q_u32(&b->y[i]);
    vst1q_u32(out[0], X);
    vst1q_u32(out[1], Y);
    vst1q_u32(out[2], a3d_v_fkey(X));
    vst1q_u32(out[3], a3d_v_fkey(Y));
    {
        const uint32x4_t M16 = vdupq_n_u32(0xFFFFu);
        const uint32x4_t SX = a3d_v_sn4i(X), SY = a3d_v_sn4i(Y);
        vst1q_u32(out[4], vorrq_u32(vshlq_n_u32(vandq_u32(vsubq_u32(vdupq_n_u32((uint32_t)k->h16), SY), M16), 16),
                                    vandq_u32(SX, M16)));
    }
    uint32x4_t ok = vandq_u32(a3d_v_finite(X), a3d_v_finite(Y));
    /* z: 2.30 */
    {
        const uint32x4_t Z = vld1q_u32(&b->z[i]);
        ok = vandq_u32(ok, a3d_v_finite(Z));
        vst1q_u32(out[5], a3d_v_z30i(Z));
    }
    const uint32x4_t ONE = vdupq_n_u32(0x3F800000u);
    uint32x4_t W = ONE;
    if (d->persp && g->tp && g->twoff)
    {
        const uint32x4_t w = vld1q_u32(&b->w[i]);
        const uint32x4_t wok = vandq_u32(vcgtq_s32(vreinterpretq_s32_u32(w), vdupq_n_s32(0)),
                                         vcltq_u32(w, vdupq_n_u32(0x7149F2CAu)));
        W = vbslq_u32(wok, w, ONE);
    }
    ok = vandq_u32(ok, a3d_v_finite(W));
    vst1q_u32(out[10], vandq_u32(ok, vdupq_n_u32(1u)));
    const uint32x4_t TV2 = vreinterpretq_u32_f32(vmulq_f32(vreinterpretq_f32_u32(W), vdupq_n_f32(d->wscale)));
    vst1q_u32(out[6], TV2);
    uint32x4_t bad = a3d_v_fzero(TV2);
    /* s, t: u * w and v * w, zero when they are not finite; s = 0 where u * w is zero and u is not */
    if (d->textured)
    {
        const uint32x4_t ZERO = vdupq_n_u32(0u);
        uint32x4_t U = ZERO, V = ZERO;
        if (g->tp)
        {
            U = vld1q_u32(g->norm ? &b->u[i] : &b->ud[i]);
            V = vld1q_u32(g->norm ? &b->v[i] : &b->vd[i]);
        }
        U = vandq_u32(U, a3d_v_finite(U));      /* !finite(u) -> +0 */
        V = vandq_u32(V, a3d_v_finite(V));
        const uint32x4_t SW = vreinterpretq_u32_f32(vmulq_f32(vreinterpretq_f32_u32(U), vreinterpretq_f32_u32(W)));
        const uint32x4_t TW = vreinterpretq_u32_f32(vmulq_f32(vreinterpretq_f32_u32(V), vreinterpretq_f32_u32(W)));
        const uint32x4_t unz = vmvnq_u32(a3d_v_fzero(U)), vnz = vmvnq_u32(a3d_v_fzero(V));
        const uint32x4_t swz = vorrq_u32(a3d_v_fzero(SW), vmvnq_u32(a3d_v_finite(SW)));
        const uint32x4_t twz = vorrq_u32(a3d_v_fzero(TW), vmvnq_u32(a3d_v_finite(TW)));
        vst1q_u32(out[7], vbicq_u32(U, vandq_u32(swz, unz)));
        vst1q_u32(out[8], vbicq_u32(V, vandq_u32(twz, vnz)));
        bad = vorrq_u32(bad, vorrq_u32(vandq_u32(a3d_v_fzero(SW), unz), vandq_u32(a3d_v_fzero(TW), vnz)));
    }
    else
    {
        vst1q_u32(out[7], vdupq_n_u32(0u));
        vst1q_u32(out[8], vdupq_n_u32(0u));
    }
    /* the colour bytes, r g b a from the most significant */
    {
        uint32x4_t tv5 = vshlq_n_u32(a3d_v_u8i(a3d_v_col(g, b, 0u, i)), 24);
        tv5 = vorrq_u32(tv5, vshlq_n_u32(a3d_v_u8i(a3d_v_col(g, b, 1u, i)), 16));
        tv5 = vorrq_u32(tv5, vshlq_n_u32(a3d_v_u8i(a3d_v_col(g, b, 2u, i)), 8));
        vst1q_u32(out[9], vorrq_u32(tv5, a3d_v_u8i(a3d_v_col(g, b, 3u, i))));
    }
    uint32_t bb[4];
    vst1q_u32(bb, bad);
    *redo = (bb[0] ? 1u : 0u) | (bb[1] ? 2u : 0u) | (bb[2] ? 4u : 0u) | (bb[3] ? 8u : 0u);
}
#endif
#endif

#if defined(A3D_PACK_NEON) && defined(__arm__) && !defined(__aarch64__)
#define A3D_PACK4_ASM 1
typedef struct a3d_pk4p
{
    uint32_t h16;               /*   0: 16 x the target's height                                    */
    uint32_t wscale;            /*   4: 1/w's scale, the float's bits                               */
    uint32_t wmask;             /*   8: ~0 when w is read (perspective, texture coordinates, a w)   */
    uint32_t tpmask;            /*  12: ~0 when u and v are read                                    */
    uint32_t texmask;           /*  16: ~0 when the draw is textured                                */
    uint32_t uoff, voff;        /*  20, 24: the u / v columns' byte offsets from x (u or ud, v or vd) */
    uint32_t pad;               /*  28                                                              */
    uint32_t coff[4];           /*  32: the colour columns' byte offsets from x (r, g, b, a)        */
    uint32_t cmask[4];          /*  48: ~0 when the colour is read, 0 when constant                 */
    uint32_t cst[4][4];         /*  64: the constant colours, four lanes each                       */
    uint32_t bad[4];            /* 128: out -- the lanes the reference must redo                    */
} a3d_pk4p;
#define A3D_OFS(t, f) ((uint32_t)__builtin_offsetof(t, f))
typedef char a3d_pk4p_layout[(A3D_OFS(a3d_pk4p, wscale) == 4u && A3D_OFS(a3d_pk4p, wmask) == 8u
                              && A3D_OFS(a3d_pk4p, tpmask) == 12u && A3D_OFS(a3d_pk4p, texmask) == 16u
                              && A3D_OFS(a3d_pk4p, uoff) == 20u && A3D_OFS(a3d_pk4p, voff) == 24u
                              && A3D_OFS(a3d_pk4p, coff) == 32u && A3D_OFS(a3d_pk4p, cmask) == 48u
                              && A3D_OFS(a3d_pk4p, cst) == 64u && A3D_OFS(a3d_pk4p, bad) == 128u
                              && A3D_OFS(a3d_vbatch, y) == 256u && A3D_OFS(a3d_vbatch, z) == 512u
                              && A3D_OFS(a3d_vbatch, w) == 1280u) ? 1 : -1];

/* a3d_v_sn4i of qV into q11 (q2, q3, q8-q10 scratch; q13 = 0x7F800000, q15 = 0x7FFFFFFF) */
#define A3D_PK_SN4I(QV)                                                                                         \
    "vmov.f32  q8, #16.0\n\t"                                                                                   \
    "vmul.f32  q8, " QV ", q8\n\t"                            \
    "vand      q9, q8, q15\n\t"                                                                                 \
    "vcgt.u32  q9, q9, q13\n\t"             /* nan: |p| > inf                               */                  \
    "vmov.f32  q10, #0.5\n\t"                                                                                   \
    "vclt.s32  q11, q8, #0\n\t"             /* neg                                          */                  \
    "vsub.f32  q2, q8, q10\n\t"                                                                                 \
    "vadd.f32  q10, q8, q10\n\t"                                                                                \
    "vbsl      q11, q2, q10\n\t"            /* neg ? p - 0.5 : p + 0.5                      */                  \
    "vcvt.s32.f32 q11, q11\n\t"                                                                                 \
    "vmov.i32  q2, #0xC7000000\n\t"                                                                             \
    "vcge.u32  q2, q8, q2\n\t"              /* lo                                           */                  \
    "vmvn.i32  q10, #0x7FFF\n\t"            /* 0xFFFF8000 = -32768                          */                  \
    "vbit      q11, q10, q2\n\t"                                                                                \
    "movw      %[t0], #0xFE00\n\t"                                                                              \
    "movt      %[t0], #0x46FF\n\t"                                                                              \
    "vdup.32   q3, %[t0]\n\t"                                                                                   \
    "vcge.s32  q3, q8, q3\n\t"              /* hi                                           */                  \
    "vmov.i32  q2, #0x7FFF\n\t"                                                                                 \
    "vbit      q11, q2, q3\n\t"             /* hi -> 32767                                  */                  \
    "vbit      q11, q10, q9\n\t"            /* nan -> -32768 (last: NaN, then hi, then lo)  */

/* a3d_v_u8i of colour Q (0..3) -- the column or the constant -- shifted and ORed into q14 */
#define A3D_PK_U8I(Q, SHIFT_OR)                                                                                 \
    "ldr       %[t0], [%[p], #(32 + 4 * " #Q ")]\n\t"                                                           \
    "add       %[t0], %[x], %[t0]\n\t"                                                                          \
    "vld1.32   {d0-d1}, [%[t0]]\n\t"                                                                            \
    "ldr       %[t0], [%[p], #(48 + 4 * " #Q ")]\n\t"                                                           \
    "vdup.32   q3, %[t0]\n\t"                                                                                   \
    "add       %[t0], %[p], #(64 + 16 * " #Q ")\n\t"                                                            \
    "vld1.32   {d2-d3}, [%[t0]]\n\t"                                                                            \
    "vbsl      q3, q0, q1\n\t"                                \
    "vmul.f32  q1, q3, q8\n\t"                                                                                  \
    "vadd.f32  q1, q1, q9\n\t"                                                                                  \
    "vcvt.u32.f32 q1, q1\n\t"               /* c * 255 + 0.5, truncated                     */                  \
    "vcge.u32  q2, q3, q10\n\t"                                                                                 \
    "vbit      q1, q11, q2\n\t"             /* >= 1.0 -> 255                                */                  \
    "vcle.s32  q2, q3, #0\n\t"                                                                                  \
    "vcgt.u32  q0, q3, q13\n\t"                                                                                 \
    "vorr      q2, q2, q0\n\t"                                                                                  \
    "vbic      q1, q1, q2\n\t"              /* +0, negative, NaN -> 0                       */                  \
    SHIFT_OR

static void a3d_pack4_asm(const uint32_t *x, uint32_t out[11][4], a3d_pk4p *p)
{
    uint32_t t0, t1;
    __asm__ volatile(
        "vmvn.i32  q15, #0x80000000\n\t"        /* 0x7FFFFFFF                                   */
        "movw      %[t0], #0x0000\n\t"
        "movt      %[t0], #0x7F80\n\t"
        "vdup.32   q13, %[t0]\n\t"              /* +inf's bits                                  */
        "movw      %[t0], #0x78EC\n\t"
        "movt      %[t0], #0x60AD\n\t"
        "vdup.32   q14, %[t0]\n\t"              /* a3d_fint.h's finite limit (1e20)             */
        /* ---- x, y: the words, the order keys, Sn.4 -> TRIANGLE_VERTS word 0 ---- */
        "vld1.32   {d0-d1}, [%[x]]\n\t"         /* X                                            */
        "add       %[t0], %[x], #256\n\t"
        "vld1.32   {d2-d3}, [%[t0]]\n\t"        /* Y                                            */
        "mov       %[t1], %[out]\n\t"
        "vst1.32   {d0-d1}, [%[t1]]!\n\t"       /* out[0]                                       */
        "vst1.32   {d2-d3}, [%[t1]]!\n\t"       /* out[1]                                       */
        "vshr.s32  q2, q0, #31\n\t"
        "vand      q3, q0, q15\n\t"
        "veor      q3, q3, q2\n\t"
        "vsub.i32  q3, q3, q2\n\t"
        "vst1.32   {d6-d7}, [%[t1]]!\n\t"       /* out[2]: fkey(x)                              */
        "vshr.s32  q2, q1, #31\n\t"
        "vand      q3, q1, q15\n\t"
        "veor      q3, q3, q2\n\t"
        "vsub.i32  q3, q3, q2\n\t"
        "vst1.32   {d6-d7}, [%[t1]]!\n\t"       /* out[3]: fkey(y)                              */
        "vand      q2, q0, q15\n\t"
        "vcgt.u32  q2, q14, q2\n\t"
        "vand      q3, q1, q15\n\t"
        "vcgt.u32  q3, q14, q3\n\t"
        "vand      q12, q2, q3\n\t"
        A3D_PK_SN4I("q0")
        "vmov      q0, q11\n\t"
        A3D_PK_SN4I("q1")                       /* SY -> q11                                    */
        "ldr       %[t0], [%[p], #0]\n\t"
        "vdup.32   q2, %[t0]\n\t"
        "vsub.i32  q2, q2, q11\n\t"             /* h16 - SY                                     */
        "vmov.i32  q3, #0xFFFF\n\t"
        "vand      q2, q2, q3\n\t"
        "vshl.i32  q2, q2, #16\n\t"
        "vand      q0, q0, q3\n\t"
        "vorr      q2, q2, q0\n\t"
        "vst1.32   {d4-d5}, [%[t1]]!\n\t"       /* out[4]                                       */
        /* ---- z: 2.30 ---- */
        "add       %[t0], %[x], #512\n\t"
        "vld1.32   {d0-d1}, [%[t0]]\n\t"        /* Z                                            */
        "vand      q2, q0, q15\n\t"
        "vcgt.u32  q2, q14, q2\n\t"
        "vand      q12, q12, q2\n\t"            /* ok &= finite(z)                              */
        "vcle.s32  q3, q0, #0\n\t"
        "vcgt.u32  q2, q0, q13\n\t"
        "vorr      q3, q3, q2\n\t"              /* +0, negative or NaN                          */
        "movw      %[t0], #0x0000\n\t"
        "movt      %[t0], #0x4E80\n\t"
        "vdup.32   q8, %[t0]\n\t"               /* 2^30                                         */
        "vmul.f32  q8, q0, q8\n\t"
        "vcvt.u32.f32 q8, q8\n\t"
        "vmov.f32  q9, #1.0\n\t"
        "vcge.u32  q9, q0, q9\n\t"
        "vmov.i32  q10, #0x40000000\n\t"
        "vbit      q8, q10, q9\n\t"             /* z >= 1.0 -> 2^30                             */
        "vbic      q8, q8, q3\n\t"
        "vst1.32   {d16-d17}, [%[t1]]!\n\t"     /* out[5]                                       */
        /* ---- w: a3d_finish_vtx, the finite test, 1/w's word ---- */
        "add       %[t0], %[x], #1280\n\t"
        "vld1.32   {d0-d1}, [%[t0]]\n\t"        /* w as read (stale when not read: masked)      */
        "ldr       %[t0], [%[p], #8]\n\t"
        "vdup.32   q3, %[t0]\n\t"
        "vcgt.s32  q2, q0, #0\n\t"
        "vand      q2, q2, q3\n\t"
        "movw      %[t0], #0xF2CA\n\t"
        "movt      %[t0], #0x7149\n\t"
        "vdup.32   q3, %[t0]\n\t"
        "vcgt.u32  q3, q3, q0\n\t"
        "vand      q2, q2, q3\n\t"              /* read, positive, under 1e30                   */
        "vmov.f32  q1, #1.0\n\t"
        "vbsl      q2, q0, q1\n\t"
        "vand      q3, q2, q15\n\t"
        "vcgt.u32  q3, q14, q3\n\t"
        "vand      q12, q12, q3\n\t"            /* ok &= finite(w)                              */
        "vmov.i32  q3, #1\n\t"
        "vand      q3, q12, q3\n\t"
        "add       %[t0], %[out], #160\n\t"
        "vst1.32   {d6-d7}, [%[t0]]\n\t"        /* out[10]: ok                                  */
        "ldr       %[t0], [%[p], #4]\n\t"
        "vdup.32   q8, %[t0]\n\t"
        "vmul.f32  q8, q2, q8\n\t"              /* w * wscale                                   */
        "vst1.32   {d16-d17}, [%[t1]]!\n\t"     /* out[6]                                       */
        "vand      q12, q8, q15\n\t"
        "vceq.i32  q12, q12, #0\n\t"
        /* ---- s, t ---- */
        "ldr       %[t0], [%[p], #20]\n\t"
        "add       %[t0], %[x], %[t0]\n\t"
        "vld1.32   {d0-d1}, [%[t0]]\n\t"        /* u                                            */
        "ldr       %[t0], [%[p], #24]\n\t"
        "add       %[t0], %[x], %[t0]\n\t"
        "vld1.32   {d2-d3}, [%[t0]]\n\t"        /* v                                            */
        "ldr       %[t0], [%[p], #12]\n\t"
        "vdup.32   q3, %[t0]\n\t"
        "vand      q0, q0, q3\n\t"
        "vand      q1, q1, q3\n\t"              /* 0 when not read                              */
        "vand      q3, q0, q15\n\t"
        "vcgt.u32  q3, q14, q3\n\t"
        "vand      q0, q0, q3\n\t"              /* !finite(u) -> +0                             */
        "vand      q3, q1, q15\n\t"
        "vcgt.u32  q3, q14, q3\n\t"
        "vand      q1, q1, q3\n\t"
        "vmul.f32  q8, q0, q2\n\t"
        "vmul.f32  q9, q1, q2\n\t"
        "vtst.32   q10, q0, q15\n\t"            /* u != 0                                       */
        "vtst.32   q11, q1, q15\n\t"            /* v != 0                                       */
        "vtst.32   q2, q8, q15\n\t"             /* sw != 0                                      */
        "vand      q3, q8, q15\n\t"
        "vcgt.u32  q3, q14, q3\n\t"
        "vand      q3, q2, q3\n\t"              /* sw != 0 and finite                           */
        "vbic      q3, q10, q3\n\t"
        "vbic      q0, q0, q3\n\t"              /* s                                            */
        "vbic      q10, q10, q2\n\t"            /* sw == 0, u != 0: redo                        */
        "vtst.32   q2, q9, q15\n\t"
        "vand      q3, q9, q15\n\t"
        "vcgt.u32  q3, q14, q3\n\t"
        "vand      q3, q2, q3\n\t"
        "vbic      q3, q11, q3\n\t"
        "vbic      q1, q1, q3\n\t"              /* t                                            */
        "vbic      q11, q11, q2\n\t"
        "vorr      q10, q10, q11\n\t"
        "ldr       %[t0], [%[p], #16]\n\t"
        "vdup.32   q3, %[t0]\n\t"
        "vand      q0, q0, q3\n\t"
        "vand      q1, q1, q3\n\t"
        "vand      q10, q10, q3\n\t"            /* not textured: s = t = 0, no redo             */
        "vst1.32   {d0-d1}, [%[t1]]!\n\t"       /* out[7]                                       */
        "vst1.32   {d2-d3}, [%[t1]]!\n\t"       /* out[8]                                       */
        "vorr      q12, q12, q10\n\t"
        /* ---- the colour bytes ---- */
        "movw      %[t0], #0x0000\n\t"
        "movt      %[t0], #0x437F\n\t"
        "vdup.32   q8, %[t0]\n\t"               /* 255.0f                                       */
        "vmov.f32  q9, #0.5\n\t"
        "vmov.f32  q10, #1.0\n\t"
        "vmov.i32  q11, #255\n\t"
        A3D_PK_U8I(0, "vshl.i32  q14, q1, #24\n\t")
        A3D_PK_U8I(1, "vshl.i32  q1, q1, #16\n\tvorr      q14, q14, q1\n\t")
        A3D_PK_U8I(2, "vshl.i32  q1, q1, #8\n\tvorr      q14, q14, q1\n\t")
        A3D_PK_U8I(3, "vorr      q14, q14, q1\n\t")
        "vst1.32   {d28-d29}, [%[t1]]\n\t"      /* out[9]                                       */
        "add       %[t0], %[p], #128\n\t"
        "vst1.32   {d24-d25}, [%[t0]]\n\t"      /* bad                                          */
        : [t0] "=&r"(t0), [t1] "=&r"(t1)
        : [x] "r"(x), [out] "r"(out), [p] "r"(p)
        : "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d16", "d17", "d18", "d19", "d20", "d21", "d22",
          "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
}
#undef A3D_PK_SN4I
#undef A3D_PK_U8I
#endif

uint32_t a3d_pack_neg;

/* the batch's n vertices into g_pv */
static void a3d_pack_batch(const a3d_dc *d, const a3d_pvk *k, const a3d_vcfg *g, a3d_vbatch *b, uint32_t n)
{
    if (g->tp && !g->norm)
        for (uint32_t i = 0; i < n; i++)
        {
            /* a3d_fetch_rec's division, on the VFP (NEON has none) -- the reference divides again for itself */
            b->ud[i] = a3d_fbu(a3d_ubf(b->u[i]) / d->texw);
            b->vd[i] = a3d_fbu(a3d_ubf(b->v[i]) / d->texh);
        }
#ifdef A3D_PACK_NEON
    uint32_t out[11][4] __attribute__((aligned(16)));
#ifdef A3D_PACK4_ASM
    a3d_pk4p pk __attribute__((aligned(16)));
    pk.h16 = (uint32_t)k->h16;
    pk.wscale = a3d_fbu(d->wscale);
    pk.wmask = (d->persp && g->tp && g->twoff) ? 0xFFFFFFFFu : 0u;
    pk.tpmask = g->tp ? 0xFFFFFFFFu : 0u;
    pk.texmask = d->textured ? 0xFFFFFFFFu : 0u;
    pk.uoff = g->norm ? A3D_OFS(a3d_vbatch, u) : A3D_OFS(a3d_vbatch, ud);
    pk.voff = g->norm ? A3D_OFS(a3d_vbatch, v) : A3D_OFS(a3d_vbatch, vd);
    pk.pad = 0;
    for (uint32_t q = 0; q < 4u; q++)
    {
        const uint32_t rd = g->col[q] < A3D_VC_CONST;
        pk.coff[q] = A3D_OFS(a3d_vbatch, c) + 4u * A3D_VB * (rd ? g->col[q] : 0u);
        pk.cmask[q] = rd ? 0xFFFFFFFFu : 0u;
        for (uint32_t j = 0; j < 4u; j++)
            pk.cst[q][j] = g->cst[q];
    }
#endif
    for (uint32_t i = 0; i < n; i += 4u)
    {
        uint32_t redo = 0;
#ifdef A3D_PACK4_ASM
        a3d_pack4_asm(&b->x[i], out, &pk);
        redo = (pk.bad[0] ? 1u : 0u) | (pk.bad[1] ? 2u : 0u) | (pk.bad[2] ? 4u : 0u) | (pk.bad[3] ? 8u : 0u);
#else
        a3d_pack4(d, k, g, b, i, out, &redo);
#endif
        const uint32_t lanes = n - i < 4u ? n - i : 4u;
        for (uint32_t j = 0; j < lanes; j++)
        {
            a3d_pv *o = &g_pv[b->slot[i + j]];
            if ((redo >> j) & 1u)
            {
                a3d_pack_ref(d, k, g, b, i + j, o);
                a3d_cnt.vp_redo++;
                continue;
            }
            o->x = a3d_ubf(out[0][j]);
            o->y = a3d_ubf(out[1][j]);
            o->kx = (int32_t)out[2][j];
            o->ky = (int32_t)out[3][j];
            o->tv[0] = out[4][j];
            o->tv[1] = out[5][j];
            o->tv[2] = out[6][j];
            o->tv[3] = out[7][j];
            o->tv[4] = out[8][j];
            o->tv[5] = out[9][j];
            o->ok = out[10][j];
            o->pad = 0;
            a3d_cnt.vp_neon++;
        }
    }
#else
    for (uint32_t i = 0; i < n; i++)
    {
        a3d_pack_ref(d, k, g, b, i, &g_pv[b->slot[i]]);
        a3d_cnt.vp_scalar++;
    }
#endif
    if (a3d_pack_neg && n)
        g_pv[b->slot[0]].tv[1] ^= 1u;
}
#endif /* A3D_PACK_BATCH */

#ifdef A3D_PACK_SELFTEST
static uint32_t g_st_rng;
static uint32_t a3d_st_rnd(void)
{
    uint32_t x = g_st_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_st_rng = x;
}
static const uint32_t A3D_ST_SPECIAL[] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, 0x00800000u, 0x80800000u,
    0x3F800000u, 0xBF800000u, 0x3F7FFFFFu, 0x3F000000u, 0x3D800000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u,
    0xFFC00000u, 0x7F800001u, 0x60AD78EBu, 0x60AD78ECu, 0xE0AD78ECu, 0x7149F2C9u, 0x7149F2CAu, 0x44FFFFE0u,
    0xC5000000u, 0x45000000u, 0x46FFFE00u, 0x46FFFDFFu, 0xC7000000u, 0xC6FFFFFFu, 0x0A000000u, 0x8A000000u,
    0x01000000u, 0x1E3CE508u, 0x3F7FFF00u, 0x3C000000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x41C00000u, 0x447A0000u
};
static uint32_t a3d_st_bits(uint32_t kind)
{
    const uint32_t r = a3d_st_rnd();
    switch ((r >> 28) & 7u)
    {
    case 0: case 1:
        return A3D_ST_SPECIAL[(r & 0xFFFFu) % (sizeof(A3D_ST_SPECIAL) / sizeof(A3D_ST_SPECIAL[0]))];
    case 2:
        return a3d_st_rnd();                            /* any pattern */
    case 3:
        return a3d_st_rnd() & 0x80FFFFFFu;              /* tiny: denormals and the smallest normals, both signs */
    default:
    {
        const float f = (float)(a3d_st_rnd() & 0xFFFFFFu) / 16777216.0f;
        float v = f;
        switch (kind)
        {
        case 0: v = -256.0f + 1280.0f * f; break;
        case 1: v = f; break;
        case 2: v = 0.000244140625f + 16.0f * f * f; break;
        case 3: v = -8.0f + 16.0f * f; break;
        default: v = f; break;
        }
        uint32_t u;
        __builtin_memcpy(&u, &v, 4);
        return u;
    }
    }
}

uint32_t a3d_pack_selftest(uint32_t rounds, uint32_t seed, uint32_t *neon, uint32_t *redo, uint32_t *first)
{
    static a3d_ctx sc;
    static const uint32_t CM[6] = { 0xFFFFFFFFu, W3D_CMODE_RGBA, W3D_CMODE_ARGB, W3D_CMODE_BGRA, W3D_CMODE_RGB,
                                    W3D_CMODE_BGR };
    static const float WS[3] = { 1.0f, 0.125f, 9.5367431640625e-07f };
    uint32_t bad = 0;
    const uint32_t n0 = a3d_cnt.vp_neon, r0 = a3d_cnt.vp_redo;
    g_st_rng = seed ? seed : 0x3D3D2026u;
    *first = 0xFFFFFFFFu;
    for (uint32_t rd = 0; rd < rounds; rd++)
    {
        memset(&sc, 0, sizeof(sc));
        sc.tgt.width = (rd & 1u) ? 2047u : 640u;
        sc.tgt.height = (rd & 1u) ? 2047u : 480u;
        a3d_dc d;
        memset(&d, 0, sizeof(d));
        d.c = &sc;
        d.textured = (rd >> 1) & 1u;
        d.persp = (rd >> 2) & 1u;
        d.gouraud = ((rd >> 3) & 3u) != 0u;
        d.texw = 256.0f;
        d.texh = 96.0f;
        d.wscale = WS[(rd >> 5) % 3u];
        for (uint32_t q = 0; q < 4u; q++)
        {
            const uint32_t u = a3d_st_bits(1);
            __builtin_memcpy(&d.cur[q], &u, 4);
        }
        a3d_arrays a;
        memset(&a, 0, sizeof(a));
        a.tp = ((rd >> 7) % 3u) != 0u ? 1u : 0u;
        a.twoff = ((rd >> 7) % 3u) == 2u ? 8u : 0u;
        a.tflags = ((rd >> 9) & 1u) ? W3D_TEXCOORD_NORMALIZED : 0u;
        const uint32_t cm = CM[(rd >> 10) % 6u];
        a.cp = cm != 0xFFFFFFFFu;
        a.cmode = a.cp ? (cm | W3D_COLOR_FLOAT) : 0u;
        a3d_pvk k;
        a3d_pvk_make(&d, &k);
        a3d_vcfg g;
        a3d_vcfg_make(&d, &a, &g);
        a3d_vbatch *b = &g_vb;
        for (uint32_t i = 0; i < A3D_VB; i++)
        {
            b->slot[i] = i;
            b->x[i] = a3d_st_bits(0);
            b->y[i] = a3d_st_bits(0);
            b->z[i] = a3d_st_bits(1);
            b->u[i] = a3d_st_bits(3);
            b->v[i] = a3d_st_bits(3);
            b->w[i] = a3d_st_bits(2);
            if ((a3d_st_rnd() & 15u) == 0u)
            {
                /* a product the VFP keeps as a denormal: a tiny u, v against a small w */
                b->u[i] = 0x0A000000u | (a3d_st_rnd() & 0x007FFFFFu);
                b->w[i] = 0x00800000u + (a3d_st_rnd() & 0x00FFFFFFu);
            }
            for (uint32_t q = 0; q < 4u; q++)
                b->c[q][i] = a3d_st_bits(1);
        }
        a3d_pack_batch(&d, &k, &g, b, A3D_VB);
        for (uint32_t i = 0; i < A3D_VB; i++)
        {
            a3d_pv ref;
            uint32_t wr[sizeof(a3d_pv) / 4u], wp[sizeof(a3d_pv) / 4u], dif = 0;
            a3d_pack_ref(&d, &k, &g, b, i, &ref);
            __builtin_memcpy(wr, &ref, sizeof(wr));
            __builtin_memcpy(wp, &g_pv[i], sizeof(wp));
            for (uint32_t q = 0; q < sizeof(a3d_pv) / 4u; q++)
                dif |= wr[q] ^ wp[q];
            if (dif != 0)
            {
                if (bad == 0)
                    *first = (rd << 8) | i;
                bad++;
            }
        }
    }
    *neon = a3d_cnt.vp_neon - n0;
    *redo = a3d_cnt.vp_redo - r0;
    return bad;
}
#endif

static inline uint32_t a3d_index_at(const uint8_t *ip, uint32_t type, uint32_t base, uint32_t k)
{
    if (ip == 0)
        return base + k;
    switch (type)
    {
    case W3D_INDEX_UBYTE: return ip[k];
    case W3D_INDEX_UWORD: return a3d_be16(ip + 2u * k);
    default:              return a3d_be32(ip + 4u * k);
    }
}

typedef struct a3d_ix a3d_ix;
static int32_t a3d_assemble_pv(a3d_dc *d, const a3d_arrays *a, uint32_t prim, const a3d_ix *x, uint32_t count);

/* assemble primitives from a run of vertex indices: idx(k) gives the k-th index */
struct a3d_ix { uint32_t base, type, ptr; };
static int a3d_index(const a3d_ix *x, uint32_t k, uint32_t *out)
{
    if (x->ptr == 0)
    {
        *out = x->base + k;
        return 1;
    }
    switch (x->type)
    {
    case W3D_INDEX_UBYTE: *out = a3d_g_ptr(x->ptr)[k]; return 1;
    case W3D_INDEX_UWORD: *out = a3d_be16(a3d_g_ptr(x->ptr) + 2u * k); return 1;
    default:              *out = a3d_be32(a3d_g_ptr(x->ptr) + 4u * k); return 1;
    }
}

static int32_t a3d_assemble_pv(a3d_dc *d, const a3d_arrays *a, uint32_t prim, const a3d_ix *x, uint32_t count)
{
    if (a3d_vp_off || !a->fast
        || (prim != W3D_PRIMITIVE_TRIANGLES && prim != W3D_PRIMITIVE_TRIFAN && prim != W3D_PRIMITIVE_TRISTRIP))
    {
        return -1;
    }
    const uint32_t nread = prim == W3D_PRIMITIVE_TRIANGLES ? count - count % 3u : (count >= 3u ? count : 0u);
    if (nread == 0)
    {
        return -1;
    }
    uint32_t lo, hi;
    if (x->ptr == 0)
    {
        lo = x->base;
        hi = x->base + (nread - 1u);
        if (hi < lo)
        {
            a3d_cnt.vp_refused++;
            return -1;
        }
    }
    else
    {
        const uint8_t *ip = a3d_g_ptr(x->ptr);
        lo = 0xFFFFFFFFu;
        hi = 0;
        switch (x->type)
        {
        case W3D_INDEX_UBYTE:
            for (uint32_t k = 0; k < nread; k++)
            {
                const uint32_t v = ip[k];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            break;
        case W3D_INDEX_UWORD:
            for (uint32_t k = 0; k < nread; k++)
            {
                const uint32_t v = a3d_be16(ip + 2u * k);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            break;
        default:
            for (uint32_t k = 0; k < nread; k++)
            {
                const uint32_t v = a3d_be32(ip + 4u * k);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            break;
        }
    }
    const uint32_t n = hi - lo + 1u;
    const uint64_t va0 = (uint64_t)a->vp + (uint64_t)lo * a->vstride;
    const uint64_t va1 = (uint64_t)a->vp + (uint64_t)hi * a->vstride;
    const int64_t first = (int64_t)va0 + a->lo;
    const uint64_t end = va1 + (uint64_t)((int64_t)a->lo + (int64_t)a->span);
    if (hi > 0x00FFFFFFu || n > A3D_VP_MAX || a->vstride > 0x10000u || va1 > 0xFFFFFFFFull
        || first < 0 || end > 0x100000000ull
        || !a3d_g_ok((uint32_t)first, (uint32_t)(end - (uint64_t)first), A3D_GR_READ))
    {
        a3d_cnt.vp_refused++;
        return -1;
    }
    /* the vertices the indices name */
    const uint32_t words = (n + 31u) >> 5;
    for (uint32_t w = 0; w < words; w++)
        g_pv_mark[w] = 0;
    if (x->ptr == 0)
    {
        for (uint32_t w = 0; w < words; w++)
            g_pv_mark[w] = 0xFFFFFFFFu;         /* an array draw names every vertex of its range */
    }
    else
    {
        const uint8_t *ip = a3d_g_ptr(x->ptr);
        switch (x->type)
        {
        case W3D_INDEX_UBYTE:
            for (uint32_t q = 0; q < nread; q++)
            {
                const uint32_t j = ip[q] - lo;
                g_pv_mark[j >> 5] |= 1u << (j & 31u);
            }
            break;
        case W3D_INDEX_UWORD:
            for (uint32_t q = 0; q < nread; q++)
            {
                const uint32_t j = a3d_be16(ip + 2u * q) - lo;
                g_pv_mark[j >> 5] |= 1u << (j & 31u);
            }
            break;
        default:
            for (uint32_t q = 0; q < nread; q++)
            {
                const uint32_t j = a3d_be32(ip + 4u * q) - lo;
                g_pv_mark[j >> 5] |= 1u << (j & 31u);
            }
            break;
        }
    }
    a3d_pvk k;
    a3d_pvk_make(d, &k);
    const uint8_t *hp0 = a3d_g_ptr((uint32_t)first);
    uint32_t fetched = 0;
#ifdef A3D_PACK_NEON
    a3d_vcfg vc;
    a3d_vcfg_make(d, a, &vc);
    const uint32_t stride = a->vstride, ox = a->ox, ou = a->ou, ov = a->ov, ow = a->ow, oc = a->oc;
    a3d_vbatch *vb = &g_vb;
    uint32_t nb = 0;
    for (uint32_t w = 0; w < words; w++)
    {
        uint32_t bits = g_pv_mark[w];
        if (w + 1u == words && (n & 31u))
            bits &= (1u << (n & 31u)) - 1u;     /* (an array draw's last word: only [lo, hi]) */
        while (bits)
        {
            const uint32_t j = (w << 5) + (uint32_t)__builtin_ctz(bits);
            bits &= bits - 1u;
            const uint8_t *p = hp0 + j * stride;
            if (j + 4u < n)
                __builtin_prefetch(p + 4u * stride);    /* inside [lo, hi] only: the records this draw reads */
            vb->slot[nb] = j;
            vb->x[nb] = a3d_be32(p + ox);
            vb->y[nb] = a3d_be32(p + ox + 4u);
            vb->z[nb] = a3d_be32(p + ox + 8u);
            if (vc.tp)
            {
                vb->u[nb] = a3d_be32(p + ou);
                vb->v[nb] = a3d_be32(p + ov);
                if (vc.twoff)
                    vb->w[nb] = a3d_be32(p + ow);
            }
            for (uint32_t q = 0; q < vc.ncol; q++)
                vb->c[q][nb] = a3d_be32(p + oc + 4u * q);
            if (++nb == A3D_VB)
            {
                a3d_pack_batch(d, &k, &vc, vb, nb);
                fetched += nb;
                nb = 0;
            }
        }
    }
    if (nb)
    {
        a3d_pack_batch(d, &k, &vc, vb, nb);
        fetched += nb;
    }
    a3d_cnt.vp_batches += (fetched + A3D_VB - 1u) / A3D_VB;
#else
    for (uint32_t w = 0; w < words; w++)
    {
        uint32_t bits = g_pv_mark[w];
        if (w + 1u == words && (n & 31u))
            bits &= (1u << (n & 31u)) - 1u;     /* (an array draw's last word: only [lo, hi]) */
        while (bits)
        {
            const uint32_t j = (w << 5) + (uint32_t)__builtin_ctz(bits);
            bits &= bits - 1u;
            a3d_vtx v;
            a3d_fetch_rec(d, a, hp0 + j * a->vstride, &v);
            a3d_pv_make(d, &k, &v, &g_pv[j]);
            fetched++;
        }
    }
    a3d_cnt.vp_scalar += fetched;
#endif
    a3d_cnt.vp_draws++;
    a3d_cnt.vp_verts += fetched;
    const a3d_vsrc src = { a, 0, 0 };
    const uint8_t *ip = x->ptr ? a3d_g_ptr(x->ptr) : 0;
    const uint32_t it = x->type, ib = x->base;
    uint32_t i0, i1, i2;
    switch (prim)
    {
    case W3D_PRIMITIVE_TRIANGLES:
        for (uint32_t q = 0; q + 2u < count; q += 3u)
        {
            i0 = a3d_index_at(ip, it, ib, q);
            i1 = a3d_index_at(ip, it, ib, q + 1u);
            i2 = a3d_index_at(ip, it, ib, q + 2u);
            a3d_emit_pv(d, &k, &src, lo, i0, i1, i2);
        }
        break;
    case W3D_PRIMITIVE_TRIFAN:
    {
        const uint32_t f0 = a3d_index_at(ip, it, ib, 0);
        for (uint32_t q = 1; q + 1u < count; q++)
        {
            i1 = a3d_index_at(ip, it, ib, q);
            i2 = a3d_index_at(ip, it, ib, q + 1u);
            a3d_emit_pv(d, &k, &src, lo, f0, i1, i2);
        }
        break;
    }
    default:    /* W3D_PRIMITIVE_TRISTRIP */
        for (uint32_t q = 0; q + 2u < count; q++)
        {
            i0 = a3d_index_at(ip, it, ib, q);
            i1 = a3d_index_at(ip, it, ib, q + 1u);
            i2 = a3d_index_at(ip, it, ib, q + 2u);
            if (q & 1u)
                a3d_emit_pv(d, &k, &src, lo, i1, i0, i2);  /* odd triangles keep the strip's winding */
            else
                a3d_emit_pv(d, &k, &src, lo, i0, i1, i2);
        }
        break;
    }
    return W3D_SUCCESS;
}

static int32_t a3d_assemble(a3d_dc *d, const a3d_arrays *a, uint32_t prim, const a3d_ix *x, uint32_t count,
                            float lw, float ps)
{
    {
        const int32_t r = a3d_assemble_pv(d, a, prim, x, count);
        if (r >= 0)
            return r;
    }
    for (uint32_t i = 0; i < A3D_VC; i++)
        g_vc.tag[i] = 0;
    a3d_vtx v0, v1, v2, first;
    uint32_t i0, i1, i2;
    switch (prim)
    {
    case W3D_PRIMITIVE_TRIANGLES:
        for (uint32_t k = 0; k + 2u < count; k += 3u)
        {
            if (!a3d_index(x, k, &i0) || !a3d_index(x, k + 1u, &i1) || !a3d_index(x, k + 2u, &i2)
                || !a3d_vertex(d, a, i0, &v0) || !a3d_vertex(d, a, i1, &v1) || !a3d_vertex(d, a, i2, &v2))
                return W3D_ILLEGALINPUT;
            a3d_emit(d, &v0, &v1, &v2, 0);
        }
        return W3D_SUCCESS;
    case W3D_PRIMITIVE_TRIFAN:
        if (count < 3u)
            return W3D_SUCCESS;
        if (!a3d_index(x, 0, &i0) || !a3d_vertex(d, a, i0, &first))
            return W3D_ILLEGALINPUT;
        for (uint32_t k = 1; k + 1u < count; k++)
        {
            if (!a3d_index(x, k, &i1) || !a3d_index(x, k + 1u, &i2)
                || !a3d_vertex(d, a, i1, &v1) || !a3d_vertex(d, a, i2, &v2))
                return W3D_ILLEGALINPUT;
            a3d_emit(d, &first, &v1, &v2, 0);
        }
        return W3D_SUCCESS;
    case W3D_PRIMITIVE_TRISTRIP:
        for (uint32_t k = 0; k + 2u < count; k++)
        {
            if (!a3d_index(x, k, &i0) || !a3d_index(x, k + 1u, &i1) || !a3d_index(x, k + 2u, &i2)
                || !a3d_vertex(d, a, i0, &v0) || !a3d_vertex(d, a, i1, &v1) || !a3d_vertex(d, a, i2, &v2))
                return W3D_ILLEGALINPUT;
            if (k & 1u)
                a3d_emit(d, &v1, &v0, &v2, 0);      /* odd triangles keep the strip's winding */
            else
                a3d_emit(d, &v0, &v1, &v2, 0);
        }
        return W3D_SUCCESS;
    case W3D_PRIMITIVE_POINTS:
        for (uint32_t k = 0; k < count; k++)
        {
            if (!a3d_index(x, k, &i0) || !a3d_vertex(d, a, i0, &v0))
                return W3D_ILLEGALINPUT;
            a3d_point(d, &v0, ps);
        }
        return W3D_SUCCESS;
    case W3D_PRIMITIVE_LINES:
    case W3D_PRIMITIVE_LINESTRIP:
    case W3D_PRIMITIVE_LINELOOP:
    {
        const uint32_t step = prim == W3D_PRIMITIVE_LINES ? 2u : 1u;
        for (uint32_t k = 0; k + 1u < count; k += step)
        {
            if (!a3d_index(x, k, &i0) || !a3d_index(x, k + 1u, &i1)
                || !a3d_vertex(d, a, i0, &v0) || !a3d_vertex(d, a, i1, &v1))
                return W3D_ILLEGALINPUT;
            a3d_line(d, &v0, &v1, lw);
        }
        if (prim == W3D_PRIMITIVE_LINELOOP && count >= 2u)
        {
            if (!a3d_index(x, count - 1u, &i0) || !a3d_index(x, 0, &i1)
                || !a3d_vertex(d, a, i0, &v0) || !a3d_vertex(d, a, i1, &v1))
                return W3D_ILLEGALINPUT;
            a3d_line(d, &v0, &v1, lw);
        }
        return W3D_SUCCESS;
    }
    default:
        return W3D_ILLEGALINPUT;
    }
}

int32_t a3d_draw(a3d_ctx *c, uint32_t kind, uint32_t a1)
{
    a3d_dc d;
    __builtin_memset(&d, 0, sizeof(d));
    d.c = c;
    int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    a3d_cnt.draws++;
    const uint8_t *g = a3d_g_ptr(c->gctx);
    a3d_vtx v[3];
    switch (kind)
    {
    case A3D_DK_TRIANGLE:
    {
        if (!a3d_g_ok(a1, W3D_TRI_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        e = a3d_state_c(&d, a3d_be32(a3d_g_ptr(a1 + W3D_TRI_TEX)), 0);
        if (e != W3D_SUCCESS)
            return e;
        for (uint32_t i = 0; i < 3u; i++)
            if (!a3d_fetch_w3dv(&d, a1 + W3D_TRI_V1 + i * W3D_V_SIZE, &v[i]))
                return W3D_ILLEGALINPUT;
        a3d_emit(&d, &v[0], &v[1], &v[2], 0);
        return W3D_SUCCESS;
    }
    case A3D_DK_TRIANGLEV:
    {
        if (!a3d_g_ok(a1, W3D_TRIV_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        e = a3d_state_c(&d, a3d_be32(a3d_g_ptr(a1 + W3D_TRIV_TEX)), 0);
        if (e != W3D_SUCCESS)
            return e;
        for (uint32_t i = 0; i < 3u; i++)
            if (!a3d_fetch_w3dv(&d, a3d_be32(a3d_g_ptr(a1 + 4u * i)), &v[i]))
                return W3D_ILLEGALINPUT;
        a3d_emit(&d, &v[0], &v[1], &v[2], 0);
        return W3D_SUCCESS;
    }
    case A3D_DK_TRIFAN:
    case A3D_DK_TRISTRIP:
    case A3D_DK_TRIFANV:
    case A3D_DK_TRISTRIPV:
    {
        if (!a3d_g_ok(a1, W3D_TRIS_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        const uint8_t *p = a3d_g_ptr(a1);
        const uint32_t n = a3d_be32(p + W3D_TRIS_COUNT);
        const uint32_t vp = a3d_be32(p + W3D_TRIS_V);
        const int byptr = (kind == A3D_DK_TRIFANV || kind == A3D_DK_TRISTRIPV);
        if (n > 0x10000u || !a3d_g_ok(vp, n * (byptr ? 4u : W3D_V_SIZE), A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        e = a3d_state_c(&d, a3d_be32(p + W3D_TRIS_TEX), 0);
        if (e != W3D_SUCCESS)
            return e;
        const int fan = (kind == A3D_DK_TRIFAN || kind == A3D_DK_TRIFANV);
        if (n < 3u)
            return W3D_SUCCESS;
        {
            const int32_t r = a3d_strip_pv(&d, vp, n, byptr ? 1u : 0u, fan ? 1u : 0u);
            if (r >= 0)
                return r;
        }
        for (uint32_t k = 0; k + 2u < n; k++)
        {
            /* fan: (0, k+1, k+2); strip: (k, k+1, k+2), the odd ones swapped to keep the winding */
            const uint32_t i0 = fan ? 0u : k, i1 = k + 1u, i2 = k + 2u;
            const uint32_t idx[3] = { i0, i1, i2 };
            for (uint32_t j = 0; j < 3u; j++)
            {
                const uint32_t va = byptr ? a3d_be32(a3d_g_ptr(vp + 4u * idx[j])) : vp + idx[j] * W3D_V_SIZE;
                if (!a3d_fetch_w3dv(&d, va, &v[j]))
                    return W3D_ILLEGALINPUT;
            }
            if (!fan && (k & 1u))
                a3d_emit(&d, &v[1], &v[0], &v[2], 0);
            else
                a3d_emit(&d, &v[0], &v[1], &v[2], 0);
        }
        return W3D_SUCCESS;
    }
    case A3D_DK_LINE:
    {
        if (!a3d_g_ok(a1, W3D_LINE_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        e = a3d_state_c(&d, a3d_be32(a3d_g_ptr(a1 + W3D_LINE_TEX)), 1);
        if (e != W3D_SUCCESS)
            return e;
        if (!a3d_fetch_w3dv(&d, a1 + W3D_LINE_V1, &v[0]) || !a3d_fetch_w3dv(&d, a1 + W3D_LINE_V2, &v[1]))
            return W3D_ILLEGALINPUT;
        a3d_line(&d, &v[0], &v[1], a3d_bef32(a3d_g_ptr(a1 + W3D_LINE_WIDTH)));
        return W3D_SUCCESS;
    }
    case A3D_DK_POINT:
    {
        if (!a3d_g_ok(a1, W3D_POINT_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        e = a3d_state_c(&d, a3d_be32(a3d_g_ptr(a1 + W3D_POINT_TEX)), 1);
        if (e != W3D_SUCCESS)
            return e;
        if (!a3d_fetch_w3dv(&d, a1 + W3D_POINT_V1, &v[0]))
            return W3D_ILLEGALINPUT;
        a3d_point(&d, &v[0], a3d_bef32(a3d_g_ptr(a1 + W3D_POINT_SIZE_F)));
        return W3D_SUCCESS;
    }
    case A3D_DK_LINESTRIP:
    case A3D_DK_LINELOOP:
    {
        if (!a3d_g_ok(a1, W3D_LINES_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        const uint8_t *p = a3d_g_ptr(a1);
        const uint32_t n = a3d_be32(p + W3D_LINES_COUNT);
        const uint32_t vp = a3d_be32(p + W3D_LINES_V);
        if (n > 0x10000u || !a3d_g_ok(vp, n * W3D_V_SIZE, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        e = a3d_state_c(&d, a3d_be32(p + W3D_LINES_TEX), 1);
        if (e != W3D_SUCCESS)
            return e;
        const float lw = a3d_bef32(p + W3D_LINES_WIDTH);
        for (uint32_t k = 0; k + 1u < n; k++)
        {
            if (!a3d_fetch_w3dv(&d, vp + k * W3D_V_SIZE, &v[0]) || !a3d_fetch_w3dv(&d, vp + (k + 1u) * W3D_V_SIZE, &v[1]))
                return W3D_ILLEGALINPUT;
            a3d_line(&d, &v[0], &v[1], lw);
        }
        if (kind == A3D_DK_LINELOOP && n >= 2u)
        {
            if (!a3d_fetch_w3dv(&d, vp + (n - 1u) * W3D_V_SIZE, &v[0]) || !a3d_fetch_w3dv(&d, vp, &v[1]))
                return W3D_ILLEGALINPUT;
            a3d_line(&d, &v[0], &v[1], lw);
        }
        return W3D_SUCCESS;
    }
    case A3D_DK_ARRAY:
    case A3D_DK_ELEMENTS:
    {
        const uint32_t words = kind == A3D_DK_ARRAY ? 3u : 4u;
        if (!a3d_g_ok(a1, 4u * words, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        const uint8_t *p = a3d_g_ptr(a1);
        const uint32_t prim = a3d_be32(p);
        a3d_ix x;
        uint32_t count;
        if (kind == A3D_DK_ARRAY)
        {
            x.base = a3d_be32(p + 4);
            x.type = W3D_INDEX_ULONG;
            x.ptr = 0;
            count = a3d_be32(p + 8);
        }
        else
        {
            x.base = 0;
            x.type = a3d_be32(p + 4);
            count = a3d_be32(p + 8);
            x.ptr = a3d_be32(p + 12);
            const uint32_t isz = x.type == W3D_INDEX_UBYTE ? 1u : (x.type == W3D_INDEX_UWORD ? 2u : 4u);
            if (x.type > W3D_INDEX_ULONG || count > 0x100000u || !a3d_g_ok(x.ptr, count * isz, A3D_GR_READ))
                return W3D_ILLEGALINPUT;
        }
        if (prim > W3D_PRIMITIVE_LINESTRIP || count > 0x100000u)
            return W3D_ILLEGALINPUT;
        a3d_arrays ar;
        a3d_arrays_read(c, &ar);
        const uint32_t lp = prim >= W3D_PRIMITIVE_POINTS;
        e = a3d_state_c(&d, a3d_be32(g + W3D_CTX_CURRENTTEX), lp);
        if (e != W3D_SUCCESS)
            return e;
        return a3d_assemble(&d, &ar, prim, &x, count, 1.0f, 1.0f);
    }
    default:
        return W3D_ILLEGALINPUT;
    }
}

A3D_LATE_TEXT static void a3d_mgl_vtx(const a3d_dc *d, const mgl_vtx *s, a3d_vtx *o)
{
    o->x = s->x;
    o->y = s->y;
    o->z = s->z;
    o->r = s->r;
    o->g = s->g;
    o->b = s->b;
    o->a = s->a;
    a3d_finish_vtx(d, o, s->s, s->t, s->w);
}

#ifdef A3D_HEAP_LIB68K
#define A3D_MGL_SV 64u
#define A3D_MGL_ST 64u
#else
#define A3D_MGL_SV 256u
#define A3D_MGL_ST 256u
#endif
typedef struct a3d_mgl_out
{
    a3d_dc *d;
    mgl_vtx v[A3D_MGL_SV];
    uint16_t i[3u * A3D_MGL_ST];
    uint32_t nv, nt, binned;
} a3d_mgl_out;
static a3d_mgl_out g_mgl_out A3D_LATE_BSS;

A3D_LATE_TEXT static void a3d_mgl_drain(a3d_mgl_out *m)
{
    a3d_vtx v[3];
    for (uint32_t t = 0; t < m->nt; t++)
    {
        for (uint32_t k = 0; k < 3u; k++)
            a3d_mgl_vtx(m->d, &m->v[m->i[3u * t + k]], &v[k]);
        a3d_emit(m->d, &v[0], &v[1], &v[2], 0);     /* (a3d_emit: SETUP, its binning: BIN) */
    }
    m->binned += m->nt;
    m->nv = m->nt = 0;
}

A3D_LATE_TEXT static void a3d_mgl_room(void *ud, uint32_t nv, uint32_t nt)
{
    a3d_mgl_out *m = (a3d_mgl_out *)ud;
    if (m->nv + nv > A3D_MGL_SV || m->nt + nt > A3D_MGL_ST)
        a3d_mgl_drain(m);
}

A3D_LATE_TEXT int32_t a3d_draw_mgl(a3d_ctx *c, const mgl_geom *g, const uint8_t *rec, uint32_t ntri, uint32_t *binned)
{
    a3d_dc d;
    __builtin_memset(&d, 0, sizeof(d));
    d.c = c;
    int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    a3d_cnt.draws++;
    a3d_mcnt.batches++;
    e = a3d_state_c(&d, a3d_be32(a3d_g_ptr(c->gctx) + W3D_CTX_CURRENTTEX), 0);
    if (e != W3D_SUCCESS)
        return e;
    a3d_mgl_out *m = &g_mgl_out;
    m->d = &d;
    m->nv = m->nt = m->binned = 0;
    uint32_t in = 0, culled = 0, clipped = 0;
    const mgl_stage_out o = { m->v, m->i, &m->nv, &m->nt, a3d_mgl_room, m, &in, &culled, &clipped };
    for (uint32_t t = 0; t < ntri; t++)
    {
        mgl_cvtx v[3];
        float ox[3], oy[3], oz[3], ow[3];
        for (uint32_t k = 0; k < 3u; k++)
        {
            const uint8_t *p = rec + (3u * t + k) * A3D_MGL_REC_SIZE;
            ox[k] = a3d_bef32(p);
            oy[k] = a3d_bef32(p + 4);
            oz[k] = a3d_bef32(p + 8);
            ow[k] = a3d_bef32(p + 12);
            v[k].s = a3d_bef32(p + 16);
            v[k].t = a3d_bef32(p + 20);
            v[k].r = a3d_bef32(p + 24);
            v[k].g = a3d_bef32(p + 28);
            v[k].b = a3d_bef32(p + 32);
            v[k].a = a3d_bef32(p + 36);
        }
        for (uint32_t k = 0; k < 3u; k++)
            mgl_stage_xform(g->mvp, ox[k], oy[k], oz[k], ow[k], &v[k]);
        mgl_stage_tri(g, &v[0], &v[1], &v[2], &o);
    }
    a3d_mgl_drain(m);
    a3d_mcnt.tris_in += in;
    a3d_mcnt.culled += culled;
    a3d_mcnt.clipped += clipped;
    a3d_mcnt.tris_out += m->binned;
    *binned = m->binned;
    return W3D_SUCCESS;
}

int32_t a3d_clear(a3d_ctx *c, uint32_t what, uint32_t d1, uint32_t a1)
{
    if (what != 1u && what != 2u)
        return W3D_ILLEGALINPUT;
    double z = 1.0;
    if (what == 2u)
    {
        if ((a1 & 1u) || !a3d_g_ok(a1, 8, A3D_GR_READ))
            return W3D_ILLEGALINPUT;
        z = a3d_bef64(a3d_g_ptr(a1));
        if (!(z == z))
            return W3D_ILLEGALINPUT;
    }
    const int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    if (!a3d_room(c))
    {
        /* cut: the pass reopened */
    }
    a3d_fbstate_begin(c);
    if (what == 1u)
    {
        /* W3D_ClearDrawRegion's colour is ARGB; RasterIX's clear colour is RGBA8888 (BaseColorReg) */
        c->clear_color = (d1 << 8) | (d1 >> 24);
        a3d_reg(c, RIX_REG_CLEAR_COLOR, c->clear_color);
        for (uint32_t i = 0; i < a3d_B.bands; i++)
        {
            a3d_band_prepare(c, i, 1u, 0u, 1u, 0u);
            a3d_bin_band_fb(&a3d_B, i, RIX_FB_MEMSET | RIX_FB_COLOR);
            a3d_P.band[i] |= A3D_BT_CDIRTY;
        }
    }
    else
    {
        const double zc = a3d_clamp01d(z);
        uint32_t z16 = (uint32_t)(zc * 65536.0);
        if (z16 > 0xFFFFu)
            z16 = 0xFFFFu;
        c->clear_depth = z16;
        a3d_reg(c, RIX_REG_CLEAR_DEPTH, z16);
        for (uint32_t i = 0; i < a3d_B.bands; i++)
        {
            a3d_band_prepare(c, i, 0u, 1u, 0u, 1u);
            a3d_bin_band_fb(&a3d_B, i, RIX_FB_MEMSET | RIX_FB_DEPTH);
            a3d_P.band[i] |= A3D_BT_ZDIRTY;
        }
    }
    a3d_fbstate_end(c);
    return W3D_SUCCESS;
}

int32_t a3d_zspan(a3d_ctx *c, uint32_t op, uint32_t a1)
{
    if (op > A3D_ZS_WRITESPAN || !a3d_g_ok(a1, 20, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    const uint8_t *p = a3d_g_ptr(a1);
    const uint32_t x = a3d_be32(p), y = a3d_be32(p + 4);
    uint32_t n = a3d_be32(p + 8);
    const uint32_t zp = a3d_be32(p + 12), mp = a3d_be32(p + 16);
    if (op == A3D_ZS_READPIXEL || op == A3D_ZS_WRITEPIXEL)
        n = 1;
    const int write = op >= A3D_ZS_WRITEPIXEL;
    if (n == 0 || n > 4096u || (zp & 1u) || !a3d_g_ok(zp, 8u * n, write ? A3D_GR_READ : A3D_GR_WRITE))
        return W3D_ILLEGALINPUT;
    if (op == A3D_ZS_WRITESPAN && mp && !a3d_g_ok(mp, n, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    const int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    const uint8_t *g = a3d_g_ptr(c->gctx);
    if (!a3d_be16(g + W3D_CTX_ZBUFFERALLOC) || c->depth == 0)
        return W3D_NOZBUFFER;
    a3d_drain_to(a3d_submit(c, 0));             /* unbounded: a Z read must see finished depth (rare) */
    if (x >= c->depth_w || y >= c->depth_h || n > c->depth_w - x)
        return W3D_ILLEGALINPUT;
    const uint32_t L = a3d_B.band_lines ? a3d_B.band_lines : 1u;
    const uint32_t band = y / L;
    const uint32_t zs = c->depth_stride ? c->depth_stride : c->depth_w;
    uint8_t *zrow = c->depth + 2u * (y * zs);
    if (band < 64u && !((c->depth_band_valid >> band) & 1u))
    {
        /* this band's depth never reached DDR: it holds the clear value as far as the guest knows */
        const uint32_t cz = c->clear_depth;
        const uint32_t r0 = band * L, r1 = (r0 + L < c->depth_h) ? r0 + L : c->depth_h;
        for (uint32_t r = r0; r < r1; r++)
            for (uint32_t k = 0; k < zs; k++)
                a3d_wle16(c->depth + 2u * (r * zs + k), (uint16_t)cz);
        c->depth_band_valid |= (uint64_t)1 << band;
    }
    for (uint32_t k = 0; k < n; k++)
    {
        uint8_t *zz = zrow + 2u * (x + k);
        uint8_t *gz = a3d_g_ptr(zp + 8u * k);
        if (write)
        {
            if (op == A3D_ZS_WRITESPAN && mp && a3d_g_ptr(mp)[k] == 0)
                continue;
            const double v = a3d_clamp01d(a3d_bef64(gz));
            uint32_t z16 = (uint32_t)(v * 65536.0);
            if (z16 > 0xFFFFu)
                z16 = 0xFFFFu;
            a3d_wle16(zz, (uint16_t)z16);
        }
        else
            a3d_wbef64(gz, (double)a3d_le16(zz) / 65536.0);
    }
    return W3D_SUCCESS;
}

#if defined(__arm__) && !defined(__APPLE__) && !defined(A3D_HEAP_LIB68K)
#define A3D_MGLC_TEXT __attribute__((section(".mglc_tail_text")))
#else
#define A3D_MGLC_TEXT
#endif
A3D_MGLC_TEXT int32_t a3d_mglc_tris(a3d_ctx *c, const mgl_vtx *v, uint32_t nv, const uint16_t *idx, uint32_t nt)
{
    a3d_dc d;
    __builtin_memset(&d, 0, sizeof(d));
    d.c = c;
    int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    a3d_cnt.draws++;
    e = a3d_state_c(&d, a3d_be32(a3d_g_ptr(c->gctx) + W3D_CTX_CURRENTTEX), 0);
    if (e != W3D_SUCCESS)
        return e;
    a3d_vtx t[3];
    for (uint32_t i = 0; i < nt; i++)
    {
        const uint32_t i0 = idx[3u * i], i1 = idx[3u * i + 1u], i2 = idx[3u * i + 2u];
        if (i0 >= nv || i1 >= nv || i2 >= nv)
            continue;
        a3d_mgl_vtx(&d, &v[i0], &t[0]);
        a3d_mgl_vtx(&d, &v[i1], &t[1]);
        a3d_mgl_vtx(&d, &v[i2], &t[2]);
        a3d_emit(&d, &t[0], &t[1], &t[2], 0);
    }
    a3d_mcnt.tris_out += nt;
    return W3D_SUCCESS;
}

A3D_MGLC_TEXT int32_t a3d_mglc_lines(a3d_ctx *c, const mgl_vtx *v, uint32_t nlines)
{
    a3d_dc d;
    __builtin_memset(&d, 0, sizeof(d));
    d.c = c;
    int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    a3d_cnt.draws++;
    const uint8_t *g = a3d_g_ptr(c->gctx);
    e = a3d_state_c(&d, (a3d_be32(g + W3D_CTX_STATE) & W3D_TEXMAPPING) ? a3d_be32(g + W3D_CTX_CURRENTTEX) : 0u, 1);
    if (e != W3D_SUCCESS)
        return e;
    a3d_vtx t[2];
    for (uint32_t i = 0; i < nlines; i++)
    {
        a3d_mgl_vtx(&d, &v[2u * i], &t[0]);
        a3d_mgl_vtx(&d, &v[2u * i + 1u], &t[1]);
        a3d_line(&d, &t[0], &t[1], 1.0f);
    }
    return W3D_SUCCESS;
}

/* a3d_clear's, the depth given (W3D_ClearZBuffer's double is in guest memory there) */
A3D_MGLC_TEXT int32_t a3d_mglc_clear(a3d_ctx *c, uint32_t what, uint32_t argb, double z)
{
    if ((what != 1u && what != 2u) || !(z == z))
        return W3D_ILLEGALINPUT;
    const int32_t e = a3d_pass_ensure(c);
    if (e != W3D_SUCCESS)
        return e;
    (void)a3d_room(c);
    a3d_fbstate_begin(c);
    if (what == 1u)
    {
        c->clear_color = (argb << 8) | (argb >> 24);
        a3d_reg(c, RIX_REG_CLEAR_COLOR, c->clear_color);
        for (uint32_t i = 0; i < a3d_B.bands; i++)
        {
            a3d_band_prepare(c, i, 1u, 0u, 1u, 0u);
            a3d_bin_band_fb(&a3d_B, i, RIX_FB_MEMSET | RIX_FB_COLOR);
            a3d_P.band[i] |= A3D_BT_CDIRTY;
        }
    }
    else
    {
        const double zc = a3d_clamp01d(z);
        uint32_t z16 = (uint32_t)(zc * 65536.0);
        if (z16 > 0xFFFFu)
            z16 = 0xFFFFu;
        c->clear_depth = z16;
        a3d_reg(c, RIX_REG_CLEAR_DEPTH, z16);
        for (uint32_t i = 0; i < a3d_B.bands; i++)
        {
            a3d_band_prepare(c, i, 0u, 1u, 0u, 1u);
            a3d_bin_band_fb(&a3d_B, i, RIX_FB_MEMSET | RIX_FB_DEPTH);
            a3d_P.band[i] |= A3D_BT_ZDIRTY;
        }
    }
    a3d_fbstate_end(c);
    return W3D_SUCCESS;
}
