/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: textures. */

#include "a3d_agent.h"

#define RIX_PF_RGBA4444 0u
#define RIX_PF_RGBA5551 1u
#define RIX_PF_RGB565   2u

struct a3d_texobj
{
    uint32_t used, gen;
    uint32_t ctx;               /* owning context index                                   */
    uint32_t gtex, gpriv;       /* guest W3D_Texture and its private block                */
    uint32_t w, h;              /* stored level 0: powers of two, <= 256                  */
    uint32_t lw, lh;            /* their log2                                             */
    uint32_t levels;            /* levels stored: 1, or real_levels when mipmapped        */
    uint32_t real_levels;
    uint32_t sw, sh;            /* the guest's level-0 size                               */
    float srcw, srch;           /* the same as floats: texel coordinates are divided by it */
    uint32_t wfmt;              /* the guest's format (W3D_*)                              */
    uint32_t fmt;               /* RIX_PF_*                                               */
    uint32_t genmask;           /* bit l: stored level l is generated from level l - 1    */
    uint8_t *data;
    uint32_t phys, bytes;
    uint32_t last_fence;        /* the last submission that sampled it                    */
    uint32_t dead;              /* freed by the guest; the block waits for last_fence     */
    uint32_t ugen;              /* bumped at every rename (a context re-sends the stream)  */
};
static a3d_texobj g_tex[A3D_MAX_TEX];
static uint32_t g_tex_done;     /* the last completed fence seen                          */
static uint32_t g_tex_maybe_dead;
uint32_t a3d_tex_l2_dirty;      /* the ARM wrote texels since the last FABRIC submission   */

/* blocks a rename left behind: freed at the fence of their last use */
#define A3D_RETIRED     64u
static struct { uint8_t *data; uint32_t fence; } g_retired[A3D_RETIRED];

static uint32_t a3d_log2(uint32_t v)
{
    uint32_t l = 0;
    while ((1u << (l + 1u)) <= v)
        l++;
    return l;
}

/* level l of a w x h texture (both powers of two): RasterIX's sizes, 1 at the end of either axis */
static uint32_t a3d_lvl_w(const a3d_texobj *t, uint32_t l)
{
    return l < t->lw ? t->w >> l : 1u;
}

static uint32_t a3d_lvl_h(const a3d_texobj *t, uint32_t l)
{
    return l < t->lh ? t->h >> l : 1u;
}

/* the byte offset of level l in the block: the sum of the sizes before it (TextureSampler.v's offset) */
static uint32_t a3d_lvl_off(const a3d_texobj *t, uint32_t l)
{
    uint32_t off = 0;
    for (uint32_t k = 0; k < l; k++)
        off += a3d_lvl_w(t, k) * a3d_lvl_h(t, k) * 2u;
    return off;
}

static void a3d_tex_release(a3d_texobj *t)
{
    if (t->data)
        a3d_pool_free(t->data);
    const uint32_t gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->gen = gen;
    if (a3d_cnt.textures)
        a3d_cnt.textures--;
}

static void a3d_dec16(uint32_t fmt, uint16_t v, uint32_t c[4])
{
    switch (fmt)
    {
    case RIX_PF_RGB565:
        c[0] = ((v >> 11) & 31u) * 255u / 31u; c[1] = ((v >> 5) & 63u) * 255u / 63u;
        c[2] = (v & 31u) * 255u / 31u; c[3] = 255u;
        break;
    case RIX_PF_RGBA5551:
        c[0] = ((v >> 11) & 31u) * 255u / 31u; c[1] = ((v >> 6) & 31u) * 255u / 31u;
        c[2] = ((v >> 1) & 31u) * 255u / 31u; c[3] = (v & 1u) ? 255u : 0u;
        break;
    default:
        c[0] = ((v >> 12) & 15u) * 17u; c[1] = ((v >> 8) & 15u) * 17u;
        c[2] = ((v >> 4) & 15u) * 17u; c[3] = (v & 15u) * 17u;
        break;
    }
}

static uint16_t a3d_enc16(uint32_t fmt, const uint32_t c[4])
{
    switch (fmt)
    {
    case RIX_PF_RGB565:
        return (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
    case RIX_PF_RGBA5551:
        return (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 3) << 6) | ((c[2] >> 3) << 1) | (c[3] >= 128u ? 1u : 0u));
    default:
        return (uint16_t)(((c[0] >> 4) << 12) | ((c[1] >> 4) << 8) | ((c[2] >> 4) << 4) | (c[3] >> 4));
    }
}

/* one source texel of a Warp3D format, as 8-bit RGBA */
static void a3d_src_texel(uint32_t wfmt, const uint8_t *p, uint32_t c[4])
{
    switch (wfmt)
    {
    case W3D_R8G8B8:   c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; c[3] = 255u; break;
    case W3D_R8G8B8A8: c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; c[3] = p[3]; break;
    case W3D_A8R8G8B8: c[3] = p[0]; c[0] = p[1]; c[1] = p[2]; c[2] = p[3]; break;
    case W3D_R5G6B5:
    {
        const uint32_t v = a3d_be16(p);
        c[0] = ((v >> 11) & 31u) * 255u / 31u; c[1] = ((v >> 5) & 63u) * 255u / 63u;
        c[2] = (v & 31u) * 255u / 31u; c[3] = 255u;
        break;
    }
    case W3D_A1R5G5B5:
    {
        const uint32_t v = a3d_be16(p);
        c[3] = (v & 0x8000u) ? 255u : 0u; c[0] = ((v >> 10) & 31u) * 255u / 31u;
        c[1] = ((v >> 5) & 31u) * 255u / 31u; c[2] = (v & 31u) * 255u / 31u;
        break;
    }
    case W3D_A4R4G4B4:
    {
        const uint32_t v = a3d_be16(p);
        c[3] = ((v >> 12) & 15u) * 17u; c[0] = ((v >> 8) & 15u) * 17u;
        c[1] = ((v >> 4) & 15u) * 17u; c[2] = (v & 15u) * 17u;
        break;
    }
    case W3D_A8:       c[0] = c[1] = c[2] = 255u; c[3] = p[0]; break;
    case W3D_L8:       c[0] = c[1] = c[2] = p[0]; c[3] = 255u; break;
    case W3D_L8A8:     c[0] = c[1] = c[2] = p[0]; c[3] = p[1]; break;
    case W3D_I8:
    default:           c[0] = c[1] = c[2] = c[3] = p[0]; break;
    }
}

static uint32_t a3d_src_bpp(uint32_t wfmt)
{
    switch (wfmt)
    {
    case W3D_R8G8B8:   return 3u;
    case W3D_R8G8B8A8:
    case W3D_A8R8G8B8: return 4u;
    case W3D_R5G6B5:
    case W3D_A1R5G5B5:
    case W3D_A4R4G4B4:
    case W3D_L8A8:     return 2u;
    case W3D_A8:
    case W3D_L8:
    case W3D_I8:       return 1u;
    default:           return 0u;
    }
}

/* the source size of guest level L (Warp3D halves each axis down to 1) */
static uint32_t a3d_src_dim(uint32_t s, uint32_t L)
{
    return L < 32u && (s >> L) ? s >> L : 1u;
}

static void a3d_convert(const a3d_texobj *t, uint8_t *dst, uint32_t dw, uint32_t dh, const uint8_t *src,
                        uint32_t sw, uint32_t sh, uint32_t sbpr, uint32_t sx0, uint32_t sy0, uint32_t rw,
                        uint32_t rh)
{
    const uint32_t bpp = a3d_src_bpp(t->wfmt);
    const uint32_t xa = (sx0 * dw + sw - 1u) / sw;
    const uint32_t xb = ((sx0 + rw) * dw + sw - 1u) / sw;
    const uint32_t ya = (sy0 * dh + sh - 1u) / sh;
    const uint32_t yb = ((sy0 + rh) * dh + sh - 1u) / sh;
    for (uint32_t y = ya; y < yb && y < dh; y++)
    {
        const uint32_t sy = (y * sh) / dh;
        if (sy < sy0 || sy >= sy0 + rh)
            continue;
        const uint8_t *row = src + (sy - sy0) * sbpr;
        for (uint32_t x = xa; x < xb && x < dw; x++)
        {
            const uint32_t sx = (x * sw) / dw;
            if (sx < sx0 || sx >= sx0 + rw)
                continue;
            uint32_t cc[4];
            a3d_src_texel(t->wfmt, row + (sx - sx0) * bpp, cc);
            a3d_wle16(dst + 2u * (y * dw + x), a3d_enc16(t->fmt, cc));
        }
    }
}

/* stored level l (>= 1) from level l - 1: a 2x2 box filter */
static void a3d_genlevel(const a3d_texobj *t, uint8_t *data, uint32_t l)
{
    const uint32_t pw = a3d_lvl_w(t, l - 1u), ph = a3d_lvl_h(t, l - 1u);
    const uint32_t ww = a3d_lvl_w(t, l), hh = a3d_lvl_h(t, l);
    const uint8_t *prev = data + a3d_lvl_off(t, l - 1u);
    uint8_t *out = data + a3d_lvl_off(t, l);
    for (uint32_t y = 0; y < hh; y++)
        for (uint32_t x = 0; x < ww; x++)
        {
            uint32_t acc[4] = { 0, 0, 0, 0 };
            for (uint32_t k = 0; k < 4u; k++)
            {
                const uint32_t xx = (2u * x + (k & 1u)) < pw ? 2u * x + (k & 1u) : pw - 1u;
                const uint32_t yy = (2u * y + (k >> 1)) < ph ? 2u * y + (k >> 1) : ph - 1u;
                uint32_t cc[4];
                a3d_dec16(t->fmt, a3d_le16(prev + 2u * (yy * pw + xx)), cc);
                for (uint32_t j = 0; j < 4u; j++)
                    acc[j] += cc[j];
            }
            for (uint32_t j = 0; j < 4u; j++)
                acc[j] = (acc[j] + 2u) / 4u;
            a3d_wle16(out + 2u * (y * ww + x), a3d_enc16(t->fmt, acc));
        }
}

/* regenerate the driver-generated levels below `from` (after level from - 1 changed) */
static void a3d_regen(const a3d_texobj *t, uint8_t *data, uint32_t from)
{
    for (uint32_t l = from; l < t->real_levels; l++)
        if ((t->genmask >> l) & 1u)
            a3d_genlevel(t, data, l);
}

uint32_t a3d_tex_lvl_off(uint32_t lw, uint32_t lh, uint32_t l)
{
    a3d_texobj T;
    memset(&T, 0, sizeof(T));
    T.lw = lw;
    T.lh = lh;
    T.w = 1u << lw;
    T.h = 1u << lh;
    return a3d_lvl_off(&T, l) / 2u;
}

/* the source-level -> stored-level shift: 0 unless the guest's level 0 was resampled down */
static uint32_t a3d_tex_shift(const a3d_texobj *t)
{
    if (t->sw == t->w && t->sh == t->h)
        return 0;
    const uint32_t a = a3d_log2(t->sw) > t->lw ? a3d_log2(t->sw) - t->lw : 0u;
    const uint32_t b = a3d_log2(t->sh) > t->lh ? a3d_log2(t->sh) - t->lh : 0u;
    return a > b ? a : b;
}

uint32_t a3d_tex_handle(const a3d_texobj *t)
{
    return t ? ((t->gen << 16) | ((uint32_t)(t - g_tex) + 1u)) : 0u;
}

uint32_t a3d_tex_ugen(const a3d_texobj *t)
{
    return t ? t->ugen : 0u;
}

static a3d_texobj *a3d_tex_from_priv(uint32_t gtex, uint32_t *gpriv_out)
{
    if ((gtex & 1u) || !a3d_g_ok(gtex, W3D_TEX_SIZE, A3D_GR_READ))
        return 0;
    const uint32_t tp = a3d_be32(a3d_g_ptr(gtex + W3D_TEX_DRIVER));
    if ((tp & 1u) || !a3d_g_ok(tp, A3D_TPRIV_SIZE, A3D_GR_READ | A3D_GR_WRITE))
        return 0;
    if (a3d_be32(a3d_g_ptr(tp + A3D_T_MAGIC)) != A3D_TPRIV_MAGIC)
        return 0;
    if (gpriv_out)
        *gpriv_out = tp;
    const uint32_t h = a3d_be32(a3d_g_ptr(tp + A3D_T_HANDLE));
    const uint32_t idx = (h & 0xFFFFu) - 1u;
    if (idx >= A3D_MAX_TEX)
        return 0;
    a3d_texobj *t = &g_tex[idx];
    if (!t->used || t->dead || t->gen != (h >> 16) || t->gtex != gtex || t->gpriv != tp)
        return 0;
    return t;
}

a3d_texobj *a3d_tex_lookup(uint32_t gtex)
{
    return a3d_tex_from_priv(gtex, 0);
}

static uint32_t a3d_supplied(const uint8_t *g, uint32_t L, uint32_t src, uint32_t bytes)
{
    const uint32_t m0 = a3d_be32(g + W3D_TEX_MIPMAPS);
    const uint32_t idx = (m0 != 0 && m0 == src) ? L : L - 1u;
    if (L == 0 || idx >= 16u)
        return 0;
    const uint32_t p = a3d_be32(g + W3D_TEX_MIPMAPS + 4u * idx);
    return p && a3d_g_ok(p, bytes, A3D_GR_READ) ? p : 0u;
}

int32_t a3d_tex_alloc(a3d_ctx *c, uint32_t gtex, uint32_t flags)
{
    uint32_t tp = 0;
    if (a3d_tex_from_priv(gtex, &tp) != 0)
        return W3D_ILLEGALINPUT;            /* already a texture */
    if (tp == 0 || !a3d_g_ok(gtex, W3D_TEX_SIZE, A3D_GR_READ | A3D_GR_WRITE))
        return W3D_ILLEGALINPUT;
    const uint8_t *g = a3d_g_ptr(gtex);
    const uint32_t src = a3d_be32(g + W3D_TEX_TEXSOURCE);
    const uint32_t wfmt = a3d_be32(g + W3D_TEX_TEXFMTSRC);
    const uint32_t sw = a3d_be32(g + W3D_TEX_TEXWIDTH);
    const uint32_t sh = a3d_be32(g + W3D_TEX_TEXHEIGHT);
    if (wfmt == W3D_CHUNKY)
        return W3D_UNSUPPORTEDTEXFMT;       /* M1d */
    const uint32_t bpp = a3d_src_bpp(wfmt);
    if (bpp == 0)
        return W3D_UNSUPPORTEDTEXFMT;
    if (sw == 0 || sh == 0 || sw > 2048u || sh > 2048u)
        return W3D_UNSUPPORTEDTEXSIZE;
    if (!a3d_g_ok(src, sw * sh * bpp, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    uint32_t idx = A3D_MAX_TEX;
    for (uint32_t i = 0; i < A3D_MAX_TEX; i++)
        if (!g_tex[i].used)
        {
            idx = i;
            break;
        }
    if (idx == A3D_MAX_TEX)
        return W3D_NOMEMORY;
    a3d_texobj T;
    memset(&T, 0, sizeof(T));
    /* the stored size: powers of two, at most 256 (resampled down) */
    T.lw = a3d_log2(sw < A3D_MAX_TEXSIZE ? sw : A3D_MAX_TEXSIZE);
    T.lh = a3d_log2(sh < A3D_MAX_TEXSIZE ? sh : A3D_MAX_TEXSIZE);
    T.w = 1u << T.lw;
    T.h = 1u << T.lh;
    T.sw = sw;
    T.sh = sh;
    T.wfmt = wfmt;
    /* the format: 565 without alpha; 5551 when every alpha is 0 or 255; else 4444 */
    T.fmt = RIX_PF_RGB565;
    if (wfmt == W3D_R8G8B8A8 || wfmt == W3D_A8R8G8B8 || wfmt == W3D_A1R5G5B5)
    {
        T.fmt = RIX_PF_RGBA5551;
        if (wfmt != W3D_A1R5G5B5)
        {
            const uint8_t *s = a3d_g_ptr(src);
            const uint32_t ao = wfmt == W3D_R8G8B8A8 ? 3u : 0u;
            for (uint32_t i = 0; i < sw * sh; i++)
            {
                const uint8_t a = s[4u * i + ao];
                if (a != 0u && a != 255u)
                {
                    T.fmt = RIX_PF_RGBA4444;
                    break;
                }
            }
        }
    }
    else if (wfmt == W3D_A4R4G4B4 || wfmt == W3D_A8 || wfmt == W3D_L8A8 || wfmt == W3D_I8)
        T.fmt = RIX_PF_RGBA4444;
    if (flags & (A3D_TA_MIPMAP | 2u))
        T.real_levels = T.levels = (T.lw > T.lh ? T.lw : T.lh) + 1u;
    else
        T.real_levels = T.levels = 1u;
    T.bytes = a3d_lvl_off(&T, T.levels);
    const uint32_t pages = (T.bytes + 4095u) / 4096u;
    if (pages > A3D_MAX_PAGES)
        return W3D_UNSUPPORTEDTEXSIZE;
    uint8_t *data = (uint8_t *)a3d_pool_alloc_t(pages * 4096u);
    if (data == 0)
        return W3D_NOMEMORY;
    /* level 0: nearest resampling from the guest image */
    a3d_convert(&T, data, T.w, T.h, a3d_g_ptr(src), sw, sh, sw * bpp, 0, 0, sw, sh);
    /* the mip chain: a level the program supplied is converted from its image, the others generated */
    const uint32_t shift = a3d_tex_shift(&T);
    for (uint32_t l = 1; l < T.real_levels; l++)
    {
        const uint32_t L = l + shift;      /* the guest's level that maps onto stored level l */
        const uint32_t lsw = a3d_src_dim(sw, L), lsh = a3d_src_dim(sh, L);
        const uint32_t p = a3d_supplied(g, L, src, lsw * lsh * bpp);
        if (p)
            a3d_convert(&T, data + a3d_lvl_off(&T, l), a3d_lvl_w(&T, l), a3d_lvl_h(&T, l), a3d_g_ptr(p), lsw, lsh,
                        lsw * bpp, 0, 0, lsw, lsh);
        else
        {
            T.genmask |= 1u << l;
            a3d_genlevel(&T, data, l);
        }
    }
    a3d_plat_clean_nc(data, pages * 4096u);
    a3d_tex_l2_dirty = 1;
    a3d_texobj *t = &g_tex[idx];
    const uint32_t gen = ((t->gen + 1u) & 0xFFFFu) ? ((t->gen + 1u) & 0xFFFFu) : 1u;
    *t = T;
    t->used = 1;
    t->gen = gen;
    t->ctx = (uint32_t)(c - a3d_ctxs);
    t->gtex = gtex;
    t->gpriv = tp;
    t->srcw = (float)sw;
    t->srch = (float)sh;
    t->data = data;
    t->phys = a3d_heap_phys(data);
    t->bytes = pages * 4096u;
    uint8_t *pp = a3d_g_ptr(tp);
    a3d_wbe32(pp + A3D_T_HANDLE, (gen << 16) | (idx + 1u));
    a3d_wbe32(pp + A3D_T_GEN, gen);
    a3d_cnt.textures++;
    a3d_cnt.tex_bytes += t->bytes;
    return W3D_SUCCESS;
}

/* a block some pending work still samples: binned in the open pass, or submitted and not complete */
static int a3d_tex_busy(const a3d_texobj *t)
{
    return t->last_fence > g_tex_done || (a3d_P.open && t->last_fence == a3d_P.fence);
}

static int a3d_retire(uint8_t *data, uint32_t fence)
{
    for (uint32_t i = 0; i < A3D_RETIRED; i++)
        if (g_retired[i].data == 0)
        {
            g_retired[i].data = data;
            g_retired[i].fence = fence;
            return 1;
        }
    return 0;
}

int32_t a3d_tex_update(a3d_ctx *c, uint32_t kind, uint32_t a1)
{
    if (kind > A3D_TU_RECT || (a1 & 1u) || !a3d_g_ok(a1, A3D_TUB_SIZE, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    const uint8_t *b = a3d_g_ptr(a1);
    a3d_texobj *t = a3d_tex_lookup(a3d_be32(b + A3D_TUB_TEX));
    if (t == 0 || t->ctx != (uint32_t)(c - a3d_ctxs))
        return W3D_ILLEGALINPUT;
    const uint32_t img = a3d_be32(b + A3D_TUB_IMAGE), L = a3d_be32(b + A3D_TUB_LEVEL);
    if (L > 15u)
        return W3D_ILLEGALINPUT;
    const uint32_t bpp = a3d_src_bpp(t->wfmt);
    const uint32_t lsw = a3d_src_dim(t->sw, L), lsh = a3d_src_dim(t->sh, L);
    uint32_t x0 = 0, y0 = 0, rw = lsw, rh = lsh, bpr = lsw * bpp;
    if (kind == A3D_TU_RECT)
    {
        x0 = a3d_be32(b + A3D_TUB_X);
        y0 = a3d_be32(b + A3D_TUB_Y);
        rw = a3d_be32(b + A3D_TUB_W);
        rh = a3d_be32(b + A3D_TUB_H);
        bpr = a3d_be32(b + A3D_TUB_BPR);
        if (x0 >= lsw || y0 >= lsh || rw == 0 || rh == 0 || rw > lsw - x0 || rh > lsh - y0 || bpr < rw * bpp
            || bpr > 0x10000u)
            return W3D_ILLEGALINPUT;
    }
    if (!a3d_g_ok(img, (rh - 1u) * bpr + rw * bpp, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    /* which stored level the guest's level L is (a resampled texture's big levels collapse onto level 0) */
    const uint32_t shift = a3d_tex_shift(t);
    a3d_cnt.tex_updates++;
    uint32_t l;
    if (L >= shift)
        l = L - shift;
    else if (L == 0)
        l = 0;                              /* resampled anew from the new level 0 */
    else
        return W3D_SUCCESS;                 /* a level larger than what is stored: nothing to keep */
    if (l >= t->real_levels)
        return W3D_SUCCESS;                 /* not mipmapped, or past the chain (Wazp3D ignores it too) */
    uint8_t *data = t->data;
    if (a3d_tex_busy(t))
    {
        uint8_t *nb = (uint8_t *)a3d_pool_alloc_t(t->bytes);
        if (nb && a3d_retire(t->data, t->last_fence))
        {
            memcpy(nb, t->data, t->bytes);
            data = nb;
            t->data = nb;
            t->phys = a3d_heap_phys(nb);
            t->last_fence = 0;
            t->ugen++;
            a3d_cnt.tex_renames++;
        }
        else
        {
            if (nb)
                a3d_pool_free(nb);
            a3d_cnt.tex_update_drains++;
            if (a3d_P.open && t->last_fence == a3d_P.fence)
                a3d_submit(&a3d_ctxs[a3d_P.ctx], A3D_SUB_OTHER);
            a3d_drain_to(t->last_fence);
        }
    }
    const uint32_t dw = a3d_lvl_w(t, l), dh = a3d_lvl_h(t, l);
    a3d_convert(t, data + a3d_lvl_off(t, l), dw, dh, a3d_g_ptr(img), lsw, lsh, bpr, x0, y0, rw, rh);
    a3d_regen(t, data, l + 1u);
    a3d_plat_clean_nc(data, t->bytes);
    a3d_tex_l2_dirty = 1;
    return W3D_SUCCESS;
}

static void a3d_tex_kill(a3d_texobj *t)
{
    if (t->gpriv && a3d_g_ok(t->gpriv, A3D_TPRIV_SIZE, A3D_GR_WRITE))
        a3d_wbe32(a3d_g_ptr(t->gpriv + A3D_T_HANDLE), 0);
    t->dead = 1;
    g_tex_maybe_dead = 1;
    t->gtex = 0;
    t->gpriv = 0;
    if (t->last_fence <= g_tex_done && !(a3d_P.open && t->last_fence == a3d_P.fence))
        a3d_tex_release(t);
}

int32_t a3d_tex_free(a3d_ctx *c, uint32_t gtex, uint32_t all)
{
    const uint32_t ci = (uint32_t)(c - a3d_ctxs);
    if (all)
    {
        for (uint32_t i = 0; i < A3D_MAX_TEX; i++)
            if (g_tex[i].used && !g_tex[i].dead && g_tex[i].ctx == ci)
                a3d_tex_kill(&g_tex[i]);
        return W3D_SUCCESS;
    }
    a3d_texobj *t = a3d_tex_lookup(gtex);
    if (t == 0 || t->ctx != ci)
        return W3D_ILLEGALINPUT;
    if (c->tex_bound == a3d_tex_handle(t))
        c->tex_bound = 0;
    a3d_tex_kill(t);
    return W3D_SUCCESS;
}

void a3d_tex_init(void)
{
    memset(g_tex, 0, sizeof(g_tex));
    memset(g_retired, 0, sizeof(g_retired));
    g_tex_done = 0;
    g_tex_maybe_dead = 0;
    a3d_tex_l2_dirty = 0;
}

void a3d_tex_drop_ctx(uint32_t ctxi)
{
    for (uint32_t i = 0; i < A3D_MAX_TEX; i++)
        if (g_tex[i].used && g_tex[i].ctx == ctxi)
            a3d_tex_release(&g_tex[i]);     /* the context was drained: nothing uses them */
}

void a3d_tex_retired_flush(void)
{
    for (uint32_t i = 0; i < A3D_RETIRED; i++)
        if (g_retired[i].data)
        {
            a3d_pool_free(g_retired[i].data);
            g_retired[i].data = 0;
        }
}

void a3d_tex_completed(uint32_t fence)
{
    g_tex_done = fence;
    if (g_tex_maybe_dead)
    {
        uint32_t left = 0;
        for (uint32_t i = 0; i < A3D_MAX_TEX; i++)
            if (g_tex[i].used && g_tex[i].dead)
            {
                if (g_tex[i].last_fence <= fence && !(a3d_P.open && g_tex[i].last_fence == a3d_P.fence))
                    a3d_tex_release(&g_tex[i]);
                else
                    left = 1;
            }
        g_tex_maybe_dead = left;
    }
    for (uint32_t i = 0; i < A3D_RETIRED; i++)
        if (g_retired[i].data && g_retired[i].fence <= fence && !(a3d_P.open && g_retired[i].fence == a3d_P.fence))
        {
            a3d_pool_free(g_retired[i].data);
            g_retired[i].data = 0;
        }
}

void a3d_tex_used(a3d_texobj *t, uint32_t fence)
{
    if (t && fence > t->last_fence)
        t->last_fence = fence;
}

uint32_t a3d_tex_tmureg(const a3d_texobj *t, uint32_t gtex)
{
    uint32_t ws = 0, wt = 0, bilinear = 0, mip = 0;
    uint32_t tp = 0;
    if (a3d_g_ok(gtex + W3D_TEX_DRIVER, 4, A3D_GR_READ))
        tp = a3d_be32(a3d_g_ptr(gtex + W3D_TEX_DRIVER));
    if (tp && a3d_g_ok(tp, A3D_TPRIV_SIZE, A3D_GR_READ))
    {
        const uint8_t *p = a3d_g_ptr(tp);
        ws = a3d_be32(p + A3D_T_WRAP_S) == W3D_CLAMP;
        wt = a3d_be32(p + A3D_T_WRAP_T) == W3D_CLAMP;
        const uint32_t fmag = a3d_be32(p + A3D_T_MAGFILTER), fmin = a3d_be32(p + A3D_T_MINFILTER);
        bilinear = fmag == W3D_LINEAR;
        mip = t->real_levels > 1u && fmin >= W3D_NEAREST_MIP_NEAREST && fmin <= W3D_LINEAR_MIP_LINEAR;
    }
    return t->lw | (t->lh << 4) | (ws << 8) | (wt << 9) | (bilinear << 10) | (mip << 11) | (t->fmt << 12);
}

uint32_t a3d_tex_stream(const a3d_texobj *t, uint32_t *pages, uint32_t max)
{
    const uint32_t n = t->bytes / 4096u;
    for (uint32_t i = 0; i < n && i < max; i++)
        pages[i] = t->phys + 4096u * i;
    return n < max ? n : max;
}

void a3d_tex_size(const a3d_texobj *t, float *w, float *h)
{
    *w = t->srcw;
    *h = t->srch;
}

uint32_t a3d_tex_hash(void)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < A3D_MAX_TEX; i++)
    {
        const a3d_texobj *t = &g_tex[i];
        if (!t->used)
            continue;
        const uint32_t w[13] = { i, t->gen, t->ctx, t->gtex, t->w, t->h, t->fmt, t->bytes, t->last_fence, t->dead,
                                 t->levels, t->genmask, t->ugen };
        const uint8_t *b = (const uint8_t *)w;
        for (uint32_t k = 0; k < sizeof(w); k++)
            h = (h ^ b[k]) * 16777619u;
        const uint32_t texel_bytes = a3d_lvl_off(t, t->levels);
        for (uint32_t k = 0; k < 64u && k < texel_bytes; k++)
            h = (h ^ t->data[k]) * 16777619u;
    }
    for (uint32_t i = 0; i < A3D_RETIRED; i++)
        h = (h ^ (g_retired[i].data ? g_retired[i].fence + 1u : 0u)) * 16777619u;
    return h;
}
