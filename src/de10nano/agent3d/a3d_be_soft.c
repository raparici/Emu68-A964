/*
    Texture unit and fog after RasterIX, Copyright (c) 2023 ToNi3141 (https://github.com/ToNi3141/RasterIX)
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* BigMig 3Dx2: the SOFT backend (core #1 renders the band lists itself); its texture unit and fog follow RasterIX's RTL. */

#include "a3d_agent.h"

#define SOFT_JOBS       2u
#define SOFT_BAND_PX    16384u

typedef struct soft_job
{
    a3d_listset ls;
    uint32_t next;              /* next band to render                       */
    uint32_t live;
} soft_job;

static soft_job g_job[SOFT_JOBS];
static uint32_t g_soft_done;
static uint16_t g_col[SOFT_BAND_PX], g_dep[SOFT_BAND_PX];
uint32_t a3d_soft_le A3D_R8_BSS;

/* the band's state, rebuilt from each band list (they are self-contained) */
typedef struct soft_st
{
    uint32_t reg[19];
    uint32_t page[A3D_MAX_PAGES];
    uint32_t npages;
    uint32_t fog[RIX_FOG_LUT_WORDS];
} soft_st;

static uint8_t *soft_va(const a3d_listset *ls, uint32_t phys, uint32_t bytes)
{
    return (uint8_t *)(uintptr_t)a3d_ls_va(ls, phys, bytes);
}

static void soft_defaults(soft_st *s)
{
    memset(s, 0, sizeof(*s));
    s->reg[RIX_REG_FRAGMENT_PIPELINE] = 2u | (0xFFu << 6) | (1u << 14) | (0xFu << 15) | (1u << 19) | (2u << 27);
    s->reg[RIX_REG_TEX_ENV(0)] = 1u | (1u << 3) | (3u << 8) | (3u << 14) | (2u << 18) | (2u << 20);
}

static uint32_t c8(int32_t v)
{
    return v < 0 ? 0u : (v > 255 ? 255u : (uint32_t)v);
}

static int soft_test(uint32_t func, uint32_t a, uint32_t b)
{
    switch (func & 7u)
    {
    case 0: return 1;           /* ALWAYS   */
    case 1: return 0;           /* NEVER    */
    case 2: return a < b;       /* LESS     */
    case 3: return a == b;      /* EQUAL    */
    case 4: return a <= b;      /* LEQUAL   */
    case 5: return a > b;       /* GREATER  */
    case 6: return a != b;      /* NOTEQUAL */
    default: return a >= b;     /* GEQUAL   */
    }
}

static uint32_t soft_rep5(uint32_t v) { return (v << 3) | (v >> 2); }
static uint32_t soft_rep6(uint32_t v) { return (v << 2) | (v >> 4); }

static void soft_texel(uint32_t fmt, uint16_t v, uint32_t c[4])
{
    switch (fmt)
    {
    case 2u:                    /* RGB565   */
        c[0] = soft_rep5((v >> 11) & 31u); c[1] = soft_rep6((v >> 5) & 63u); c[2] = soft_rep5(v & 31u); c[3] = 255u;
        break;
    case 1u:                    /* RGBA5551 */
        c[0] = soft_rep5((v >> 11) & 31u); c[1] = soft_rep5((v >> 6) & 31u); c[2] = soft_rep5((v >> 1) & 31u);
        c[3] = (v & 1u) ? 255u : 0u;
        break;
    default:                    /* RGBA4444 */
        c[0] = ((v >> 12) & 15u) * 17u; c[1] = ((v >> 8) & 15u) * 17u;
        c[2] = ((v >> 4) & 15u) * 17u; c[3] = (v & 15u) * 17u;
        break;
    }
}

typedef a3d_soft_tu soft_tu;

uint32_t a3d_soft_lvl_off(uint32_t lw, uint32_t lh, uint32_t l)
{
    if (lw > 8u) lw = 8u;
    if (lh > 8u) lh = 8u;
    const uint32_t width = l < lw ? lw - l : 0u, height = l < lh ? lh - l : 0u;
    const uint32_t lod_mask = ~((1u << (width + height + 1u)) - 1u) & 0x3FFFFu;          /* 18 bits */
    const uint32_t wmask = (1u << lw) - 1u, hmask = (1u << lh) - 1u;
    uint32_t mask = 0;
    for (uint32_t i = 0; i < 9u; i++)
    {
        mask |= (((wmask >> i) ^ (hmask >> i)) & 1u) << (17u - (2u * i + 1u));
        mask |= (((wmask >> i) | (hmask >> i)) & 1u) << (17u - 2u * i);
    }
    const uint32_t fin = mask >> (18u - (lw + lh + 1u));
    return (fin & lod_mask) & 0x1FFFFu;                                                    /* [0 +: 17] */
}

/* a float coordinate as the RTL's S16.15 (floor; a NaN or a huge value becomes 0) */
static int32_t soft_s1615(float v)
{
    const float f = v * 32768.0f;
    if (!(f > -1073741824.0f && f < 1073741824.0f))
        return 0;
    const int32_t i = (int32_t)f;
    return ((float)i > f) ? i - 1 : i;
}

/* LodCalculator.v: the LOD from this pixel's coordinate and the one at (x + 1, y + 1) */
uint32_t a3d_soft_lod(uint32_t lw, uint32_t lh, int32_t s, int32_t t, int32_t sxy, int32_t txy)
{
    const uint32_t ds = (uint32_t)(s - sxy), dt = (uint32_t)(t - txy);
    const uint32_t us = (ds & 0x80000000u) ? ((~ds + 1u) & 0x7FFFu) : (ds & 0x7FFFu);
    const uint32_t ut = (dt & 0x80000000u) ? ((~dt + 1u) & 0x7FFFu) : (dt & 0x7FFFu);
    const uint32_t du = ((us >> 7) & 0xFFu) >> (8u - (lw > 8u ? 8u : lw));
    const uint32_t dv = ((ut >> 7) & 0xFFu) >> (8u - (lh > 8u ? 8u : lh));
    uint32_t m = (du | dv) & 0xFFu, lod = 0;
    while (m)
    {
        lod++;
        m >>= 1;
    }
    return lod;
}

static uint32_t soft_clampc(int32_t c, uint32_t clamp)
{
    if (clamp)
    {
        if (c < 0)
            return 0;
        if ((c >> 15) != 0)
            return 0x7FFFu;
    }
    return (uint32_t)c & 0xFFFFu;
}

static void soft_mix(const uint32_t a[4], const uint32_t b[4], uint32_t i, uint32_t o[4])
{
    for (uint32_t k = 0; k < 4u; k++)
        o[k] = (a[k] * i + b[k] * (255u - i) + 127u) / 255u;
}

void a3d_soft_taps(const a3d_soft_tu *T, uint32_t l, int32_t s, int32_t t, uint32_t addr[4], uint32_t *subu,
                   uint32_t *subv)
{
    const uint32_t wl = l < T->lw ? T->lw - l : 0u, hl = l < T->lh ? T->lh - l : 0u;
    const uint32_t off = a3d_soft_lvl_off(T->lw, T->lh, l);
    /* half a texel (bilinear) or one (the nearest path's second column, unused): 0 for a 1-texel axis */
    const int32_t hs = wl ? (int32_t)(1u << (14u - wl)) : 0, ht = hl ? (int32_t)(1u << (14u - hl)) : 0;
    int32_t s0 = s, t0 = t, s1, t1;
    if (T->bilinear)
    {
        s0 = s - hs; s1 = s + hs;
        t0 = t - ht; t1 = t + ht;
    }
    else
    {
        s1 = s + 2 * hs;
        t1 = t + 2 * ht;
    }
    const uint32_t u0 = ((soft_clampc(s0, T->clamps) >> 7) & 0xFFu) >> (8u - wl);
    const uint32_t u1 = ((soft_clampc(s1, T->clamps) >> 7) & 0xFFu) >> (8u - wl);
    const uint32_t v0 = ((soft_clampc(t0, T->clampt) >> 7) & 0xFFu) >> (8u - hl);
    const uint32_t v1 = ((soft_clampc(t1, T->clampt) >> 7) & 0xFFu) >> (8u - hl);
    addr[0] = off + ((v0 << wl) | u0);
    addr[1] = off + ((v0 << wl) | u1);
    addr[2] = off + ((v1 << wl) | u0);
    addr[3] = off + ((v1 << wl) | u1);
    *subu = (((uint32_t)s0 & 0x7FFFu) << 1 << wl) & 0xFFFFu;
    *subv = (((uint32_t)t0 & 0x7FFFu) << 1 << hl) & 0xFFFFu;
}

/* TextureSampler.v + TextureFilter.v: the texel of (s, t) (S16.15) at level l */
void a3d_soft_sample(const a3d_soft_tu *T, uint32_t l, int32_t s, int32_t t, uint32_t out[4])
{
    uint32_t a[4], subu, subv;
    a3d_soft_taps(T, l, s, t, a, &subu, &subv);
    uint32_t c00[4];
    soft_texel(T->fmt, a3d_le16(T->base + 2u * a[0]), c00);
    if (!T->bilinear)
    {
        __builtin_memcpy(out, c00, sizeof(c00));
        return;
    }
    uint32_t c01[4], c10[4], c11[4], m0[4], m1[4];
    soft_texel(T->fmt, a3d_le16(T->base + 2u * a[1]), c01);
    soft_texel(T->fmt, a3d_le16(T->base + 2u * a[2]), c10);
    soft_texel(T->fmt, a3d_le16(T->base + 2u * a[3]), c11);
    const uint32_t is = (0xFFFFu - subu) >> 8, it = (0xFFFFu - subv) >> 8;
    soft_mix(c00, c01, is, m0);
    soft_mix(c10, c11, is, m1);
    soft_mix(m0, m1, it, out);
}

/* Fog.v: the colour towards the fog colour by the intensity of the interpolated 1/w, alpha kept */
void a3d_soft_fog_apply(const uint32_t lut[RIX_FOG_LUT_WORDS], const uint32_t fogc[4], float x, uint32_t c[4])
{
    const uint32_t fi = a3d_soft_fog(lut, x), a = c[3];
    soft_mix(c, fogc, fi, c);
    c[3] = a;
}

/* FunctionInterpolator.v: the fog intensity (0 .. 255, 255 = no fog) of an interpolated 1/w */
uint32_t a3d_soft_fog(const uint32_t lut[RIX_FOG_LUT_WORDS], float x)
{
    uint32_t xb;
    __builtin_memcpy(&xb, &x, 4);
    if (xb >= 0x3F000000u)
        return 255u;
    if (xb <= 0x2F800000u)
        return 0u;
    const uint32_t idx = (126u - ((xb >> 23) & 0xFFu)) & 31u;
    const int32_t m = (int32_t)lut[2u * idx] >> 8, b = (int32_t)lut[2u * idx + 1u] >> 8;   /* the top 24 bits */
    const int32_t xs = 256 - (int32_t)((xb & 0x7FFFFFu) >> 15);
    int32_t mxb = (int32_t)((uint32_t)(m * xs) << 8) >> 8;                                /* 24-bit wrap */
    mxb = (int32_t)((uint32_t)(mxb + b) << 8) >> 8;
    if (mxb < 0)
        return 0u;
    if (mxb & 0x400000)
        return 255u;
    return ((uint32_t)mxb >> 6 & 0xFFFFu) >> 8;           /* intensity[15:0] = fx[21:6], the top 8 bits */
}

/* one combiner argument: source (0 texture, 1 constant, 2 primary, 3 previous) and operand */
static uint32_t soft_arg(uint32_t src, uint32_t op, uint32_t ch, const uint32_t tex[4], const uint32_t cst[4],
                         const uint32_t prim[4], int alpha)
{
    const uint32_t *s = src == 0u ? tex : (src == 1u ? cst : prim);
    if (alpha)
        return op ? 255u - s[3] : s[3];                 /* 1-bit operand: 0 SRC_ALPHA, 1 ONE_MINUS */
    switch (op & 3u)
    {
    case 0: return s[3];                                /* SRC_ALPHA            */
    case 1: return 255u - s[3];                         /* ONE_MINUS_SRC_ALPHA  */
    case 2: return s[ch];                               /* SRC_COLOR            */
    default: return 255u - s[ch];                       /* ONE_MINUS_SRC_COLOR  */
    }
}

static uint32_t soft_combine(uint32_t comb, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t shift)
{
    int32_t v;
    switch (comb & 7u)
    {
    case 0: v = (int32_t)a0; break;                                         /* REPLACE      */
    case 1: v = (int32_t)((a0 * a1 + 127u) / 255u); break;                  /* MODULATE     */
    case 2: v = (int32_t)(a0 + a1); break;                                  /* ADD          */
    case 3: v = (int32_t)(a0 + a1) - 128; break;                            /* ADD_SIGNED   */
    case 4: v = (int32_t)((a0 * a2 + a1 * (255u - a2) + 127u) / 255u); break; /* INTERPOLATE */
    case 5: v = (int32_t)a0 - (int32_t)a1; break;                           /* SUBTRACT     */
    default:                                                                /* DOT3: per channel approximation */
        v = 4 * (((int32_t)a0 - 128) * ((int32_t)a1 - 128)) / 255 + 128; break;
    }
    v <<= (shift & 3u);
    return c8(v);
}

static uint32_t soft_factor(uint32_t f, uint32_t ch, const uint32_t src[4], const uint32_t dst[4])
{
    switch (f & 15u)
    {
    case 0: return 0u;                      /* ZERO                 */
    case 1: return 255u;                    /* ONE                  */
    case 2: return dst[ch];                 /* DST_COLOR            */
    case 3: return src[ch];                 /* SRC_COLOR            */
    case 4: return 255u - dst[ch];          /* ONE_MINUS_DST_COLOR  */
    case 5: return 255u - src[ch];          /* ONE_MINUS_SRC_COLOR  */
    case 6: return src[3];                  /* SRC_ALPHA            */
    case 7: return 255u - src[3];           /* ONE_MINUS_SRC_ALPHA  */
    case 8: return dst[3];                  /* DST_ALPHA            */
    case 9: return 255u - dst[3];           /* ONE_MINUS_DST_ALPHA  */
    default:                                /* SRC_ALPHA_SATURATE   */
        if (ch == 3u) return 255u;
        return src[3] < 255u - dst[3] ? src[3] : 255u - dst[3];
    }
}

static uint16_t soft_logic(uint32_t op, uint16_t s, uint16_t d)
{
    switch (op & 15u)
    {
    case 0: return 0;
    case 1: return 0xFFFF;
    case 2: return s;
    case 3: return (uint16_t)~s;
    case 4: return d;
    case 5: return (uint16_t)~d;
    case 6: return (uint16_t)(s & d);
    case 7: return (uint16_t)~(s & d);
    case 8: return (uint16_t)(s | d);
    case 9: return (uint16_t)~(s | d);
    case 10: return (uint16_t)(s ^ d);
    case 11: return (uint16_t)~(s ^ d);
    case 12: return (uint16_t)(s & ~d);
    case 13: return (uint16_t)(~s & d);
    case 14: return (uint16_t)(s | ~d);
    default: return (uint16_t)(~s | d);
    }
}

static float soft_f(uint32_t u)
{
    float f;
    __builtin_memcpy(&f, &u, 4);
    return f;
}

static int64_t soft_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py)
{
    return (int64_t)(bx - ax) * (py - ay) - (int64_t)(by - ay) * (px - ax);
}

static inline float soft_i64f(int64_t v)
{
    return (float)(int32_t)(v >> 32) * 4294967296.0f + (float)(uint32_t)v;
}

/* a TRIANGLE_VERTS into the current band; returns the work spent */
static uint32_t soft_tri(const a3d_listset *ls, const soft_st *s, const uint8_t *pl)
{
    uint32_t w[A3D_TV_WORDS];
    for (uint32_t i = 0; i < A3D_TV_WORDS; i++)
        w[i] = rix_get32(pl + 4u * i);
    int32_t X[3], Y[3];
    float Z[3], IW[3], SW[3], TW[3], C[3][4];
    const int32_t h16 = (int32_t)(16u * ls->height);
    for (uint32_t i = 0; i < 3u; i++)
    {
        const uint32_t *v = &w[6u * i];
        X[i] = (int16_t)(v[0] & 0xFFFFu);
        Y[i] = h16 - (int32_t)(int16_t)(v[0] >> 16);
        Z[i] = (float)v[1] * (1.0f / 1073741824.0f);
        IW[i] = soft_f(v[2]);
        SW[i] = soft_f(v[3]) * IW[i];
        TW[i] = soft_f(v[4]) * IW[i];
        C[i][0] = (float)(v[5] >> 24);
        C[i][1] = (float)((v[5] >> 16) & 0xFFu);
        C[i][2] = (float)((v[5] >> 8) & 0xFFu);
        C[i][3] = (float)(v[5] & 0xFFu);
    }
    int64_t area = (int64_t)(X[1] - X[0]) * (Y[2] - Y[0]) - (int64_t)(X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0)
        return 64u;
    if (area < 0)
    {
        /* one orientation for the edge functions: swap vertices 1 and 2 */
        int32_t ti; float tf;
        ti = X[1]; X[1] = X[2]; X[2] = ti;
        ti = Y[1]; Y[1] = Y[2]; Y[2] = ti;
        tf = Z[1]; Z[1] = Z[2]; Z[2] = tf;
        tf = IW[1]; IW[1] = IW[2]; IW[2] = tf;
        tf = SW[1]; SW[1] = SW[2]; SW[2] = tf;
        tf = TW[1]; TW[1] = TW[2]; TW[2] = tf;
        for (uint32_t k = 0; k < 4u; k++) { tf = C[1][k]; C[1][k] = C[2][k]; C[2][k] = tf; }
        area = -area;
    }
    const uint32_t res = s->reg[RIX_REG_RENDER_RESOLUTION];
    const int32_t width = (int32_t)(res & 0x7FFu), rows = (int32_t)((res >> 16) & 0x7FFu);
    const int32_t by0 = (int32_t)ls->height - (int32_t)((s->reg[RIX_REG_Y_OFFSET] >> 16) & 0x7FFu) - rows;
    int32_t x0 = width, x1 = -1, y0 = by0 + rows, y1 = by0 - 1;
    /* the bounding box of the pixel centres the triangle may cover */
    int32_t minx = X[0], maxx = X[0], miny = Y[0], maxy = Y[0];
    for (uint32_t i = 1; i < 3u; i++)
    {
        if (X[i] < minx) minx = X[i];
        if (X[i] > maxx) maxx = X[i];
        if (Y[i] < miny) miny = Y[i];
        if (Y[i] > maxy) maxy = Y[i];
    }
    x0 = (minx - 8 + 15) >> 4;  x1 = (maxx - 8) >> 4;
    y0 = (miny - 8 + 15) >> 4;  y1 = (maxy - 8) >> 4;
    if (x0 < 0) x0 = 0;
    if (x1 > width - 1) x1 = width - 1;
    if (y0 < by0) y0 = by0;
    if (y1 > by0 + rows - 1) y1 = by0 + rows - 1;
    const uint32_t fe = s->reg[RIX_REG_FEATURE_ENABLE];
    if (fe & (1u << 5))
    {
        const int32_t sx0 = (int32_t)(s->reg[RIX_REG_SCISSOR_START] & 0xFFFFu);
        const int32_t sy0 = (int32_t)ls->height - (int32_t)(s->reg[RIX_REG_SCISSOR_END] >> 16);
        const int32_t sx1 = (int32_t)(s->reg[RIX_REG_SCISSOR_END] & 0xFFFFu);
        const int32_t sy1 = (int32_t)ls->height - (int32_t)(s->reg[RIX_REG_SCISSOR_START] >> 16);
        if (x0 < sx0) x0 = sx0;
        if (y0 < sy0) y0 = sy0;
        if (x1 > sx1 - 1) x1 = sx1 - 1;
        if (y1 > sy1 - 1) y1 = sy1 - 1;
    }
    if (x0 > x1 || y0 > y1 || width <= 0)
        return 64u;
    const uint32_t textured = (fe >> 6) & 1u;
    const uint32_t tr = s->reg[RIX_REG_TMU_TEXTURE(0)];
    soft_tu tu;
    tu.lw = tr & 15u;
    tu.lh = (tr >> 4) & 15u;
    tu.clamps = (tr >> 8) & 1u;
    tu.clampt = (tr >> 9) & 1u;
    tu.bilinear = (tr >> 10) & 1u;
    tu.mip = (tr >> 11) & 1u;
    tu.fmt = (tr >> 12) & 15u;
    tu.base = 0;
    if (tu.lw > 8u || tu.lh > 8u)
        tu.lw = tu.lh = 0;
    /* the block holds level 0, and the whole chain when mipmapping (any LOD past it reads the 1x1 level) */
    const uint32_t chain = (tu.lw > tu.lh ? tu.lw : tu.lh) + 1u;
    const uint32_t tex_bytes = tu.mip ? 2u * (a3d_soft_lvl_off(tu.lw, tu.lh, chain - 1u) + 1u) : 2u << (tu.lw + tu.lh);
    if (textured && s->npages)
    {
        int contiguous = 1;
        for (uint32_t k = 1; k < s->npages; k++)
            if (s->page[k] != s->page[0] + 4096u * k)
                contiguous = 0;
        if (contiguous && tex_bytes <= s->npages * 4096u)
            tu.base = soft_va(ls, s->page[0], tex_bytes);
        if (tu.base == 0)
            a3d_cnt.list_errors++;
    }
    const uint8_t *tex = tu.base;
    const uint32_t fogging = fe & 1u;
    const uint32_t fc = s->reg[RIX_REG_FOG_COLOR];
    const uint32_t fogc[4] = { fc >> 24, (fc >> 16) & 0xFFu, (fc >> 8) & 0xFFu, fc & 0xFFu };
    const uint32_t te = s->reg[RIX_REG_TEX_ENV(0)];
    const uint32_t tc = s->reg[RIX_REG_TEX_ENV_COLOR(0)];
    const uint32_t cst[4] = { tc >> 24, (tc >> 16) & 0xFFu, (tc >> 8) & 0xFFu, tc & 0xFFu };
    const uint32_t fp = s->reg[RIX_REG_FRAGMENT_PIPELINE];
    const uint32_t blending = (fe >> 1) & 1u, depth = (fe >> 2) & 1u, atest = (fe >> 3) & 1u, lop = (fe >> 8) & 1u;
    const float inv_area = 1.0f / soft_i64f(area);
    uint32_t spent = 64u;
    for (int32_t py = y0; py <= y1; py++)
    {
        const int32_t cy = py * 16 + 8;
        for (int32_t px = x0; px <= x1; px++)
        {
            spent++;
            const int32_t cx = px * 16 + 8;
            const int64_t e0 = soft_edge(X[1], Y[1], X[2], Y[2], cx, cy);
            const int64_t e1 = soft_edge(X[2], Y[2], X[0], Y[0], cx, cy);
            const int64_t e2 = soft_edge(X[0], Y[0], X[1], Y[1], cx, cy);
            if (e0 < 0 || e1 < 0 || e2 < 0)
                continue;
            if (e0 == 0 && !((Y[2] - Y[1]) < 0 || ((Y[2] - Y[1]) == 0 && (X[2] - X[1]) > 0)))
                continue;
            if (e1 == 0 && !((Y[0] - Y[2]) < 0 || ((Y[0] - Y[2]) == 0 && (X[0] - X[2]) > 0)))
                continue;
            if (e2 == 0 && !((Y[1] - Y[0]) < 0 || ((Y[1] - Y[0]) == 0 && (X[1] - X[0]) > 0)))
                continue;
            const float b0 = soft_i64f(e0) * inv_area, b1 = soft_i64f(e1) * inv_area, b2 = soft_i64f(e2) * inv_area;
            const uint32_t idx = (uint32_t)((py - by0) * width + px);
            if (idx >= SOFT_BAND_PX)
                continue;
            float z = b0 * Z[0] + b1 * Z[1] + b2 * Z[2];
            uint32_t z16 = z > 0.0f ? (z < 1.0f ? (uint32_t)(z * 65536.0f) : 0xFFFFu) : 0u;
            if (z16 > 0xFFFFu) z16 = 0xFFFFu;
            if (depth && !soft_test(fp & 7u, z16, g_dep[idx]))
                continue;
            uint32_t prim[4], texel[4] = { 255u, 255u, 255u, 255u }, frag[4];
            for (uint32_t k = 0; k < 4u; k++)
            {
                const float cf = b0 * C[0][k] + b1 * C[1][k] + b2 * C[2][k] + 0.5f;
                prim[k] = cf > 0.0f ? (cf < 255.0f ? (uint32_t)cf : 255u) : 0u;
            }
            const float iw = b0 * IW[0] + b1 * IW[1] + b2 * IW[2];
            if (tex)
            {
                float ss = b0 * SW[0] + b1 * SW[1] + b2 * SW[2];
                float tt = b0 * TW[0] + b1 * TW[1] + b2 * TW[2];
                if (iw != 0.0f)
                {
                    ss /= iw;
                    tt /= iw;
                }
                const int32_t S = soft_s1615(ss), Tt = soft_s1615(tt);
                uint32_t lod = 0;
                if (tu.mip)
                {
                    /* the coordinate one pixel right and down (the RTL's mipmap sample) */
                    const int64_t f0 = soft_edge(X[1], Y[1], X[2], Y[2], cx + 16, cy + 16);
                    const int64_t f1 = soft_edge(X[2], Y[2], X[0], Y[0], cx + 16, cy + 16);
                    const int64_t f2 = soft_edge(X[0], Y[0], X[1], Y[1], cx + 16, cy + 16);
                    const float c0 = soft_i64f(f0) * inv_area, c1 = soft_i64f(f1) * inv_area, c2 = soft_i64f(f2) * inv_area;
                    const float iw2 = c0 * IW[0] + c1 * IW[1] + c2 * IW[2];
                    float s2 = c0 * SW[0] + c1 * SW[1] + c2 * SW[2], t2 = c0 * TW[0] + c1 * TW[1] + c2 * TW[2];
                    if (iw2 != 0.0f)
                    {
                        s2 /= iw2;
                        t2 /= iw2;
                    }
                    lod = a3d_soft_lod(tu.lw, tu.lh, S, Tt, soft_s1615(s2), soft_s1615(t2));
                }
                a3d_soft_sample(&tu, lod, S, Tt, texel);
            }
            if (textured)
            {
                const uint32_t cr = te & 7u, ca = (te >> 3) & 7u;
                const uint32_t srgb[3] = { (te >> 6) & 3u, (te >> 8) & 3u, (te >> 10) & 3u };
                const uint32_t sa[3] = { (te >> 12) & 3u, (te >> 14) & 3u, (te >> 16) & 3u };
                const uint32_t orgb[3] = { (te >> 18) & 3u, (te >> 20) & 3u, (te >> 22) & 3u };
                const uint32_t oa[3] = { (te >> 24) & 1u, (te >> 25) & 1u, (te >> 26) & 1u };
                for (uint32_t ch = 0; ch < 3u; ch++)
                    frag[ch] = soft_combine(cr, soft_arg(srgb[0], orgb[0], ch, texel, cst, prim, 0),
                                            soft_arg(srgb[1], orgb[1], ch, texel, cst, prim, 0),
                                            soft_arg(srgb[2], orgb[2], ch, texel, cst, prim, 0), (te >> 27) & 3u);
                frag[3] = soft_combine(ca, soft_arg(sa[0], oa[0], 3u, texel, cst, prim, 1),
                                       soft_arg(sa[1], oa[1], 3u, texel, cst, prim, 1),
                                       soft_arg(sa[2], oa[2], 3u, texel, cst, prim, 1), (te >> 29) & 3u);
            }
            else
                for (uint32_t k = 0; k < 4u; k++)
                    frag[k] = prim[k];
            if (atest && !soft_test((fp >> 3) & 7u, frag[3], (fp >> 6) & 0xFFu))
                continue;
            if (fogging)
                a3d_soft_fog_apply(s->fog, fogc, iw, frag);
            const uint16_t old = g_col[idx];
            const uint32_t dst[4] = { ((old >> 11) & 31u) * 255u / 31u, ((old >> 5) & 63u) * 255u / 63u,
                                      (old & 31u) * 255u / 31u, 0u };   /* no alpha channel: RasterIX reads 0 */
            uint32_t out[3];
            if (blending)
            {
                const uint32_t sf = (fp >> 19) & 15u, df = (fp >> 23) & 15u;
                for (uint32_t ch = 0; ch < 3u; ch++)
                    out[ch] = c8((int32_t)((frag[ch] * soft_factor(sf, ch, frag, dst)
                                            + dst[ch] * soft_factor(df, ch, frag, dst) + 127u) / 255u));
            }
            else
                for (uint32_t ch = 0; ch < 3u; ch++)
                    out[ch] = frag[ch];
            uint16_t nv = (uint16_t)(((out[0] >> 3) << 11) | ((out[1] >> 2) << 5) | (out[2] >> 3));
            if (lop)
                nv = soft_logic((fp >> 27) & 15u, nv, old);
            /* colour mask: bits 18 R, 17 G, 16 B */
            uint16_t keep = 0;
            if (!((fp >> 18) & 1u)) keep |= 0xF800u;
            if (!((fp >> 17) & 1u)) keep |= 0x07E0u;
            if (!((fp >> 16) & 1u)) keep |= 0x001Fu;
            g_col[idx] = (uint16_t)((nv & ~keep) | (old & keep));
            if (depth && ((fp >> 14) & 1u))
                g_dep[idx] = (uint16_t)z16;
            a3d_cnt.soft_pixels++;
        }
    }
    return spent;
}

A3D_TAIL_CALL static void soft_fb_colour(uint8_t *p, uint32_t size, int load)
{
    if (load && a3d_soft_le)
        for (uint32_t i = 0; i < size; i++)
            g_col[i] = a3d_le16(p + 2u * i);
    else if (load)
        for (uint32_t i = 0; i < size; i++)
            g_col[i] = a3d_be16(p + 2u * i);
    else
    {
        if (a3d_soft_le)
            for (uint32_t i = 0; i < size; i++)
                a3d_wle16(p + 2u * i, g_col[i]);
        else
            for (uint32_t i = 0; i < size; i++)
                a3d_wbe16(p + 2u * i, g_col[i]);
        a3d_plat_clean_nc(p, size * 2u);
    }
}

/* a FRAMEBUFFER command on the band buffers */
static void soft_fb(const a3d_listset *ls, const soft_st *s, uint32_t op)
{
    const uint32_t size = (op >> RIX_FB_SIZE_SHIFT) & RIX_FB_SIZE_MASK;
    const uint32_t res = s->reg[RIX_REG_RENDER_RESOLUTION];
    const uint32_t band_px = (res & 0x7FFu) * ((res >> 16) & 0x7FFu);
    if (size == 0 || size > SOFT_BAND_PX || size > band_px)
    {
        a3d_cnt.list_errors++;
        return;
    }
    if (op & RIX_FB_MEMSET)
    {
        if (op & RIX_FB_COLOR)
        {
            const uint32_t cc = s->reg[RIX_REG_CLEAR_COLOR];
            const uint16_t v = (uint16_t)(((cc >> 27) << 11) | (((cc >> 18) & 63u) << 5) | ((cc >> 11) & 31u));
            for (uint32_t i = 0; i < size; i++)
                g_col[i] = v;
        }
        if (op & RIX_FB_DEPTH)
            for (uint32_t i = 0; i < size; i++)
                g_dep[i] = (uint16_t)s->reg[RIX_REG_CLEAR_DEPTH];
        return;
    }
    const int load = (op & RIX_FB_LOAD) != 0, commit = (op & RIX_FB_COMMIT) != 0;
    if (!load && !commit)
    {
        a3d_cnt.list_errors++;
        return;
    }
    if (op & RIX_FB_COLOR)
    {
        uint8_t *p = soft_va(ls, s->reg[RIX_REG_COLOR_BUFFER_ADDR], size * 2u);
        if (p == 0)
        {
            a3d_cnt.list_errors++;
            return;
        }
        soft_fb_colour(p, size, load);
    }
    if (op & RIX_FB_DEPTH)
    {
        uint8_t *p = soft_va(ls, s->reg[RIX_REG_DEPTH_BUFFER_ADDR], size * 2u);
        if (p == 0)
        {
            a3d_cnt.list_errors++;
            return;
        }
        if (load)
            for (uint32_t i = 0; i < size; i++)
                g_dep[i] = a3d_le16(p + 2u * i);
        else
            for (uint32_t i = 0; i < size; i++)
                a3d_wle16(p + 2u * i, g_dep[i]);
    }
}

static uint32_t soft_band(const a3d_listset *ls, uint32_t b)
{
    a3d_walk wk;
    const int ok = a3d_walk_begin(&wk, ls, b);
    if (ok == 0)
        return 1u;
    if (ok < 0)
    {
        a3d_cnt.list_errors++;
        return 1u;
    }
    soft_st s;
    soft_defaults(&s);
    uint32_t spent = 1u;
    a3d_cmd c;
    int r;
    while ((r = a3d_walk_next(&wk, &c)) > 0)
    {
        switch (c.op & RIX_OP_MASK)
        {
        case RIX_OP_RENDER_CONFIG:
        {
            const uint32_t a = c.op & 0x0FFFFFFFu;
            if (a < 19u)
                s.reg[a] = rix_get32(c.payload);
            break;
        }
        case RIX_OP_FRAMEBUFFER:
            soft_fb(ls, &s, c.op);
            spent += 16u;
            break;
        case RIX_OP_TEXTURE_STREAM:
            if (((c.op >> RIX_TEX_TMU_SHIFT) & RIX_TEX_TMU_MASK) == 0u)
            {
                s.npages = c.n;
                for (uint32_t i = 0; i < c.n; i++)
                    s.page[i] = rix_get32(c.payload + 4u * i);
            }
            break;
        case A3D_OP_TRIANGLE_VERTS:
            spent += soft_tri(ls, &s, c.payload);
            break;
        case RIX_OP_FOG_LUT_STREAM:
            for (uint32_t i = 0; i < RIX_FOG_LUT_WORDS && i < c.n; i++)
                s.fog[i] = rix_get32(c.payload + 4u * i);
            break;
        default:
            break;                  /* NOP */
        }
    }
    if (r < 0)
        a3d_cnt.list_errors++;
    a3d_cnt.soft_bands++;
    return spent;
}

static uint32_t a3d_soft_progress(uint32_t budget);
static int a3d_soft_submit(const a3d_listset *ls)
{
    for (uint32_t i = 0; i < SOFT_JOBS; i++)
        if (!g_job[i].live)
        {
            g_job[i].ls = *ls;
            g_job[i].next = 0;
            g_job[i].live = 1;
            return 0;
        }
    return W3D_QUEUEFAILED;
}

static uint32_t a3d_soft_progress(uint32_t budget)
{
    uint32_t spent = 0;
    for (;;)
    {
        /* the oldest live job */
        soft_job *j = 0;
        for (uint32_t i = 0; i < SOFT_JOBS; i++)
            if (g_job[i].live && (j == 0 || g_job[i].ls.fence < j->ls.fence))
                j = &g_job[i];
        if (j == 0)
            break;
        while (j->next < j->ls.bands && spent < budget)
            spent += soft_band(&j->ls, j->next++);
        if (j->next < j->ls.bands)
            break;                  /* the budget ran out mid-set */
        j->live = 0;
        g_soft_done = j->ls.fence;
        if (spent >= budget)
            break;
    }
    return g_soft_done;
}

static uint32_t a3d_soft_completed(void)
{
    return g_soft_done;
}

static void a3d_soft_reset(void)
{
    memset(g_job, 0, sizeof(g_job));
    g_soft_done = 0;
}

static uint32_t a3d_soft_units(void)
{
    return 0;
}

static uint32_t a3d_soft_orders(void)
{
    return A3D_ORD_BE;
}

A3D_TAIL_TEXT static uint32_t a3d_softle_orders(void)
{
    return A3D_ORD_LE;
}

const a3d_backend a3d_be_soft = {
    "SOFT", A3D_BE_SOFT, a3d_soft_submit, a3d_soft_progress, a3d_soft_completed, a3d_soft_reset,
    0u,     /* not async: core #1 renders every submission (a wait renders it in bounded slices) */
    0u,     /* colour big-endian: the Amiga's RGB565 (W3D_FMT_R5G6B5) is rendered in place */
    a3d_soft_units,
    a3d_soft_orders
};

A3D_TAIL_RODATA const a3d_backend a3d_be_softle = {
    "SOFT-LE", A3D_BE_SOFT, a3d_soft_submit, a3d_soft_progress, a3d_soft_completed, a3d_soft_reset,
    0u,     /* not async */
    1u,
    a3d_soft_units,
    a3d_softle_orders
};
