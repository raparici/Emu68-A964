/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the 3D engine's entries, contexts, passes and fences. */

#include <stdarg.h>
#include "a3d_agent.h"
#include "a3d_mgl.h"

a3d_stats_t a3d_cnt;
a3d_pass a3d_P;
a3d_binner a3d_B;
a3d_ctx a3d_ctxs[A3D_MAX_CTX];

static const a3d_backend *g_be;
static uint32_t g_ready;
static uint32_t g_fence;                /* the last fence handed to the backend                  */
static uint32_t g_done_polled;
static uint32_t g_area_fence[2];        /* the fence living in each list area (0 = free)         */
static struct { uint32_t n; uint8_t *chunk[A3D_CHUNKS]; } g_held[2];
static uint32_t g_backend_id;           /* the backend in use (a3d_init, after a fallback)        */
static uint32_t g_bin_owner;            /* context index + 1 whose state the binner holds        */
uint32_t a3d_wait_budget = 65536u;      /* SOFT work units a WAIT may spend (pixels + overheads)  */
uint32_t a3d_wait_mode = A3D_WAIT_AUTO;
uint32_t a3d_prot_enabled = 1u;

typedef struct a3d_chain
{
    uint8_t *chunk[A3D_MAX_CHAIN];
    uint32_t used[A3D_MAX_CHAIN];
    uint32_t n;
} a3d_chain;
static a3d_chain g_chain[A3D_MAX_BANDS];
static uint32_t g_chain_longest;

/* presents of private targets, done when their fence completes */
typedef struct a3d_present
{
    uint32_t fence, ctx;
    a3d_target tgt;
    uint64_t bands;                     /* bands that were committed                             */
    uint32_t band_lines;
} a3d_present;
#define A3D_MAX_PRESENT 4u
static a3d_present g_present[A3D_MAX_PRESENT];

static void a3d_poll(void);
static void a3d_prot_release(a3d_ctx *c);
static void a3d_prot_leave(a3d_ctx *c);
static void a3d_prot_park(a3d_ctx *c);
static int a3d_prot_can_park(const a3d_ctx *c);
static int a3d_prot_parked_all(const a3d_ctx *c);
static void a3d_priv_guard(a3d_ctx *c, uint32_t fill);

/* ---- small helpers -------------------------------------------------------------------------------- */

static uint32_t a3d_ctx_index(const a3d_ctx *c)
{
    return (uint32_t)(c - a3d_ctxs);
}

static void a3d_priv_put(const a3d_ctx *c, uint32_t off, uint32_t v)
{
    if (a3d_g_ok(c->gpriv + off, 4, A3D_GR_WRITE))
        a3d_wbe32(a3d_g_ptr(c->gpriv + off), v);
}

void a3d_reg(a3d_ctx *c, uint32_t addr, uint32_t value)
{
    if (addr < 16u && (c->reg_set & (1u << addr)) && c->reg_val[addr] == value)
        return;
    if (addr < 16u)
    {
        c->reg_val[addr] = value;
        c->reg_set |= 1u << addr;
    }
    a3d_bin_reg(&a3d_B, addr, value);
}

#define A3D_LINK_RESERVE 8u
static int a3d_more(rix_list *l, uint32_t need)
{
    a3d_chain *ch = (a3d_chain *)l->owner;
    if (need > A3D_CHUNK_BYTES - A3D_LINK_RESERVE || ch->n >= A3D_MAX_CHAIN)
        return 0;
    uint8_t *c = a3d_chunk_get();
    if (c == 0)
        return 0;
    if (ch->n)
        ch->used[ch->n - 1u] = l->pos;
    ch->chunk[ch->n] = c;
    ch->used[ch->n] = 0;
    ch->n++;
    if (ch->n > g_chain_longest)
        g_chain_longest = ch->n;
    l->mem = c;
    l->cap = A3D_CHUNK_BYTES - A3D_LINK_RESERVE;
    l->pos = 0;
    const uint32_t inuse = A3D_CHUNKS - a3d_chunks_free();
    if (inuse > a3d_cnt.chunks_peak)
        a3d_cnt.chunks_peak = inuse;
    return 1;
}

static void a3d_held_release(uint32_t area)
{
    for (uint32_t k = 0; k < g_held[area].n; k++)
        a3d_chunk_put(g_held[area].chunk[k]);
    g_held[area].n = 0;
}

/* the OPEN pass's chunks go back to the pool (a submitted set's are g_held's) */
static void a3d_chains_release(void)
{
    g_chain_longest = 0;
    for (uint32_t i = 0; i < A3D_MAX_BANDS; i++)
    {
        a3d_chain *ch = &g_chain[i];
        for (uint32_t k = 0; k < ch->n; k++)
            a3d_chunk_put(ch->chunk[k]);
        ch->n = 0;
        rix_list *l = &a3d_B.band[i].list;
        l->mem = 0;
        l->cap = 0;
        l->pos = 0;
        l->more = a3d_more;
        l->owner = ch;
        a3d_B.band[i].opt_pending = 0;
        a3d_B.band[i].overflow = 0;
    }
}

/* read the target the stub resolved into the private block; 0 = W3D_SUCCESS */
static int32_t a3d_target_read(const a3d_ctx *c, a3d_target *t)
{
    memset(t, 0, sizeof(*t));
    if (!a3d_g_ok(c->gpriv, A3D_PRIV_SIZE, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    const uint8_t *p = a3d_g_ptr(c->gpriv);
    const uint32_t flags = a3d_be32(p + A3D_P_TFLAGS);
    if (!(flags & A3D_TF_VALID))
        return W3D_NOTVISIBLE;
    const uint32_t base = a3d_be32(p + A3D_P_TBASE);
    const uint32_t bprow = a3d_be32(p + A3D_P_TBPROW);
    const uint32_t width = a3d_be32(p + A3D_P_TWIDTH);
    const uint32_t height = a3d_be32(p + A3D_P_THEIGHT);
    const uint32_t yoff = a3d_be32(p + A3D_P_TYOFFSET);
    const uint32_t fmt = a3d_be32(p + A3D_P_TFORMAT);
    if (width == 0 || height == 0 || width > 2047u || height > 2047u || yoff > 4096u)
        return W3D_ILLEGALBITMAP;
    uint32_t bpp;
    switch (fmt)
    {
    case W3D_FMT_R5G6B5: case W3D_FMT_B5G6R5: case W3D_FMT_R5G6B5PC: case W3D_FMT_B5G6R5PC:
    case W3D_FMT_R5G5B5: case W3D_FMT_B5G5R5: case W3D_FMT_R5G5B5PC: case W3D_FMT_B5G5R5PC:
        bpp = 2; break;
    case W3D_FMT_R8G8B8: case W3D_FMT_B8G8R8:
        bpp = 3; break;
    case W3D_FMT_A8R8G8B8: case W3D_FMT_A8B8G8R8: case W3D_FMT_R8G8B8A8: case W3D_FMT_B8G8R8A8:
        bpp = 4; break;
    default:
        return W3D_UNSUPPORTEDFMT;          /* CLUT and planar: no 3D (as a real Voodoo 3) */
    }
    if (bprow < width * bpp)
        return W3D_ILLEGALBITMAP;
    const uint32_t first = base + yoff * bprow;
    if (first < base || !a3d_g_target_ok(first, bprow, height))
        return W3D_ILLEGALBITMAP;
    t->base = base;
    t->guest = first;
    t->bprow = bprow;
    t->width = width;
    t->height = height;
    t->format = fmt;
    t->host = a3d_g_ptr(first);
    t->phys = a3d_g_phys(first);
    const uint32_t ord = g_be->orders ? g_be->orders() : (g_be->color_le ? A3D_ORD_LE : A3D_ORD_BE);
    const uint32_t gpu = g_be->id == A3D_BE_FABRIC;
    const uint32_t inplace = (fmt == W3D_FMT_R5G6B5PC && (ord & A3D_ORD_LE)) || (fmt == W3D_FMT_R5G6B5 && (ord & A3D_ORD_BE));
    t->direct = inplace && bprow == width * 2u && t->phys != 0
                && (!gpu || ((t->phys & 7u) == 0u
                             && a3d_bin_pick_lines(width, height, a3d_B.band_pixels, a3d_B.max_bands, 1u) != 0u));
    t->color_be = t->direct && fmt == W3D_FMT_R5G6B5;
    t->bw = (!t->direct && gpu) ? (width + 63u) & ~63u : width;
    return W3D_SUCCESS;
}

static int a3d_target_same(const a3d_target *a, const a3d_target *b)
{
    return a->guest == b->guest && a->bprow == b->bprow && a->width == b->width
        && a->height == b->height && a->format == b->format;
}

static void a3d_buffers_free(a3d_ctx *c)
{
    a3d_pbuf_free_all(c);
    c->tgt.priv = 0;
    c->tgt.priv_phys = 0;
    if (c->depth)
        a3d_pool_free(c->depth);
    c->depth = 0;
    c->depth_phys = 0;
    c->depth_band_valid = 0;
}

static void a3d_priv_fill_rows(const a3d_target *t, uint32_t y0, uint32_t y1)
{
    if (t->priv == 0)
        return;
    if (y1 > t->height)
        y1 = t->height;
    if (a3d_fill_ref_on)
        a3d_priv_fill_ref(t, y0, y1, g_be->color_le);
    else
        for (uint32_t y = y0; y < y1; y++)
            a3d_fill_row(t->host + y * t->bprow, t->priv + 2u * (y * t->bw), t->width, t->format, g_be->color_le);
    a3d_cnt.priv_fill_rows += y1 > y0 ? y1 - y0 : 0u;
}

/* (re)bind c to the target in its private block; the caller has submitted c's open pass */
static int32_t a3d_target_bind(a3d_ctx *c)
{
    a3d_target t;
    const int32_t e = a3d_target_read(c, &t);
    if (e != W3D_SUCCESS)
        return e;
    if (c->tgt_valid && a3d_target_same(&c->tgt, &t))
    {
        t.priv = c->tgt.priv;
        t.priv_phys = c->tgt.priv_phys;
        c->tgt = t;                         /* same bitmap: keep the buffers */
        return W3D_SUCCESS;
    }
    const int keep = c->tgt_valid && c->depth && t.width == c->tgt.width && t.height == c->tgt.height
                     && t.bw == c->tgt.bw && t.direct == c->tgt.direct && (t.direct || c->tgt.priv);
    if (c->lazy || c->prot_cur)
    {
        if (keep && a3d_prot_can_park(c))
            a3d_prot_park(c);
        else
        {
            a3d_drain_to(c->fence_sub);
            a3d_prot_leave(c);
        }
    }
    else if (c->fence_sub > c->fence_done && !(keep && a3d_prot_parked_all(c)))
        a3d_drain_to(c->fence_sub);
    if (keep)
    {
        a3d_pbuf_pick(c, &t);
        c->tgt = t;
        c->depth_band_valid = 0;
        c->priv_stale = t.direct ? 0u : ~(uint64_t)0;
        a3d_cnt.tgt_kept++;
        return W3D_SUCCESS;
    }
    a3d_buffers_free(c);
    c->tgt = t;
    c->tgt_valid = 1;
    a3d_cnt.tgt_alloc++;
    const uint32_t px = t.bw * t.height * 2u;
    if (!t.direct)
    {
        c->tgt.priv = (uint8_t *)a3d_pool_alloc(px);
        if (c->tgt.priv == 0)
        {
            c->tgt_valid = 0;
            return W3D_NOMEMORY;
        }
        c->tgt.priv_phys = a3d_heap_phys(c->tgt.priv);
        a3d_pbuf_first(c);
        if (t.bw > t.width)
            for (uint32_t y = 0; y < t.height; y++)
                memset(c->tgt.priv + 2u * (y * t.bw + t.width), 0, 2u * (t.bw - t.width));
    }
    c->priv_stale = t.direct ? 0u : ~(uint64_t)0;
    c->depth = (uint8_t *)a3d_pool_alloc(px);    /* on failure: no depth across passes (counted) */
    if (c->depth)
    {
        c->depth_phys = a3d_heap_phys(c->depth);
        c->depth_w = t.width;
        c->depth_h = t.height;
        c->depth_stride = t.bw;
    }
    else
        a3d_cnt.errors++;
    c->depth_band_valid = 0;
    return W3D_SUCCESS;
}

static uint32_t g_prof_depth[5], g_prof_t0[5];
void a3d_prof_begin(uint32_t b)
{
    if (g_prof_depth[b]++ == 0)
        g_prof_t0[b] = a3d_plat_prof();
}

void a3d_prof_end(uint32_t b, uint32_t *bucket)
{
    if (g_prof_depth[b] && --g_prof_depth[b] == 0)
        *bucket += (a3d_plat_prof() - g_prof_t0[b]) >> 6;
}

enum
{
    A3D_PK_565 = 0, A3D_PK_BGR565, A3D_PK_555, A3D_PK_BGR555,   /* 16-bit; pc = little-endian in memory */
    A3D_PK_ARGB, A3D_PK_ABGR, A3D_PK_RGBA, A3D_PK_BGRA,         /* 32-bit: the bytes in this order      */
    A3D_PK_RGB, A3D_PK_BGR                                      /* 24-bit                               */
};
typedef struct a3d_pconv
{
    uint32_t kind, pc, le, bpp;
} a3d_pconv;

static const uint8_t a3d_x5[32] = {     /* x * 255 / 31 */
    0x00, 0x08, 0x10, 0x18, 0x20, 0x29, 0x31, 0x39, 0x41, 0x4a, 0x52, 0x5a, 0x62, 0x6a, 0x73, 0x7b,
    0x83, 0x8b, 0x94, 0x9c, 0xa4, 0xac, 0xb4, 0xbd, 0xc5, 0xcd, 0xd5, 0xde, 0xe6, 0xee, 0xf6, 0xff };
static const uint8_t a3d_x6[64] = {     /* x * 255 / 63 */
    0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
    0x40, 0x44, 0x48, 0x4c, 0x50, 0x55, 0x59, 0x5d, 0x61, 0x65, 0x69, 0x6d, 0x71, 0x75, 0x79, 0x7d,
    0x81, 0x85, 0x89, 0x8d, 0x91, 0x95, 0x99, 0x9d, 0xa1, 0xa5, 0xaa, 0xae, 0xb2, 0xb6, 0xba, 0xbe,
    0xc2, 0xc6, 0xca, 0xce, 0xd2, 0xd6, 0xda, 0xde, 0xe2, 0xe6, 0xea, 0xee, 0xf2, 0xf6, 0xfa, 0xff };

static a3d_pconv a3d_pconv_of(uint32_t fmt, uint32_t le)
{
    a3d_pconv k;
    k.le = le ? 1u : 0u;
    k.pc = fmt == W3D_FMT_R5G6B5PC || fmt == W3D_FMT_B5G6R5PC || fmt == W3D_FMT_R5G5B5PC || fmt == W3D_FMT_B5G5R5PC;
    k.bpp = 4u;
    switch (fmt)
    {
    case W3D_FMT_A8R8G8B8: k.kind = A3D_PK_ARGB; break;
    case W3D_FMT_A8B8G8R8: k.kind = A3D_PK_ABGR; break;
    case W3D_FMT_R8G8B8A8: k.kind = A3D_PK_RGBA; break;
    case W3D_FMT_B8G8R8A8: k.kind = A3D_PK_BGRA; break;
    case W3D_FMT_R8G8B8:   k.kind = A3D_PK_RGB; k.bpp = 3u; break;
    case W3D_FMT_B8G8R8:   k.kind = A3D_PK_BGR; k.bpp = 3u; break;
    case W3D_FMT_R5G6B5: case W3D_FMT_R5G6B5PC: k.kind = A3D_PK_565; k.bpp = 2u; break;
    case W3D_FMT_B5G6R5: case W3D_FMT_B5G6R5PC: k.kind = A3D_PK_BGR565; k.bpp = 2u; break;
    case W3D_FMT_R5G5B5: case W3D_FMT_R5G5B5PC: k.kind = A3D_PK_555; k.bpp = 2u; break;
    default: k.kind = A3D_PK_BGR555; k.bpp = 2u; break;
    }
    return k;
}

static void a3d_present_row_c(const uint8_t *s, uint8_t *d, uint32_t x, uint32_t w, const a3d_pconv *k)
{
    const uint32_t s0 = k->le ? 0u : 8u, s1 = k->le ? 8u : 0u;      /* the source's byte order */
    const uint32_t o0 = k->pc ? 0u : 8u, o1 = k->pc ? 8u : 0u;      /* a 16-bit target's */
#define A3D_PV(x) ((((uint32_t)s[2u * (x)]) << s0) | (((uint32_t)s[2u * (x) + 1u]) << s1))
#define A3D_ROW16(EXPR)                                                     \
    for (; x < w; x++)                                                      \
    {                                                                       \
        const uint32_t v = A3D_PV(x);                                       \
        const uint32_t o = (EXPR);                                          \
        d[2u * x] = (uint8_t)(o >> o0);                                     \
        d[2u * x + 1u] = (uint8_t)(o >> o1);                                \
    }                                                                       \
    break;
#define A3D_ROWN(N, B0, B1, B2, B3)                                         \
    for (; x < w; x++)                                                      \
    {                                                                       \
        const uint32_t v = A3D_PV(x);                                       \
        const uint32_t R = a3d_x5[v >> 11], G = a3d_x6[(v >> 5) & 0x3Fu], B = a3d_x5[v & 0x1Fu], A = 0xFFu; \
        uint8_t *o = d + (N) * x;                                           \
        o[0] = (uint8_t)(B0); o[1] = (uint8_t)(B1); o[2] = (uint8_t)(B2);   \
        if ((N) == 4u)                                                      \
            o[3] = (uint8_t)(B3);                                           \
        (void)A; (void)R; (void)G; (void)B;                                 \
    }                                                                       \
    break;
    switch (k->kind)
    {
    case A3D_PK_565:    A3D_ROW16(v)
    case A3D_PK_BGR565: A3D_ROW16((v & 0x07E0u) | (v >> 11) | ((v & 0x1Fu) << 11))
    case A3D_PK_555:    A3D_ROW16(((v >> 1) & 0x7FE0u) | (v & 0x1Fu))
    case A3D_PK_BGR555: A3D_ROW16(((v & 0x1Fu) << 10) | ((v >> 1) & 0x03E0u) | (v >> 11))
    case A3D_PK_ARGB:   A3D_ROWN(4u, A, R, G, B)
    case A3D_PK_ABGR:   A3D_ROWN(4u, A, B, G, R)
    case A3D_PK_RGBA:   A3D_ROWN(4u, R, G, B, A)
    case A3D_PK_BGRA:   A3D_ROWN(4u, B, G, R, A)
    case A3D_PK_RGB:    A3D_ROWN(3u, R, G, B, 0)
    default:            A3D_ROWN(3u, B, G, R, 0)
    }
#undef A3D_ROWN
#undef A3D_ROW16
#undef A3D_PV
}

#if defined(__arm__) && defined(__ARM_NEON__)
#define A3D_PRESENT_NEON 1
#define A3D_NSWAP "vrev16.8 q0, q0\n\tvrev16.8 q1, q1\n\tvrev16.8 q2, q2\n\tvrev16.8 q3, q3\n\t"
#define A3D_N_BGR565(q) "vshr.u16 q8, " q ", #11\n\tvshl.i16 q9, " q ", #11\n\tvand " q ", " q ", q12\n\t"   \
                        "vorr " q ", " q ", q8\n\tvorr " q ", " q ", q9\n\t"
#define A3D_N_555(q)    "vshr.u16 q8, " q ", #1\n\tvand q8, q8, q13\n\tvand " q ", " q ", q14\n\t"             \
                        "vorr " q ", " q ", q8\n\t"
#define A3D_N_BGR555(q) "vshl.i16 q8, " q ", #11\n\tvshr.u16 q8, q8, #1\n\tvshr.u16 q9, " q ", #1\n\t"      \
                        "vand q9, q9, q15\n\tvshr.u16 " q ", " q ", #11\n\tvorr " q ", " q ", q8\n\t"            \
                        "vorr " q ", " q ", q9\n\t"
#define A3D_N_ALL(M) M("q0") M("q1") M("q2") M("q3")
#ifdef __ARMEB__
#define A3D_NLD  "vldmia %[s]!, {d0-d7}\n\tvrev64.8 q0, q0\n\tvrev64.8 q1, q1\n\tvrev64.8 q2, q2\n\tvrev64.8 q3, q3\n\t"
#define A3D_NLDS "vldmia %[s]!, {d0-d7}\n\tvrev64.16 q0, q0\n\tvrev64.16 q1, q1\n\tvrev64.16 q2, q2\n\tvrev64.16 q3, q3\n\t"
#else
#define A3D_NLD  "vldmia %[s]!, {d0-d7}\n\t"
#define A3D_NLDS "vldmia %[s]!, {d0-d7}\n\t" A3D_NSWAP
#endif
#define A3D_N16(LD, CONV, POST)                                                                               \
    __asm__ volatile("vdup.16 q12, %[k0]\n\tvdup.16 q13, %[k1]\n\tvdup.16 q14, %[k2]\n\tvdup.16 q15, %[k3]\n\t"  \
                     "1:\n\t"                                                                                 \
                     LD CONV POST                                                                             \
                     "vst1.8 {d0-d3}, [%[d]]!\n\t"                                                            \
                     "vst1.8 {d4-d7}, [%[d]]!\n\t"                                                            \
                     "subs %[n], %[n], #1\n\t"                                                                \
                     "bne 1b\n\t"                                                                             \
                     : [s] "+r"(s), [d] "+r"(d), [n] "+r"(n)                                                  \
                     : [k0] "r"(0x07E0u), [k1] "r"(0x7FE0u), [k2] "r"(0x001Fu), [k3] "r"(0x03E0u)             \
                     : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d16", "d17", "d18",   \
                       "d19", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31")
#define A3D_NGRP(q, R, G, B)                                                                                  \
    "vshrn.i16 d10, " q ", #8\n\tvmovn.i16 d9, " q "\n\t"                                                    \
    "vshr.u8 d8, d10, #3\n\tvshl.i8 d10, d10, #5\n\tvshr.u8 d10, d10, #2\n\tvsra.u8 d10, d9, #5\n\t"      \
    "vshl.i8 d9, d9, #3\n\tvshr.u8 d9, d9, #3\n\t"                                                          \
    "vtbl.8 " R ", {d20-d23}, d8\n\tvtbl.8 " B ", {d20-d23}, d9\n\t"                                        \
    "vtbl.8 " G ", {d24-d27}, d10\n\tveor d10, d10, d11\n\tvtbx.8 " G ", {d28-d31}, d10\n\t"
#define A3D_N32(LD, R, G, B, A, ST)                                                                           \
    __asm__ volatile("vld1.8 {d20-d23}, [%[t5]]\n\tvld1.8 {d24-d27}, [%[t6]]\n\tvld1.8 {d28-d31}, [%[t6b]]\n\t" \
                     "vmov.i8 d11, #32\n\tvmov.i8 " A ", #255\n\t"                                              \
                     "1:\n\t"                                                                                 \
                     LD                                                                                       \
                     A3D_NGRP("q0", R, G, B) ST                                                               \
                     A3D_NGRP("q1", R, G, B) ST                                                               \
                     A3D_NGRP("q2", R, G, B) ST                                                               \
                     A3D_NGRP("q3", R, G, B) ST                                                               \
                     "subs %[n], %[n], #1\n\t"                                                                \
                     "bne 1b\n\t"                                                                             \
                     : [s] "+r"(s), [d] "+r"(d), [n] "+r"(n)                                                  \
                     : [t5] "r"(a3d_x5), [t6] "r"(a3d_x6), [t6b] "r"(a3d_x6 + 32)                             \
                     : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10",      \
                       "d11", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26",     \
                       "d27", "d28", "d29", "d30", "d31")
#define A3D_ST4 "vst4.8 {d16-d19}, [%[d]]!\n\t"
#define A3D_ST3 "vst3.8 {d16-d18}, [%[d]]!\n\t"

/* the first (w / 32) * 32 pixels of a row; returns how many it converted.  s: 8-byte aligned (vldm) */
static uint32_t a3d_present_row_neon(const uint8_t *s, uint8_t *d, uint32_t w, const a3d_pconv *k)
{
    uint32_t n = w / 32u;
    if (n == 0)
        return 0;
    const uint32_t done = 32u * n;
    if (k->kind <= A3D_PK_BGR555)
    {
        const uint32_t sel = (k->le ? 0u : 2u) | (k->pc ? 0u : 1u);    /* bit 1: swap in, bit 0: swap out */
        switch (k->kind * 4u + sel)
        {
        case A3D_PK_565 * 4u + 0u:    A3D_N16(A3D_NLD, "", ""); break;
        case A3D_PK_565 * 4u + 1u:    A3D_N16(A3D_NLD, "", A3D_NSWAP); break;
        case A3D_PK_565 * 4u + 2u:    A3D_N16(A3D_NLDS, "", ""); break;
        case A3D_PK_565 * 4u + 3u:    A3D_N16(A3D_NLD, "", ""); break;      /* swapped in and out: a copy */
        case A3D_PK_BGR565 * 4u + 0u: A3D_N16(A3D_NLD, A3D_N_ALL(A3D_N_BGR565), ""); break;
        case A3D_PK_BGR565 * 4u + 1u: A3D_N16(A3D_NLD, A3D_N_ALL(A3D_N_BGR565), A3D_NSWAP); break;
        case A3D_PK_BGR565 * 4u + 2u: A3D_N16(A3D_NLDS, A3D_N_ALL(A3D_N_BGR565), ""); break;
        case A3D_PK_BGR565 * 4u + 3u: A3D_N16(A3D_NLDS, A3D_N_ALL(A3D_N_BGR565), A3D_NSWAP); break;
        case A3D_PK_555 * 4u + 0u:    A3D_N16(A3D_NLD, A3D_N_ALL(A3D_N_555), ""); break;
        case A3D_PK_555 * 4u + 1u:    A3D_N16(A3D_NLD, A3D_N_ALL(A3D_N_555), A3D_NSWAP); break;
        case A3D_PK_555 * 4u + 2u:    A3D_N16(A3D_NLDS, A3D_N_ALL(A3D_N_555), ""); break;
        case A3D_PK_555 * 4u + 3u:    A3D_N16(A3D_NLDS, A3D_N_ALL(A3D_N_555), A3D_NSWAP); break;
        case A3D_PK_BGR555 * 4u + 0u: A3D_N16(A3D_NLD, A3D_N_ALL(A3D_N_BGR555), ""); break;
        case A3D_PK_BGR555 * 4u + 1u: A3D_N16(A3D_NLD, A3D_N_ALL(A3D_N_BGR555), A3D_NSWAP); break;
        case A3D_PK_BGR555 * 4u + 2u: A3D_N16(A3D_NLDS, A3D_N_ALL(A3D_N_BGR555), ""); break;
        default:                      A3D_N16(A3D_NLDS, A3D_N_ALL(A3D_N_BGR555), A3D_NSWAP); break;
        }
        return done;
    }
    if (k->le)
        switch (k->kind)
        {
        case A3D_PK_ARGB: A3D_N32(A3D_NLD, "d17", "d18", "d19", "d16", A3D_ST4); break;
        case A3D_PK_ABGR: A3D_N32(A3D_NLD, "d19", "d18", "d17", "d16", A3D_ST4); break;
        case A3D_PK_RGBA: A3D_N32(A3D_NLD, "d16", "d17", "d18", "d19", A3D_ST4); break;
        case A3D_PK_BGRA: A3D_N32(A3D_NLD, "d18", "d17", "d16", "d19", A3D_ST4); break;
        case A3D_PK_RGB:  A3D_N32(A3D_NLD, "d16", "d17", "d18", "d19", A3D_ST3); break;
        default:          A3D_N32(A3D_NLD, "d18", "d17", "d16", "d19", A3D_ST3); break;
        }
    else
        switch (k->kind)
        {
        case A3D_PK_ARGB: A3D_N32(A3D_NLDS, "d17", "d18", "d19", "d16", A3D_ST4); break;
        case A3D_PK_ABGR: A3D_N32(A3D_NLDS, "d19", "d18", "d17", "d16", A3D_ST4); break;
        case A3D_PK_RGBA: A3D_N32(A3D_NLDS, "d16", "d17", "d18", "d19", A3D_ST4); break;
        case A3D_PK_BGRA: A3D_N32(A3D_NLDS, "d18", "d17", "d16", "d19", A3D_ST4); break;
        case A3D_PK_RGB:  A3D_N32(A3D_NLDS, "d16", "d17", "d18", "d19", A3D_ST3); break;
        default:          A3D_N32(A3D_NLDS, "d18", "d17", "d16", "d19", A3D_ST3); break;
        }
    return done;
}
#endif

void a3d_present_row(const uint8_t *s, uint8_t *d, uint32_t w, uint32_t fmt, uint32_t le)
{
    const a3d_pconv k = a3d_pconv_of(fmt, le);
    uint32_t x = 0;
#ifdef A3D_PRESENT_NEON
    const uint32_t p = (uint32_t)(-(uintptr_t)s & 7u) >> 1;
    if (!((uintptr_t)s & 1u) && p < w)
    {
        if (p)
            a3d_present_row_c(s, d, 0, p, &k);
        x = p + a3d_present_row_neon(s + 2u * p, d + k.bpp * p, w - p, &k);
    }
#endif
    if (x < w)
        a3d_present_row_c(s, d, x, w, &k);
}

static void a3d_direct_maint(const a3d_target *t, uint64_t bands, uint32_t band_lines)
{
    if (!bands || !band_lines || t->host == 0)
        return;
    uint32_t b0 = 0, b1 = 63;
    while (b0 < 64u && !((bands >> b0) & 1u))
        b0++;
    while (b1 > b0 && !((bands >> b1) & 1u))
        b1--;
    const uint32_t r0 = b0 * band_lines;
    if (r0 >= t->height)
        return;
    uint32_t r1 = (b1 + 1u) * band_lines;
    if (r1 > t->height)
        r1 = t->height;
    a3d_plat_clean(t->host + r0 * t->bprow, (r1 - r0) * t->bprow);
}

static void a3d_display_guard(const a3d_target *t, uint32_t own_fence, uint32_t may_drain)
{
    for (uint32_t n = 0; n < 4u; n++)
    {
        const uint32_t f = a3d_plat_display_guard(t->guest, t->guest + t->bprow * t->height);
        if (f == 0 || f >= own_fence)
            return;
        if (may_drain && f > g_done_polled)
        {
            a3d_cnt.display_waits++;
            a3d_drain_to(f);
        }
        a3d_plat_present_begin();
    }
}

static void a3d_present_run_inner(const a3d_present *pr);
static void a3d_present_run(const a3d_present *pr)
{
    a3d_prof_begin(A3D_PROF_PRESENT);
    a3d_present_run_inner(pr);
    a3d_prof_end(A3D_PROF_PRESENT, &a3d_cnt.prof_present);
}

static void a3d_present_run_inner(const a3d_present *pr)
{
    const a3d_target *t = &pr->tgt;
    if (t->direct)
    {
        a3d_direct_maint(t, pr->bands, pr->band_lines);
        return;
    }
    if (t->priv == 0)
        return;
    a3d_display_guard(t, pr->fence, 0u);
    a3d_cnt.presents++;
    const uint32_t le = g_be->color_le;
    uint32_t band = 0, brow = 0;
    for (uint32_t y = 0; y < t->height; y++)
    {
        if (y)
        {
            if (pr->band_lines && ++brow == pr->band_lines)
            {
                brow = 0;
                band++;
            }
        }
        if (band >= 64u || !((pr->bands >> band) & 1u))
            continue;
        uint8_t *d = t->host + y * t->bprow;
        const uint8_t *s = t->priv + 2u * y * t->bw;
        a3d_present_row(s, d, t->width, t->format, le);
        a3d_plat_clean(d, t->bprow);
    }
}

int a3d_pass_ensure(a3d_ctx *c)
{
    const uint32_t ci = a3d_ctx_index(c);
    if (a3d_P.open && a3d_P.ctx == ci)
    {
        /* the stub may have re-locked the bitmap at another address: then the open pass is finished */
        if (a3d_tgt_cached(c))
            return W3D_SUCCESS;
        a3d_target t;
        if (a3d_target_read(c, &t) == W3D_SUCCESS && a3d_target_same(&t, &c->tgt))
        {
            a3d_tgt_cache(c);
            return W3D_SUCCESS;
        }
        a3d_submit(c, 0);
    }
    if (a3d_P.open && a3d_P.ctx != ci)
        a3d_submit(&a3d_ctxs[a3d_P.ctx], 0);
    const int32_t e = a3d_target_bind(c);
    if (e != W3D_SUCCESS)
        return e;
    if (!a3d_bin_geometry_c11(&a3d_B, c->tgt.bw, c->tgt.height, g_be->id == A3D_BE_FABRIC))
        return W3D_UNSUPPORTEDFMT;
    a3d_chains_release();
    a3d_bin_begin_list(&a3d_B);
    if (g_bin_owner != ci + 1u)
    {
        /* the binner's logical state is another context's: forget what this one sent */
        c->reg_set = 0;
        c->tex_bound = 0;
        c->tex_ugen = 0;
        c->fog_key[5] = 0;
        g_bin_owner = ci + 1u;
        a3d_state_epoch++;
    }
    /* the clear values a MEMSET may need before the program's first clear: far depth, black */
    a3d_reg(c, RIX_REG_CLEAR_DEPTH, c->clear_depth);
    a3d_reg(c, RIX_REG_CLEAR_COLOR, c->clear_color);
    memset(a3d_P.band, 0, sizeof(a3d_P.band));
    a3d_P.open = 1;
    a3d_P.ctx = ci;
    a3d_P.fence = g_fence + 1u;
    a3d_P.staged = 0;
    a3d_P.depth_used = 0;
    a3d_P.work = 0;
    return W3D_SUCCESS;
}

#define A3D_FP_RESET (2u | (0xFFu << 6) | RIX_FP_DEPTH_MASK | RIX_FP_COLOR_MASKS | (1u << 19) | (2u << 27))
static uint32_t g_fbs_depth, g_fbs_fe, g_fbs_fp, g_fbs_had;

void a3d_fbstate_begin(a3d_ctx *c)
{
    if (g_fbs_depth++)
        return;
    const uint32_t bfe = 1u << RIX_REG_FEATURE_ENABLE, bfp = 1u << RIX_REG_FRAGMENT_PIPELINE;
    g_fbs_had = c->reg_set & (bfe | bfp);
    g_fbs_fe = (c->reg_set & bfe) ? c->reg_val[RIX_REG_FEATURE_ENABLE] : 0u;
    g_fbs_fp = (c->reg_set & bfp) ? c->reg_val[RIX_REG_FRAGMENT_PIPELINE] : A3D_FP_RESET;
    a3d_reg(c, RIX_REG_FEATURE_ENABLE, (g_fbs_fe & ~RIX_FE_SCISSOR) | RIX_FE_CLEAR_ON_COMMIT);
    a3d_reg(c, RIX_REG_FRAGMENT_PIPELINE, g_fbs_fp | RIX_FP_DEPTH_MASK | RIX_FP_COLOR_MASKS);
}

void a3d_fbstate_end(a3d_ctx *c)
{
    if (g_fbs_depth == 0 || --g_fbs_depth)
        return;
    if (g_fbs_had & (1u << RIX_REG_FEATURE_ENABLE))
        a3d_reg(c, RIX_REG_FEATURE_ENABLE, g_fbs_fe);
    if (g_fbs_had & (1u << RIX_REG_FRAGMENT_PIPELINE))
        a3d_reg(c, RIX_REG_FRAGMENT_PIPELINE, g_fbs_fp);
}

void a3d_band_prepare(a3d_ctx *c, uint32_t band, uint32_t need_color, uint32_t need_depth,
                      uint32_t clearing_color, uint32_t clearing_depth)
{
    uint32_t f = a3d_P.band[band];
    a3d_P.work = 1;
    if (!(f & A3D_BT_HEADER))
    {
        a3d_bin_band_header(&a3d_B, band, c->tgt.direct ? c->tgt.phys : c->tgt.priv_phys, c->depth_phys);
        f |= A3D_BT_HEADER;
    }
    const uint32_t fb_color = need_color && !(f & A3D_BT_COLOR) && !clearing_color;
    const uint32_t fb_depth = need_depth && !(f & A3D_BT_DEPTH) && !clearing_depth;
    if (fb_color || fb_depth)
        a3d_fbstate_begin(c);
    if (need_color && !(f & A3D_BT_COLOR))
    {
        const uint64_t bit = band < 64u ? (uint64_t)1 << band : 0u;
        if (!clearing_color)
        {
            if (c->priv_stale & bit)
            {
                a3d_priv_guard(c, 1u);
                a3d_priv_fill_rows(&c->tgt, band * a3d_B.band_lines, (band + 1u) * a3d_B.band_lines);
            }
            a3d_bin_band_fb(&a3d_B, band, RIX_FB_LOAD | RIX_FB_COLOR);
        }
        c->priv_stale &= ~bit;
        f |= A3D_BT_COLOR;
    }
    if (need_depth && !(f & A3D_BT_DEPTH))
    {
        if (!clearing_depth)
        {
            if (c->depth && ((c->depth_band_valid >> band) & 1u))
            {
                a3d_bin_band_fb(&a3d_B, band, RIX_FB_LOAD | RIX_FB_DEPTH);
                a3d_cnt.depth_loads++;
            }
            else
                a3d_bin_band_fb(&a3d_B, band, RIX_FB_MEMSET | RIX_FB_DEPTH);   /* CLEAR_DEPTH: far */
        }
        f |= A3D_BT_DEPTH;
        a3d_P.depth_used = 1;
    }
    if (fb_color || fb_depth)
        a3d_fbstate_end(c);
    a3d_P.band[band] = f;
}

static void a3d_drain_area(uint32_t area)
{
    while (g_area_fence[area] && g_be->completed() < g_area_fence[area])
        g_be->progress(0xFFFFFFFFu);
    a3d_poll();
    g_area_fence[area] = 0;
    a3d_held_release(area);
}

static void a3d_reclaim(uint32_t need)
{
    a3d_poll();
    while (a3d_chunks_free() <= need)
    {
        const uint32_t a = (g_area_fence[0] && (!g_area_fence[1] || g_area_fence[0] < g_area_fence[1])) ? 0u : 1u;
        if (!g_area_fence[a])
            break;                          /* nothing in flight holds chunks */
        a3d_cnt.chunk_waits++;
        a3d_drain_area(a);
    }
}

int a3d_room(a3d_ctx *c)
{
    const uint32_t longest = g_chain_longest;
    const uint32_t need = a3d_B.bands + 4u;
    if (longest + 2u < A3D_MAX_CHAIN)
    {
        if (a3d_chunks_free() > need)
            return 1;
        a3d_poll();
        if (a3d_chunks_free() > need)
            return 1;
    }
    a3d_cnt.cuts++;
    a3d_submit(c, A3D_SUB_OTHER);
    a3d_reclaim(need);
    a3d_pass_ensure(c);
    return 0;
}

static uint32_t a3d_submit_inner(a3d_ctx *c, uint32_t why);
uint32_t a3d_submit(a3d_ctx *c, uint32_t why)
{
    a3d_prof_begin(A3D_PROF_SUBMIT);
    const uint32_t f = a3d_submit_inner(c, why);
    a3d_prof_end(A3D_PROF_SUBMIT, &a3d_cnt.prof_submit);
    return f;
}

static uint32_t a3d_submit_inner(a3d_ctx *c, uint32_t why)
{
    const uint32_t ci = a3d_ctx_index(c);
    if (!a3d_P.open || a3d_P.ctx != ci)
        return c->fence_sub;
    if (c->last_frame_fence > g_done_polled)
    {
        a3d_cnt.frame_guard_waits++;
        a3d_drain_to(c->last_frame_fence);
    }
    a3d_priv_guard(c, 0u);
    if (c->tgt.direct)
        a3d_display_guard(&c->tgt, a3d_P.fence, 1u);
    a3d_plat_present_begin();
    uint64_t committed = 0;
    a3d_fbstate_begin(c);
    for (uint32_t i = 0; i < a3d_B.bands; i++)
    {
        const uint32_t f = a3d_P.band[i];
        if (!(f & A3D_BT_HEADER))
            continue;
        if ((f & A3D_BT_ZDIRTY) && c->depth)
        {
            a3d_bin_band_fb(&a3d_B, i, RIX_FB_COMMIT | RIX_FB_DEPTH);
            c->depth_band_valid |= (uint64_t)1 << i;
            a3d_cnt.depth_commits++;
        }
        if (f & A3D_BT_CDIRTY)
        {
            a3d_bin_band_fb(&a3d_B, i, RIX_FB_COMMIT | RIX_FB_COLOR);
            committed |= (uint64_t)1 << i;
        }
    }
    a3d_fbstate_end(c);
    if (a3d_bin_overflowed(&a3d_B))
        a3d_cnt.list_errors++;            /* a command was lost: a bug (a3d_room() should prevent it) */
    const uint32_t fence = a3d_P.fence;
    const uint32_t area = fence & 1u;
    if (g_area_fence[area] && g_be->completed() < g_area_fence[area])
    {
        a3d_drain_area(area);               /* both areas busy: the older set finishes first (unbounded) */
    }
    a3d_held_release(area);                 /* (the area's old set is complete: its chunks come back) */
    uint8_t *a = a3d_area(area);
    const uint32_t dir_bytes = 32u + 8u * A3D_MAX_BANDS;
    uint32_t list_bytes = 0;
    rix_put32(a + 0, 0x4133444Cu);          /* 'A3DL' */
    rix_put32(a + 4, a3d_B.bands);
    rix_put32(a + 8, c->tgt.width);
    rix_put32(a + 12, c->tgt.height);
    rix_put32(a + 16, a3d_B.band_lines);
    rix_put32(a + 20, fence);
    rix_put32(a + 24, 0);
    rix_put32(a + 28, 0);
    for (uint32_t i = 0; i < A3D_MAX_BANDS; i++)
    {
        a3d_chain *ch = &g_chain[i];
        uint32_t first = 0, bytes = 0;
        if (i < a3d_B.bands && ch->n)
        {
            ch->used[ch->n - 1u] = a3d_B.band[i].list.pos;
            uint32_t next = ch->used[ch->n - 1u];
            for (uint32_t k = ch->n - 1u; k-- > 0;)
            {
                uint8_t *e = ch->chunk[k] + ch->used[k];
                rix_put32(e, A3D_OP_LINK | next);
                rix_put32(e + 4, a3d_heap_phys(ch->chunk[k + 1u]));
                next = ch->used[k] + A3D_LINK_RESERVE;
            }
            first = a3d_heap_phys(ch->chunk[0]);
            bytes = next;
            for (uint32_t k = 0; k < ch->n; k++)
            {
                const uint32_t used = ch->used[k] + (k + 1u < ch->n ? A3D_LINK_RESERVE : 0u);
                a3d_plat_clean_nc(ch->chunk[k], used);
                list_bytes += used;
                if (g_held[area].n < A3D_CHUNKS)
                    g_held[area].chunk[g_held[area].n++] = ch->chunk[k];
            }
            ch->n = 0;
        }
        rix_put32(a + 32u + 8u * i, first);
        rix_put32(a + 36u + 8u * i, bytes);
    }
    a3d_plat_clean_nc(a, dir_bytes);
    a3d_cnt.list_bytes += list_bytes + dir_bytes;
    a3d_chains_release();
    /* hand it over */
    a3d_listset ls;
    memset(&ls, 0, sizeof(ls));
    ls.fence = fence;
    ls.bands = a3d_B.bands;
    ls.width = c->tgt.width;
    ls.height = c->tgt.height;
    ls.band_lines = a3d_B.band_lines;
    ls.dir = a;
    ls.dir_phys = a3d_heap_phys(a);
    ls.ctx = ci;
    c->frame_passes++;
    ls.passes = c->frame_passes;
    if (why == A3D_SUB_FRAME)
    {
        ls.flags |= A3D_LS_FRAME;
        ls.frame = c->frames;
        c->frame_passes = 0;
        a3d_cnt.sub_frame++;
    }
    ls.color_be = c->tgt.direct && c->tgt.color_be;
    ls.region[0].phys = a3d_heap_phys_base();
    ls.region[0].bytes = A3D_HEAP_SPAN0_BYTES;
    ls.region[0].va = a3d_heap_va_base();
    ls.nregions = 1;
    if (c->tgt.direct)
    {
        ls.region[1].phys = c->tgt.phys;
        ls.region[1].bytes = c->tgt.bprow * c->tgt.height;
        ls.region[1].va = c->tgt.host;
        ls.nregions = 2;
    }
    a3d_P.open = 0;
    const uint32_t direct_gpu = c->tgt.direct && g_be->id == A3D_BE_FABRIC;
    if (direct_gpu)
    {
        a3d_direct_maint(&c->tgt, committed, a3d_B.band_lines);
        a3d_cnt.direct_cleans++;
    }
    if (!c->tgt.direct || direct_gpu)
    {
        for (uint32_t k = 0; k < A3D_MAX_PRESENT; k++)
            if (g_present[k].fence == 0)
            {
                g_present[k].fence = fence;
                g_present[k].ctx = ci;
                g_present[k].tgt = c->tgt;
                if (!c->tgt.direct)
                {
                    c->priv_fence = fence;
                    c->priv_guest = c->tgt.guest;
                    a3d_pbuf_presented(c, fence);
                }
                g_present[k].bands = committed;
                g_present[k].band_lines = a3d_B.band_lines;
                break;
            }
    }
    if (g_be->submit(&ls) != 0)
        a3d_cnt.errors++;
    if (why == A3D_SUB_FRAME)
        c->last_frame_fence = fence;
    g_fence = fence;
    g_area_fence[area] = fence;
    c->fence_sub = fence;
    a3d_cnt.passes++;
    a3d_priv_put(c, A3D_P_FENCE_SUB, fence);
    a3d_priv_put(c, A3D_P_PENDING, c->fence_sub > c->fence_done ? 1u : 0u);
    return fence;
}

/* completions: fences, deferred texture frees, presents of private targets */
static void a3d_poll(void)
{
    const uint32_t done = g_be->completed();
    if (done == g_done_polled && !a3d_poll_dirty && !a3d_state_full_on)
    {
        a3d_poll_quick();
        return;
    }
    a3d_poll_dirty = 0;
    for (;;)
    {
        uint32_t best = A3D_MAX_PRESENT;
        for (uint32_t k = 0; k < A3D_MAX_PRESENT; k++)
            if (g_present[k].fence && g_present[k].fence <= done
                && (best == A3D_MAX_PRESENT || g_present[k].fence < g_present[best].fence))
                best = k;
        if (best == A3D_MAX_PRESENT)
            break;
        const uint32_t f = g_present[best].fence;
        if (f - 1u > g_done_polled)
            g_done_polled = f - 1u;
        a3d_plat_present_begin();
        a3d_present_run(&g_present[best]);
        g_present[best].fence = 0;
        if (f > g_done_polled)
            g_done_polled = f;
    }
    for (uint32_t i = 0; i < 2u; i++)
        if (g_area_fence[i] && g_area_fence[i] <= done)
        {
            g_area_fence[i] = 0;
            a3d_held_release(i);
        }
    a3d_tex_completed(done);
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        const uint32_t d = done < c->fence_sub ? done : c->fence_sub;
        if (d != c->fence_done)
        {
            c->fence_done = d;
            a3d_priv_put(c, A3D_P_FENCE_DONE, d);
        }
        const uint32_t pend = (c->fence_sub > c->fence_done) || (a3d_P.open && a3d_P.ctx == i);
        a3d_priv_put(c, A3D_P_PENDING, pend);
    }
    a3d_poll_noted();
    a3d_cnt.fences_done = done;
    g_done_polled = done;
}

/* the open pass is c's and holds something to draw */
static int a3d_open_work(const a3d_ctx *c)
{
    return a3d_P.open && a3d_P.ctx == a3d_ctx_index(c) && a3d_P.work;
}

static uint32_t a3d_prot_key(const a3d_ctx *c, uint32_t slot)
{
    return a3d_ctx_index(c) * A3D_PROT_SLOTS + slot;
}

static uint32_t g_prot_clock;              /* orders the slots' use (the eviction) */

static void a3d_prot_release_slot_f(a3d_ctx *c, uint32_t slot, uint32_t finish)
{
    if (!(c->prot & (1u << slot)))
        return;
    if (finish && c->prot_fence[slot] > g_done_polled)
    {
        a3d_drain_to(c->prot_fence[slot]);
        a3d_cnt.park_finishes++;
    }
    c->prot_fence[slot] = 0;
    a3d_plat_prot_set(a3d_prot_key(c, slot), 0, 0, 0);
    c->prot &= ~(1u << slot);
    if (c->prot_cur == slot + 1u)
        c->prot_cur = 0;
    a3d_cnt.prot_clears++;
}

static void a3d_prot_release_slot(a3d_ctx *c, uint32_t slot)
{
    a3d_prot_release_slot_f(c, slot, 1u);
}

static void a3d_prot_release(a3d_ctx *c)
{
    for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
        a3d_prot_release_slot_f(c, s, 0u);
    c->prot_cur = 0;
}

/* the promise is kept: the work is finished (the pages stay armed: no MMU entry is written) */
static void a3d_promise_kept(a3d_ctx *c)
{
    c->lazy = 0;
    if (c->prot_cur)
        a3d_plat_prot_pending(a3d_prot_key(c, c->prot_cur - 1u), 0);
}

static void a3d_prot_leave(a3d_ctx *c)
{
    if (c->lazy)
        a3d_cnt.closes++;
    a3d_promise_kept(c);
    c->prot_cur = 0;
}

static void a3d_prot_park(a3d_ctx *c)
{
    const uint32_t s = c->prot_cur - 1u;
    const uint32_t f = c->fence_sub;            /* everything c submitted: this target's work among it */
    if (f > g_done_polled)
    {
        if (!c->lazy)
            a3d_plat_prot_pending(a3d_prot_key(c, s), 1);
        if (f > c->prot_fence[s])
            c->prot_fence[s] = f;
        a3d_cnt.parks++;
    }
    else
        a3d_promise_kept(c);                    /* nothing in flight after all: as a leave */
    c->lazy = 0;
    c->prot_cur = 0;
}

static int a3d_prot_can_park(const a3d_ctx *c)
{
    return a3d_prot_enabled && c->prot_cur != 0u;
}

/* every set c submitted is covered by a parked promise (its slot pending until that work is presented) */
static int a3d_prot_parked_all(const a3d_ctx *c)
{
    for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
        if (((c->prot >> s) & 1u) && c->prot_fence[s] >= c->fence_sub)
            return 1;
    return 0;
}

static void a3d_priv_guard(a3d_ctx *c, uint32_t fill)
{
    if (c->tgt.direct)
        return;
    const a3d_pbuf *b = a3d_pbuf_cur(c);
    uint32_t f = 0, own = 0;
    if (b == 0)
        f = (c->priv_fence > g_done_polled && c->priv_guest != c->tgt.guest) ? c->priv_fence : 0u;
    else if (b->fence > g_done_polled && b->pguest != c->tgt.guest)
        f = b->fence;
    else if (fill && b->refill > g_done_polled)
    {
        f = b->refill;
        own = 1;
    }
    if (f)
    {
        a3d_cnt.priv_guard_waits++;
        a3d_r8cnt.pbuf_own_waits += own;
        a3d_drain_to(f);
    }
}

static int a3d_lazy(a3d_ctx *c, uint32_t fence)
{
    uint32_t say_pending = !c->lazy;            /* a new promise: tell the platform (re-arms if disarmed) */
    if (a3d_prot_enabled && !c->prot_cur)
    {
        if (!c->tgt_valid)
            return 0;
        const uint32_t bytes = c->tgt.bprow * c->tgt.height;
        const uint32_t lo = c->tgt.guest, base = c->tgt.base < lo ? c->tgt.base : lo;
        uint32_t slot = A3D_PROT_SLOTS, victim = A3D_PROT_SLOTS;
        for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
        {
            const uint32_t reg = (c->prot >> s) & 1u;
            if (reg && c->prot_lo[s] == lo && c->prot_hi[s] == lo + bytes && c->prot_base[s] == base)
            {
                slot = s;                       /* registered before: a double buffer's other half */
                break;
            }
            const uint32_t vreg = victim < A3D_PROT_SLOTS ? (c->prot >> victim) & 1u : 1u;
            /* a free slot beats a registered one; among registered ones the least recently current */
            if (victim == A3D_PROT_SLOTS || (!reg && vreg) || (reg && vreg && c->prot_used[s] < c->prot_used[victim]))
                victim = s;
        }
        if (slot < A3D_PROT_SLOTS)
            say_pending = 1;
        else
        {
            slot = victim;
            a3d_prot_release_slot(c, slot);     /* evicts the least recently used target, if all are taken */
            if (!a3d_plat_prot_set(a3d_prot_key(c, slot), base, lo, bytes))
            {
                a3d_cnt.prot_refused++;
                return 0;
            }
            c->prot |= 1u << slot;
            c->prot_base[slot] = base;
            c->prot_lo[slot] = lo;
            c->prot_hi[slot] = lo + bytes;
            a3d_cnt.prot_sets++;
            say_pending = 0;                    /* a fresh registration is armed and pending */
        }
        c->prot_cur = slot + 1u;
        c->prot_used[slot] = ++g_prot_clock;
    }
    if (c->prot_cur && say_pending)
        a3d_plat_prot_pending(a3d_prot_key(c, c->prot_cur - 1u), 1);
    c->lazy = 1;
    if (fence > c->lazy_fence)
        c->lazy_fence = fence;
    return 1;
}

/* the promise still covers unfinished work: binned in the open pass, or submitted and not complete */
static int a3d_lazy_open(const a3d_ctx *c)
{
    if (!c->lazy)
        return 0;
    if (a3d_open_work(c) && a3d_P.fence <= c->lazy_fence)
        return 1;
    return g_done_polled < c->lazy_fence;
}

static void a3d_prot_sync(void)
{
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        if (c->lazy && !a3d_lazy_open(c))
            a3d_promise_kept(c);
        for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
            if (c->prot_fence[s] && g_done_polled >= c->prot_fence[s])
            {
                c->prot_fence[s] = 0;
                if (!(c->lazy && c->prot_cur == s + 1u))
                    a3d_plat_prot_pending(a3d_prot_key(c, s), 0);
            }
    }
}

static uint32_t a3d_parked_fence(const a3d_ctx *c, uint32_t lo, uint32_t hi)
{
    uint32_t f = 0;
    for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
        if (c->prot_fence[s] > g_done_polled && ((c->prot >> s) & 1u) && lo < c->prot_hi[s] && hi > c->prot_base[s]
            && c->prot_fence[s] > f)
            f = c->prot_fence[s];
    return f;
}

/* finish everything c has: submit its open pass (the one cut it needs) and drain to its last fence */
static void a3d_finish(a3d_ctx *c)
{
    if (a3d_open_work(c))
        a3d_submit(c, A3D_SUB_TOUCH);
    a3d_drain_to(c->fence_sub);
    a3d_promise_kept(c);
}

uint32_t a3d_touch(uint32_t lo, uint32_t hi, uint32_t why)
{
    if (!g_ready)
        return 0;
    const uint32_t t0 = a3d_plat_cycles();
    uint32_t n = 0;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        const uint32_t pf = a3d_parked_fence(c, lo, hi);
        if (pf)
        {
            a3d_drain_to(pf);
            a3d_cnt.park_finishes++;
            n++;
        }
        if (!c->lazy || !c->prot_cur)
            continue;
        const uint32_t k = c->prot_cur - 1u;    /* the current target's promise */
        if (lo < c->prot_hi[k] && hi > c->prot_base[k])
        {
            a3d_finish(c);
            n++;
        }
    }
    a3d_poll();
    a3d_prot_sync();
    if ((why & 0xFFu) == A3D_TOUCH_FAULT)
        a3d_cnt.fault_flushes += n;
    else
        a3d_cnt.fence_flushes += n;
    a3d_cnt.fault_cycles += (a3d_plat_cycles() - t0) >> 6;
    return n;
}

uint32_t a3d_fence_of(uint32_t lo, uint32_t hi)
{
    uint32_t f = 0;
    if (!g_ready)
        return 0;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        const a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        const uint32_t pf = a3d_parked_fence(c, lo, hi);
        if (pf > f)
            f = pf;
        if (!c->lazy || !c->prot_cur)
            continue;
        const uint32_t k = c->prot_cur - 1u;
        if (!(lo < c->prot_hi[k] && hi > c->prot_base[k]))
            continue;
        const uint32_t cf = (a3d_open_work(c) && a3d_P.fence <= c->lazy_fence) ? a3d_P.fence : c->lazy_fence;
        if (cf > g_done_polled && cf > f)
            f = cf;
    }
    return f;
}

uint32_t a3d_fence_presented(uint32_t fence)
{
    return g_done_polled >= fence;
}

/* the open pass holds work up to `fence`: it must be submitted for that fence to complete */
static void a3d_fence_kick(uint32_t fence)
{
    if (a3d_P.open && a3d_P.work && a3d_P.fence <= fence)
    {
        a3d_submit(&a3d_ctxs[a3d_P.ctx], A3D_SUB_TOUCH);
        a3d_cnt.fence_flushes++;
    }
}

uint32_t a3d_fence_try(uint32_t fence)
{
    if (!g_ready || g_done_polled >= fence)
        return 1;
    a3d_fence_kick(fence);
    g_be->progress(g_be->async ? 1u : a3d_wait_budget);
    a3d_poll();                             /* presents what completed -- g_done_polled */
    a3d_prot_sync();                        /* promises whose work is presented are kept */
    const uint32_t r = g_done_polled >= fence;
    if (!r)
        a3d_cnt.fence_tries++;
    return r;
}

void a3d_fence_wait(uint32_t fence)
{
    if (!g_ready || g_done_polled >= fence)
        return;
    a3d_fence_kick(fence);
    a3d_drain_to(fence);                    /* the GPU, then a poll: the presents through `fence` run */
    a3d_prot_sync();
}

uint32_t a3d_prot_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
        for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
            n += a3d_ctxs[i].used && ((a3d_ctxs[i].prot >> s) & 1u);
    return n;
}

uint32_t a3d_lazy_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
        n += a3d_ctxs[i].used && a3d_ctxs[i].lazy;
    return n;
}

static a3d_ctx *a3d_ctx_lookup(uint32_t a0)
{
    if ((a0 & 1u) || !a3d_g_ok(a0, W3D_CTX_SIZE_V4, A3D_GR_READ))
        return 0;
    const uint32_t priv = a3d_be32(a3d_g_ptr(a0 + W3D_CTX_DRIVER));
    if ((priv & 1u) || !a3d_g_ok(priv, A3D_PRIV_SIZE, A3D_GR_READ | A3D_GR_WRITE))
        return 0;
    const uint8_t *p = a3d_g_ptr(priv);
    if (a3d_be32(p + A3D_P_MAGIC) != A3D_PRIV_MAGIC)
        return 0;
    const uint32_t h = a3d_be32(p + A3D_P_HANDLE);
    const uint32_t idx = (h & 0xFFu) - 1u;
    if (idx >= A3D_MAX_CTX)
        return 0;
    a3d_ctx *c = &a3d_ctxs[idx];
    if (!c->used || c->gen != (h >> 8) || c->gctx != a0 || c->gpriv != priv)
        return 0;
    return c;
}

static int32_t a3d_ctx_create(uint32_t a0)
{
    if ((a0 & 1u) || !a3d_g_ok(a0, W3D_CTX_SIZE_V4, A3D_GR_READ | A3D_GR_WRITE))
        return W3D_ILLEGALINPUT;
    const uint32_t priv = a3d_be32(a3d_g_ptr(a0 + W3D_CTX_DRIVER));
    if ((priv & 1u) || !a3d_g_ok(priv, A3D_PRIV_SIZE, A3D_GR_READ | A3D_GR_WRITE))
        return W3D_ILLEGALINPUT;
    uint8_t *p = a3d_g_ptr(priv);
    if (a3d_be32(p + A3D_P_MAGIC) != A3D_PRIV_MAGIC || a3d_be32(p + A3D_P_VERSION) != A3D_ABI_VERSION)
        return W3D_ILLEGALINPUT;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)       /* the same block twice: refuse the second */
        if (a3d_ctxs[i].used && (a3d_ctxs[i].gctx == a0 || a3d_ctxs[i].gpriv == priv))
            return W3D_ILLEGALINPUT;
    uint32_t idx = A3D_MAX_CTX;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
        if (!a3d_ctxs[i].used)
        {
            idx = i;
            break;
        }
    if (idx == A3D_MAX_CTX)
        return W3D_NOMEMORY;
    a3d_ctx *c = &a3d_ctxs[idx];
    const uint32_t gen = (c->gen + 1u) & 0x00FFFFFFu;
    memset(c, 0, sizeof(*c));
    c->used = 1;
    c->gen = gen ? gen : 1u;
    c->gctx = a0;
    c->gpriv = priv;
    c->never_ended = 1;
    c->clear_depth = 0xFFFFu;
    c->clear_color = 0x000000FFu;
    a3d_target t;
    const int32_t e = a3d_target_read(c, &t);
    if (e != W3D_SUCCESS)
    {
        c->used = 0;
        return e;
    }
    a3d_wbe32(p + A3D_P_HANDLE, (c->gen << 8) | (idx + 1u));
    a3d_wbe32(p + A3D_P_FENCE_SUB, 0);
    a3d_wbe32(p + A3D_P_FENCE_DONE, 0);
    a3d_wbe32(p + A3D_P_PENDING, 0);
    a3d_cnt.contexts++;
    return W3D_SUCCESS;
}

void a3d_drain_to(uint32_t fence)
{
    while (g_be->completed() < fence)
        g_be->progress(0xFFFFFFFFu);
    a3d_poll();
}

static void a3d_ctx_drain(a3d_ctx *c)
{
    a3d_submit(c, 0);
    a3d_drain_to(c->fence_sub);
}

static int32_t a3d_ctx_destroy(a3d_ctx *c)
{
    a3d_ctx_drain(c);
    if (c->lazy)
        a3d_cnt.closes++;
    c->lazy = 0;
    a3d_prot_release(c);
    const uint32_t ci = a3d_ctx_index(c);
    a3d_tex_drop_ctx(ci);
    a3d_buffers_free(c);
    a3d_priv_put(c, A3D_P_HANDLE, 0);
    if (g_bin_owner == ci + 1u)
        g_bin_owner = 0;
    const uint32_t gen = c->gen;
    memset(c, 0, sizeof(*c));
    c->gen = gen;
    a3d_cnt.contexts--;
    return W3D_SUCCESS;
}

int a3d_init(uint32_t backend)
{
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
        for (uint32_t s = 0; s < A3D_PROT_SLOTS; s++)
            if ((a3d_ctxs[i].prot >> s) & 1u)
                a3d_plat_prot_set(i * A3D_PROT_SLOTS + s, 0, 0, 0);
    memset(&a3d_cnt, 0, sizeof(a3d_cnt));
    a3d_cnt.wait_mode = a3d_wait_mode;
    a3d_pbuf_reset();
    a3d_state_epoch++;
    memset(&a3d_P, 0, sizeof(a3d_P));
    memset(a3d_ctxs, 0, sizeof(a3d_ctxs));
    memset(g_present, 0, sizeof(g_present));
    a3d_tex_init();
    g_fence = 0;
    g_done_polled = 0;
    g_area_fence[0] = g_area_fence[1] = 0;
    memset(g_held, 0, sizeof(g_held));      /* (the heap was just initialised: every chunk is free) */
    memset(g_prof_depth, 0, sizeof(g_prof_depth));
    g_bin_owner = 0;
    g_ready = 0;
    if (!a3d_heap_ready())
        return 0;
    const uint32_t want = backend;
    if (backend == A3D_BE_FABRIC && !a3d_fabric_probe())
        backend = A3D_BE_SOFT;
    a3d_soft_le = backend == A3D_BE_SOFTLE;
    g_be = backend == A3D_BE_FABRIC ? &a3d_be_fabric
         : backend == A3D_BE_SOFTLE ? &a3d_be_softle : (backend == A3D_BE_SOFT ? &a3d_be_soft : &a3d_be_null);
    g_backend_id = g_be->id;
    a3d_cnt.backend = g_be->id | ((g_be->units() & 0xFu) << 4) | (want != backend ? 0x100u : 0u);
    g_be->reset();                          /* (FABRIC may add 0x200: the GPU still runs a list it gave up on) */
    a3d_bin_init(&a3d_B, A3D_LAZY, 1u, A3D_MAX_BANDS, 1u << 14, 0u);
    memset(g_chain, 0, sizeof(g_chain));
    a3d_chains_release();
    a3d_reset_all();
    g_ready = 1;
    return 1;
}

int a3d_ready(void)
{
    return (int)g_ready;
}

uint32_t a3d_backend_id(void)
{
    return g_backend_id;
}

uint32_t a3d_chunks_held(void)
{
    return g_held[0].n + g_held[1].n;
}

void a3d_reset_all(void)
{
    memset(g_present, 0, sizeof(g_present));
    if (g_be)
    {
        if (g_be->async && g_be->units())
            while (g_be->completed() < g_fence)
                g_be->progress(0xFFFFFFFFu);
        g_be->reset();
    }
    for (uint32_t i = 0; i < 2u; i++)
    {
        a3d_held_release(i);
        g_area_fence[i] = 0;
    }
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        a3d_prot_release(c);
        a3d_buffers_free(c);
        a3d_tex_drop_ctx(i);
        const uint32_t gen = c->gen;
        memset(c, 0, sizeof(*c));
        c->gen = gen;
    }
    a3d_P.open = 0;
    a3d_state_epoch++;
    a3d_poll_dirty = 1;
    a3d_chains_release();
    a3d_tex_retired_flush();
    a3d_cnt.contexts = 0;
}

static int32_t a3d_stats_copy(uint32_t a1)
{
    if (!a3d_g_ok(a1, 8, A3D_GR_READ))
        return W3D_ILLEGALINPUT;
    const uint32_t buf = a3d_be32(a3d_g_ptr(a1));
    uint32_t size = a3d_be32(a3d_g_ptr(a1 + 4));
    if (size > sizeof(a3d_cnt))
        size = sizeof(a3d_cnt);
    size &= ~3u;
    if (size == 0 || !a3d_g_ok(buf, size, A3D_GR_WRITE))
        return W3D_ILLEGALINPUT;
    const uint32_t *s = (const uint32_t *)&a3d_cnt;
    uint8_t *d = a3d_g_ptr(buf);
    for (uint32_t i = 0; i < size / 4u; i++)
        a3d_wbe32(d + 4u * i, s[i]);
    return (int32_t)size;
}

#define A3D_SC_KEEP ((1u << (A3D_DRAW - 0x300u)) | (1u << (A3D_CLEAR - 0x300u)) | (1u << (A3D_FLUSH - 0x300u))  \
                     | (1u << (A3D_WAIT - 0x300u)) | (1u << (A3D_FRAME - 0x300u)) | (1u << (A3D_ZSPAN - 0x300u))    \
                     | (1u << (A3D_STATS - 0x300u)) | (1u << (A3D_UNLOCK - 0x300u)) | (1u << (A3D_MGL_TRIS - 0x300u)))
void a3d_oncall(uint32_t id, uint32_t regs[4], uint32_t via_trap)
{
    const uint32_t d0 = regs[0], d1 = regs[1], a0 = regs[2], a1 = regs[3];
    int32_t r = W3D_ILLEGALINPUT;
    uint32_t r1 = 0;
    a3d_cnt.oncalls++;
    if (((id - 0x300u) > 31u) || !((A3D_SC_KEEP >> (id - 0x300u)) & 1u))
    {
        a3d_state_epoch++;
        a3d_poll_dirty = 1;                   /* (a texture freed or retired: the next poll walks them) */
    }
    if (via_trap)
        a3d_cnt.oncalls_trap++;
    else
        a3d_cnt.oncalls_jit++;
    if (!g_ready)
    {
        regs[0] = id == A3D_HELLO ? 0u : (uint32_t)W3D_NODRIVER;
        regs[1] = 0;
        return;
    }
    if (id == A3D_HELLO)
    {
        regs[0] = d0 == A3D_ABI_VERSION ? A3D_AGENT_VERSION : 0u;
        regs[1] = g_be->id | ((g_be->units() & 0xFu) << A3D_CAP_UNITS_SHIFT)
                | ((A3D_HEAP_BYTES >> 20) << A3D_CAP_HEAP_MB_SHIFT) | (via_trap ? A3D_CAP_TRAPPATH : 0u)
                | A3D_CAP_MGL;
        return;
    }
    if (id == A3D_STATS)
    {
        r = d0 == 0 ? a3d_stats_copy(a1) : W3D_ILLEGALINPUT;
        regs[0] = (uint32_t)r;
        regs[1] = 0;
        return;
    }
    if (id == A3D_CTX_CREATE)
    {
        r = a3d_ctx_create(a0);
        if (r != W3D_SUCCESS)
            a3d_cnt.errors++;
        regs[0] = (uint32_t)r;
        regs[1] = 0;
        return;
    }
    a3d_ctx *c = a3d_ctx_lookup(a0);
    if (c == 0)
    {
        a3d_cnt.errors++;
        regs[0] = (uint32_t)W3D_ILLEGALINPUT;
        regs[1] = 0;
        return;
    }
    a3d_poll();
    const uint32_t retry = c->wait_retry;
    c->wait_retry = 0;
    switch (id)
    {
    case A3D_CTX_DESTROY:
        r = a3d_ctx_destroy(c);
        regs[0] = (uint32_t)r;
        regs[1] = 0;
        return;                             /* c is gone: no private-block update below */
    case A3D_TARGET:
        a3d_submit(c, A3D_SUB_OTHER);
        c->never_ended = 0;
        r = W3D_SUCCESS;
        {
            a3d_target t;
            r = a3d_target_read(c, &t);
            if (r == W3D_SUCCESS && (c->lazy || c->prot_cur) && c->tgt_valid && !a3d_target_same(&t, &c->tgt))
            {
                if (c->depth && t.width == c->tgt.width && t.height == c->tgt.height && t.bw == c->tgt.bw
                    && t.direct == c->tgt.direct && (t.direct || c->tgt.priv) && a3d_prot_can_park(c))
                    a3d_prot_park(c);
                else
                {
                    a3d_drain_to(c->fence_sub);
                    a3d_prot_leave(c);
                }
            }
        }
        break;
    case A3D_TEX_ALLOC:
        a3d_prof_begin(A3D_PROF_TEX);
        r = a3d_tex_alloc(c, a1, d0);
        a3d_prof_end(A3D_PROF_TEX, &a3d_cnt.prof_tex);
        break;
    case A3D_TEX_FREE:
        r = a3d_tex_free(c, a1, d0);
        break;
    case A3D_DRAW:
        a3d_prof_begin(A3D_PROF_DRAW);
        r = a3d_draw(c, d0, a1);
        a3d_prof_end(A3D_PROF_DRAW, &a3d_cnt.prof_draw);
        break;
    case A3D_CLEAR:
        r = a3d_clear(c, d0, d1, a1);
        if (d0 == 2u)
            c->never_ended = 0;
        break;
    case A3D_FLUSH:
        g_be->progress(a3d_wait_budget / 4u);
        r = W3D_SUCCESS;
        r1 = (a3d_P.open && a3d_P.ctx == a3d_ctx_index(c)) ? a3d_P.fence : c->fence_sub;
        break;
    case A3D_WAIT:
    {
        uint32_t f = d0;
        const uint32_t open_mine = a3d_P.open && a3d_P.ctx == a3d_ctx_index(c);
        const uint32_t autom = a3d_wait_mode == A3D_WAIT_AUTO;
        a3d_cnt.wait_mode = a3d_wait_mode;
        if (!retry)
            a3d_cnt.waits++;
        if (f == 0)
            f = open_mine ? a3d_P.fence : c->fence_sub;
        if (open_mine && f >= a3d_P.fence)
        {
            int lazy = 0;
            if (autom)
            {
                if (!a3d_P.work)
                    lazy = 1;                       /* nothing binned: nothing to finish, nothing to cut */
                else if (a3d_lazy(c, a3d_P.fence))
                {
                    lazy = 1;
                    if (!retry)
                        a3d_cnt.lazy_waits++;
                }
            }
            if (!lazy)
            {
                a3d_submit(c, A3D_SUB_WAIT);
                a3d_cnt.wait_cuts++;
            }
        }
        if (f > c->fence_sub)
            f = c->fence_sub;               /* a fence never submitted cannot be waited for */
        const uint32_t quick = autom && g_be->async;
        {
            g_be->progress(quick ? 1u : a3d_wait_budget);
        }
        a3d_poll();
        r = g_done_polled >= f ? 0 : 1;
        if (r == 1 && autom && g_be->async && a3d_lazy(c, f))
        {
            r = 0;                              /* submitted work left to the protection (async backend) */
            if (!retry)
                a3d_cnt.lazy_waits++;
        }
        else if (r == 1 && quick)
        {
            g_be->progress(a3d_wait_budget);
            a3d_poll();
            r = g_done_polled >= f ? 0 : 1;
        }
        r1 = g_done_polled;
        c->wait_retry = r == 1;
        break;
    }
    case A3D_UNLOCK:
        a3d_cnt.unlocks++;
        a3d_cnt.wait_mode = a3d_wait_mode;
        r = W3D_SUCCESS;
        if (a3d_wait_mode == A3D_WAIT_AUTO)
        {
            const uint32_t f = a3d_open_work(c) ? a3d_P.fence : c->fence_sub;
            if ((a3d_open_work(c) || g_done_polled < f) && a3d_lazy(c, f))
                a3d_cnt.lazy_waits++;
        }
        break;
    case A3D_FRAME:
        r1 = a3d_submit(c, A3D_SUB_FRAME);
        g_be->progress(a3d_wait_budget / 4u);
        c->frames++;
        c->never_ended = 0;
        a3d_cnt.frames++;
        a3d_priv_put(c, A3D_P_FRAMES, c->frames);
        r = W3D_SUCCESS;
        break;
    case A3D_ZSPAN:
        r = a3d_zspan(c, d0, a1);
        break;
    case A3D_TEX_UPDATE:
        a3d_prof_begin(A3D_PROF_TEX);
        r = a3d_tex_update(c, d0, a1);
        a3d_prof_end(A3D_PROF_TEX, &a3d_cnt.prof_tex);
        break;
    default:
        {
            const uint64_t v = a3d_oncall_more(c, id, d0, a1);
            r = (int32_t)(uint32_t)v;
            r1 = (uint32_t)(v >> 32);
        }
        break;
    }
    if (r < 0)
    {
        c->errors++;
        a3d_cnt.errors++;
        a3d_priv_put(c, A3D_P_ERRORS, c->errors);
    }
    a3d_poll();
    a3d_prot_sync();
    regs[0] = (uint32_t)r;
    regs[1] = r1;
}

static uint32_t a3d_fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (uint32_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

A3D_LATE_TEXT uint32_t a3d_state_hash(void)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        const a3d_ctx *c = &a3d_ctxs[i];
        const uint32_t w[10] = { c->used, c->gen, c->gctx, c->gpriv, c->fence_sub, c->fence_done,
                                 c->frames, c->errors, (uint32_t)c->depth_band_valid, c->tgt.guest };
        h = a3d_fnv(h, w, sizeof(w));
    }
    h = a3d_fnv(h, &a3d_P, sizeof(a3d_P));
    for (uint32_t i = 0; i < a3d_B.bands; i++)
    {
        const a3d_chain *ch = &g_chain[i];
        h = a3d_fnv(h, &ch->n, 4);
        for (uint32_t k = 0; k < ch->n; k++)
        {
            const uint32_t used = (k + 1u == ch->n) ? a3d_B.band[i].list.pos : ch->used[k];
            h = a3d_fnv(h, ch->chunk[k], used);
        }
    }
    h = a3d_fnv(h, &g_fence, 4);
    a3d_stats_t s = a3d_cnt;
    s.oncalls_trap = s.oncalls_jit = 0;     /* the only counters that legitimately differ by path */
    s.prof_draw = s.prof_submit = s.prof_present = s.prof_tex = s.prof_wait = 0;
    s.fab_kick_clocks = s.fab_kick_max = s.fab_wait_polls = 0;
    s.vp_draws = s.vp_verts = s.vp_refused = 0;
    s.vp_neon = s.vp_redo = s.vp_scalar = s.vp_batches = 0;
    h = a3d_fnv(h, &s, sizeof(s));
    return a3d_fnv(h, (uint32_t[]){ a3d_tex_hash() }, 4);
}
