/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: band fills and the target cache (placed in the image's tail). */

#include <stdint.h>
#include <string.h>
#include "a3d_agent.h"

static uint32_t a3d_ctx_index(const a3d_ctx *c)     /* (a3d_agent.c's, a copy) */
{
    return (uint32_t)(c - a3d_ctxs);
}

static void a3d_priv_put(const a3d_ctx *c, uint32_t off, uint32_t v)    /* (a3d_agent.c's, a copy) */
{
    if (a3d_g_ok(c->gpriv + off, 4, A3D_GR_WRITE))
        a3d_wbe32(a3d_g_ptr(c->gpriv + off), v);
}

static a3d_pbuf g_pbuf[A3D_MAX_CTX][A3D_PBUFS] A3D_R8_BSS;
static uint32_t g_pbuf_clock A3D_R8_BSS;
uint32_t a3d_pbuf_shared_on A3D_R8_BSS;
a3d_r8_stats_t a3d_r8cnt A3D_R8_BSS;

/* c's current private buffer's slot (0: none -- a direct target) */
a3d_pbuf *a3d_pbuf_cur(const a3d_ctx *c)
{
    if (c->tgt.priv == 0)
        return 0;
    a3d_pbuf *P = g_pbuf[a3d_ctx_index(c)];
    for (uint32_t s = 0; s < A3D_PBUFS; s++)
        if (P[s].va == c->tgt.priv)
            return &P[s];
    return 0;
}

/* a set's present will read c's current buffer */
void a3d_pbuf_presented(const a3d_ctx *c, uint32_t fence)
{
    a3d_pbuf *b = a3d_pbuf_cur(c);
    if (b)
    {
        b->fence = fence;
        b->pguest = c->tgt.guest;
    }
}

void a3d_pbuf_free_all(a3d_ctx *c)
{
    a3d_pbuf *P = g_pbuf[a3d_ctx_index(c)];
    uint32_t cur_freed = 0;
    for (uint32_t s = 0; s < A3D_PBUFS; s++)
    {
        if (P[s].va)
        {
            cur_freed |= P[s].va == c->tgt.priv;
            a3d_pool_free(P[s].va);
        }
        memset(&P[s], 0, sizeof(P[s]));
    }
    if (c->tgt.priv && !cur_freed)
        a3d_pool_free(c->tgt.priv);         /* (a buffer outside the slots: cannot happen) */
}

/* the first buffer of a new geometry (a3d_target_bind allocated it as c->tgt.priv) into slot 0 */
void a3d_pbuf_first(a3d_ctx *c)
{
    a3d_pbuf *P = g_pbuf[a3d_ctx_index(c)];
    memset(P, 0, sizeof(g_pbuf[0]));
    P[0].va = c->tgt.priv;
    P[0].phys = c->tgt.priv_phys;
    P[0].guest = c->tgt.guest;
    P[0].used = ++g_pbuf_clock;
}

void a3d_pbuf_pick(a3d_ctx *c, a3d_target *t)
{
    a3d_pbuf *P = g_pbuf[a3d_ctx_index(c)], *b = 0;
    if (t->direct)
    {
        t->priv = c->tgt.priv;
        t->priv_phys = c->tgt.priv_phys;
        return;
    }
    for (uint32_t s = 0; s < A3D_PBUFS && !b; s++)
        if (P[s].va && P[s].guest == t->guest)
            b = &P[s];                                  /* its own */
    if (!b && !a3d_pbuf_shared_on)
        for (uint32_t s = 0; s < A3D_PBUFS; s++)
            if (P[s].va == 0)
            {
                const uint32_t bytes = t->bw * t->height * 2u;
                uint8_t *va = a3d_pool_free_bytes() >= bytes + A3D_PBUF_RESERVE ? (uint8_t *)a3d_pool_alloc(bytes) : 0;
                if (va)
                {
                    if (t->bw > t->width)       /* the padding columns zero, as the first buffer's (a3d_target_bind) */
                        for (uint32_t y = 0; y < t->height; y++)
                            memset(va + 2u * (y * t->bw + t->width), 0, 2u * (t->bw - t->width));
                    memset(&P[s], 0, sizeof(P[s]));
                    P[s].va = va;
                    P[s].phys = a3d_heap_phys(va);
                    b = &P[s];
                    a3d_r8cnt.pbuf_allocs++;
                }
                else
                    a3d_r8cnt.pbuf_refused++;
                break;
            }
    if (!b)
    {
        for (uint32_t s = 0; s < A3D_PBUFS; s++)
            if (P[s].va && (!b || P[s].used < b->used))
                b = &P[s];
        a3d_r8cnt.pbuf_shares++;
    }
    if (!b)
    {
        t->priv = c->tgt.priv;                          /* (no slot holds a buffer: cannot happen -- keep needs one) */
        t->priv_phys = c->tgt.priv_phys;
        return;
    }
    /* its own target's present still owed: the first band fill waits for it (a3d_priv_guard) */
    b->refill = (b->fence && !a3d_fence_presented(b->fence) && b->pguest == t->guest) ? b->fence : 0u;
    b->guest = t->guest;
    b->used = ++g_pbuf_clock;
    t->priv = b->va;
    t->priv_phys = b->phys;
}

uint32_t a3d_pbuf_trim(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        for (uint32_t s = 0; s < A3D_PBUFS; s++)
        {
            a3d_pbuf *b = &g_pbuf[i][s];
            if (b->va == 0 || b->va == c->tgt.priv)
                continue;
            if (b->fence && !a3d_fence_presented(b->fence))
                a3d_drain_to(b->fence);
            a3d_pool_free(b->va);
            memset(b, 0, sizeof(*b));
            n++;
        }
    }
    a3d_r8cnt.pbuf_trims += n;
    return n;
}

void *a3d_pool_alloc_t(uint32_t bytes)
{
    void *p = a3d_pool_alloc(bytes);
    if (p == 0 && a3d_pbuf_trim())
        p = a3d_pool_alloc(bytes);
    return p;
}

__attribute__((weak)) void a3d_plat_clean_nc(const void *p, uint32_t len)
{
    a3d_plat_clean(p, len);
}

void a3d_pbuf_reset(void)
{
    memset(g_pbuf, 0, sizeof(g_pbuf));
}

void a3d_priv_fill_ref(const a3d_target *t, uint32_t y0, uint32_t y1, uint32_t le)
{
    for (uint32_t y = y0; y < y1; y++)
        for (uint32_t x = 0; x < t->width; x++)
        {
            const uint8_t *s = t->host + y * t->bprow;
            uint32_t r = 0, g = 0, b = 0;
            switch (t->format)
            {
            case W3D_FMT_A8R8G8B8: r = s[4u * x + 1u]; g = s[4u * x + 2u]; b = s[4u * x + 3u]; break;
            case W3D_FMT_A8B8G8R8: b = s[4u * x + 1u]; g = s[4u * x + 2u]; r = s[4u * x + 3u]; break;
            case W3D_FMT_R8G8B8A8: r = s[4u * x + 0u]; g = s[4u * x + 1u]; b = s[4u * x + 2u]; break;
            case W3D_FMT_B8G8R8A8: b = s[4u * x + 0u]; g = s[4u * x + 1u]; r = s[4u * x + 2u]; break;
            case W3D_FMT_R8G8B8:   r = s[3u * x + 0u]; g = s[3u * x + 1u]; b = s[3u * x + 2u]; break;
            case W3D_FMT_B8G8R8:   b = s[3u * x + 0u]; g = s[3u * x + 1u]; r = s[3u * x + 2u]; break;
            default:
            {
                uint16_t v = a3d_be16(s + 2u * x);
                if (t->format == W3D_FMT_R5G6B5PC || t->format == W3D_FMT_B5G6R5PC
                    || t->format == W3D_FMT_R5G5B5PC || t->format == W3D_FMT_B5G5R5PC)
                    v = (uint16_t)((v >> 8) | (v << 8));
                if (t->format == W3D_FMT_R5G6B5 || t->format == W3D_FMT_R5G6B5PC)
                { r = (v >> 8) & 0xF8u; g = (v >> 3) & 0xFCu; b = (v << 3) & 0xF8u; }
                else if (t->format == W3D_FMT_B5G6R5 || t->format == W3D_FMT_B5G6R5PC)
                { b = (v >> 8) & 0xF8u; g = (v >> 3) & 0xFCu; r = (v << 3) & 0xF8u; }
                else if (t->format == W3D_FMT_R5G5B5 || t->format == W3D_FMT_R5G5B5PC)
                { r = (v >> 7) & 0xF8u; g = (v >> 2) & 0xF8u; b = (v << 3) & 0xF8u; }
                else
                { b = (v >> 7) & 0xF8u; g = (v >> 2) & 0xF8u; r = (v << 3) & 0xF8u; }
            }
            }
            const uint16_t px565 = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
            if (le)
                a3d_wle16(t->priv + 2u * (y * t->bw + x), px565);
            else
                a3d_wbe16(t->priv + 2u * (y * t->bw + x), px565);
        }
}

uint32_t a3d_fill_ref_on A3D_R8_BSS;

/* ---- the fill's pixel kinds and NEON (copies of the present's: a3d_agent.c) ---- */
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

#if defined(__arm__) && defined(__ARM_NEON__)
#define A3D_FILL_NEON 1
#define A3D_NSWAP "vrev16.8 q0, q0\n\tvrev16.8 q1, q1\n\tvrev16.8 q2, q2\n\tvrev16.8 q3, q3\n\t"
#define A3D_N_ALL(M) M("q0") M("q1") M("q2") M("q3")
#endif

static void a3d_fill_row_c(const uint8_t *s, uint8_t *d, uint32_t x, uint32_t w, const a3d_pconv *k)
{
    const uint32_t o0 = k->le ? 0u : 8u, o1 = k->le ? 8u : 0u;      /* the private buffer's byte order */
#define A3D_FV(x) (k->pc ? ((uint32_t)s[2u * (x)] | ((uint32_t)s[2u * (x) + 1u] << 8))                       \
                         : (((uint32_t)s[2u * (x)] << 8) | (uint32_t)s[2u * (x) + 1u]))
#define A3D_FOUT(PX) do { const uint32_t px_ = (PX); d[2u * x] = (uint8_t)(px_ >> o0);                         \
                          d[2u * x + 1u] = (uint8_t)(px_ >> o1); } while (0)
#define A3D_F565(R, G, B) ((((uint32_t)(R) & 0xF8u) << 8) | (((uint32_t)(G) & 0xFCu) << 3) | ((uint32_t)(B) >> 3))
    switch (k->kind)
    {
    case A3D_PK_565:    for (; x < w; x++) { const uint32_t v = A3D_FV(x); A3D_FOUT(v); } break;
    case A3D_PK_BGR565: for (; x < w; x++) { const uint32_t v = A3D_FV(x);
                                             A3D_FOUT(((v & 0x1Fu) << 11) | (v & 0x07E0u) | (v >> 11)); } break;
    case A3D_PK_555:    for (; x < w; x++) { const uint32_t v = A3D_FV(x);
                                             A3D_FOUT(((v << 1) & 0xFFC0u) | (v & 0x1Fu)); } break;
    case A3D_PK_BGR555: for (; x < w; x++) { const uint32_t v = A3D_FV(x);
                                             A3D_FOUT(((v & 0x1Fu) << 11) | ((v << 1) & 0x07C0u) | ((v >> 10) & 0x1Fu)); }
                        break;
    case A3D_PK_ARGB:   for (; x < w; x++) A3D_FOUT(A3D_F565(s[4u * x + 1u], s[4u * x + 2u], s[4u * x + 3u])); break;
    case A3D_PK_ABGR:   for (; x < w; x++) A3D_FOUT(A3D_F565(s[4u * x + 3u], s[4u * x + 2u], s[4u * x + 1u])); break;
    case A3D_PK_RGBA:   for (; x < w; x++) A3D_FOUT(A3D_F565(s[4u * x + 0u], s[4u * x + 1u], s[4u * x + 2u])); break;
    case A3D_PK_BGRA:   for (; x < w; x++) A3D_FOUT(A3D_F565(s[4u * x + 2u], s[4u * x + 1u], s[4u * x + 0u])); break;
    case A3D_PK_RGB:    for (; x < w; x++) A3D_FOUT(A3D_F565(s[3u * x + 0u], s[3u * x + 1u], s[3u * x + 2u])); break;
    default:            for (; x < w; x++) A3D_FOUT(A3D_F565(s[3u * x + 2u], s[3u * x + 1u], s[3u * x + 0u])); break;
    }
#undef A3D_F565
#undef A3D_FOUT
#undef A3D_FV
}

#ifdef A3D_FILL_NEON
#define A3D_FC_BGR565(q) "vshr.u16 q8, " q ", #11\n\tvshl.i16 q9, " q ", #11\n\tvand " q ", " q ", q12\n\t"      \
                         "vorr " q ", " q ", q8\n\tvorr " q ", " q ", q9\n\t"
#define A3D_FC_555(q)    "vshl.i16 q8, " q ", #1\n\tvand q8, q8, q13\n\tvand " q ", " q ", q14\n\t"             \
                         "vorr " q ", " q ", q8\n\t"
#define A3D_FC_BGR555(q) "vshl.i16 q8, " q ", #11\n\tvshl.i16 q9, " q ", #1\n\tvand q9, q9, q15\n\t"           \
                         "vshr.u16 " q ", " q ", #10\n\tvand " q ", " q ", q14\n\tvorr " q ", " q ", q8\n\t"      \
                         "vorr " q ", " q ", q9\n\t"
#define A3D_F16(SWIN, CONV, SWOUT)                                                                            \
    __asm__ volatile("vdup.16 q12, %[k0]\n\tvdup.16 q13, %[k1]\n\tvdup.16 q14, %[k2]\n\tvdup.16 q15, %[k3]\n\t"  \
                     "1:\n\t"                                                                                 \
                     "pld [%[s], #192]\n\t"                                                                   \
                     "vld1.8 {d0-d3}, [%[s]]!\n\t"                                                            \
                     "vld1.8 {d4-d7}, [%[s]]!\n\t"                                                            \
                     SWIN CONV SWOUT                                                                          \
                     "vst1.8 {d0-d3}, [%[d]]!\n\t"                                                            \
                     "vst1.8 {d4-d7}, [%[d]]!\n\t"                                                            \
                     "subs %[n], %[n], #1\n\t"                                                                \
                     "bne 1b\n\t"                                                                             \
                     : [s] "+r"(s), [d] "+r"(d), [n] "+r"(n)                                                  \
                     : [k0] "r"(0x07E0u), [k1] "r"(0xFFC0u), [k2] "r"(0x001Fu), [k3] "r"(0x07C0u)             \
                     : "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d16", "d17", "d18",   \
                       "d19", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31")
#define A3D_F32(LD, R, G, B, SWOUT)                                                                           \
    __asm__ volatile("1:\n\t"                                                                                 \
                     "pld [%[s], #128]\n\t"                                                                   \
                     LD                                                                                       \
                     "vshll.u8 q8, " R ", #8\n\tvshll.u8 q9, " G ", #8\n\tvshll.u8 q10, " B ", #8\n\t"       \
                     "vsri.16 q8, q9, #5\n\tvsri.16 q8, q10, #11\n\t"                                         \
                     SWOUT                                                                                    \
                     "vst1.8 {d16-d17}, [%[d]]!\n\t"                                                          \
                     "subs %[n], %[n], #1\n\t"                                                                \
                     "bne 1b\n\t"                                                                             \
                     : [s] "+r"(s), [d] "+r"(d), [n] "+r"(n)                                                  \
                     :                                                                                        \
                     : "cc", "memory", "d0", "d1", "d2", "d3", "d16", "d17", "d18", "d19", "d20", "d21")
#define A3D_FLD4 "vld4.8 {d0-d3}, [%[s]]!\n\t"
#define A3D_FLD3 "vld3.8 {d0-d2}, [%[s]]!\n\t"
#define A3D_FSW8 "vrev16.8 q8, q8\n\t"

static uint32_t a3d_fill_row_neon(const uint8_t *s, uint8_t *d, uint32_t w, const a3d_pconv *k)
{
    if (k->kind <= A3D_PK_BGR555)
    {
        uint32_t n = w / 32u;
        if (n == 0)
            return 0;
        const uint32_t done = 32u * n;
        const uint32_t sel = (k->pc ? 0u : 2u) | (k->le ? 0u : 1u);    /* bit 1: swap in, bit 0: swap out */
        switch (k->kind * 4u + sel)
        {
        case A3D_PK_565 * 4u + 0u:    A3D_F16("", "", ""); break;
        case A3D_PK_565 * 4u + 1u:    A3D_F16("", "", A3D_NSWAP); break;
        case A3D_PK_565 * 4u + 2u:    A3D_F16(A3D_NSWAP, "", ""); break;
        case A3D_PK_565 * 4u + 3u:    A3D_F16("", "", ""); break;          /* swapped in and out: a copy */
        case A3D_PK_BGR565 * 4u + 0u: A3D_F16("", A3D_N_ALL(A3D_FC_BGR565), ""); break;
        case A3D_PK_BGR565 * 4u + 1u: A3D_F16("", A3D_N_ALL(A3D_FC_BGR565), A3D_NSWAP); break;
        case A3D_PK_BGR565 * 4u + 2u: A3D_F16(A3D_NSWAP, A3D_N_ALL(A3D_FC_BGR565), ""); break;
        case A3D_PK_BGR565 * 4u + 3u: A3D_F16(A3D_NSWAP, A3D_N_ALL(A3D_FC_BGR565), A3D_NSWAP); break;
        case A3D_PK_555 * 4u + 0u:    A3D_F16("", A3D_N_ALL(A3D_FC_555), ""); break;
        case A3D_PK_555 * 4u + 1u:    A3D_F16("", A3D_N_ALL(A3D_FC_555), A3D_NSWAP); break;
        case A3D_PK_555 * 4u + 2u:    A3D_F16(A3D_NSWAP, A3D_N_ALL(A3D_FC_555), ""); break;
        case A3D_PK_555 * 4u + 3u:    A3D_F16(A3D_NSWAP, A3D_N_ALL(A3D_FC_555), A3D_NSWAP); break;
        case A3D_PK_BGR555 * 4u + 0u: A3D_F16("", A3D_N_ALL(A3D_FC_BGR555), ""); break;
        case A3D_PK_BGR555 * 4u + 1u: A3D_F16("", A3D_N_ALL(A3D_FC_BGR555), A3D_NSWAP); break;
        case A3D_PK_BGR555 * 4u + 2u: A3D_F16(A3D_NSWAP, A3D_N_ALL(A3D_FC_BGR555), ""); break;
        default:                      A3D_F16(A3D_NSWAP, A3D_N_ALL(A3D_FC_BGR555), A3D_NSWAP); break;
        }
        return done;
    }
    uint32_t n = w / 8u;
    if (n == 0)
        return 0;
    const uint32_t done = 8u * n;
    if (k->le)
        switch (k->kind)
        {
        case A3D_PK_ARGB: A3D_F32(A3D_FLD4, "d1", "d2", "d3", ""); break;
        case A3D_PK_ABGR: A3D_F32(A3D_FLD4, "d3", "d2", "d1", ""); break;
        case A3D_PK_RGBA: A3D_F32(A3D_FLD4, "d0", "d1", "d2", ""); break;
        case A3D_PK_BGRA: A3D_F32(A3D_FLD4, "d2", "d1", "d0", ""); break;
        case A3D_PK_RGB:  A3D_F32(A3D_FLD3, "d0", "d1", "d2", ""); break;
        default:          A3D_F32(A3D_FLD3, "d2", "d1", "d0", ""); break;
        }
    else
        switch (k->kind)
        {
        case A3D_PK_ARGB: A3D_F32(A3D_FLD4, "d1", "d2", "d3", A3D_FSW8); break;
        case A3D_PK_ABGR: A3D_F32(A3D_FLD4, "d3", "d2", "d1", A3D_FSW8); break;
        case A3D_PK_RGBA: A3D_F32(A3D_FLD4, "d0", "d1", "d2", A3D_FSW8); break;
        case A3D_PK_BGRA: A3D_F32(A3D_FLD4, "d2", "d1", "d0", A3D_FSW8); break;
        case A3D_PK_RGB:  A3D_F32(A3D_FLD3, "d0", "d1", "d2", A3D_FSW8); break;
        default:          A3D_F32(A3D_FLD3, "d2", "d1", "d0", A3D_FSW8); break;
        }
    return done;
}
#endif

static a3d_pconv a3d_fill_pconv_of(uint32_t fmt, uint32_t le)
{
    a3d_pconv k;
    k.le = le ? 1u : 0u;
    k.pc = fmt == W3D_FMT_R5G6B5PC || fmt == W3D_FMT_B5G6R5PC || fmt == W3D_FMT_R5G5B5PC || fmt == W3D_FMT_B5G5R5PC;
    k.bpp = 4u;
    if (fmt == W3D_FMT_A8R8G8B8)
        k.kind = A3D_PK_ARGB;
    else if (fmt == W3D_FMT_A8B8G8R8)
        k.kind = A3D_PK_ABGR;
    else if (fmt == W3D_FMT_R8G8B8A8)
        k.kind = A3D_PK_RGBA;
    else if (fmt == W3D_FMT_B8G8R8A8)
        k.kind = A3D_PK_BGRA;
    else
    {
        k.bpp = 3u;
        if (fmt == W3D_FMT_R8G8B8)
            k.kind = A3D_PK_RGB;
        else if (fmt == W3D_FMT_B8G8R8)
            k.kind = A3D_PK_BGR;
        else
        {
            k.bpp = 2u;
            if (fmt == W3D_FMT_R5G6B5 || fmt == W3D_FMT_R5G6B5PC)
                k.kind = A3D_PK_565;
            else if (fmt == W3D_FMT_B5G6R5 || fmt == W3D_FMT_B5G6R5PC)
                k.kind = A3D_PK_BGR565;
            else if (fmt == W3D_FMT_R5G5B5 || fmt == W3D_FMT_R5G5B5PC)
                k.kind = A3D_PK_555;
            else
                k.kind = A3D_PK_BGR555;
        }
    }
    return k;
}

void a3d_fill_row(const uint8_t *s, uint8_t *d, uint32_t w, uint32_t fmt, uint32_t le)
{
    const a3d_pconv k = a3d_fill_pconv_of(fmt, le);
    uint32_t x = 0;
#ifdef A3D_FILL_NEON
    x = a3d_fill_row_neon(s, d, w, &k);
#endif
    if (x < w)
        a3d_fill_row_c(s, d, x, w, &k);
}

typedef struct a3d_tcache
{
    uint32_t valid, epoch;
    uint32_t w[7];
    uint32_t guest, bprow, width, height, format;   /* the c->tgt the words were found the same as */
} a3d_tcache;
static a3d_tcache g_tcache[A3D_MAX_CTX] A3D_R8_BSS;

static void a3d_tgt_words(const a3d_ctx *c, uint32_t w[7])
{
    const uint8_t *p = a3d_g_ptr(c->gpriv);
    for (uint32_t i = 0; i < 7u; i++)
        w[i] = a3d_be32(p + A3D_P_TBASE + 4u * i);     /* TBASE, TBPROW, TWIDTH, THEIGHT, TFORMAT, TYOFFSET, TFLAGS */
}

int a3d_tgt_cached(const a3d_ctx *c)
{
    const a3d_tcache *t = &g_tcache[a3d_ctx_index(c)];
    if (a3d_state_full_on || !t->valid || t->epoch != a3d_state_epoch
        || !a3d_g_ok(c->gpriv, A3D_PRIV_SIZE, A3D_GR_READ))
        return 0;
    uint32_t w[7], same = c->tgt_valid && c->tgt.guest == t->guest && c->tgt.bprow == t->bprow
                          && c->tgt.width == t->width && c->tgt.height == t->height && c->tgt.format == t->format;
    a3d_tgt_words(c, w);
    for (uint32_t i = 0; i < 7u; i++)
        same &= w[i] == t->w[i];
    return (int)same;
}

void a3d_tgt_cache(const a3d_ctx *c)
{
    a3d_tcache *t = &g_tcache[a3d_ctx_index(c)];
    a3d_tgt_words(c, t->w);
    t->guest = c->tgt.guest;
    t->bprow = c->tgt.bprow;
    t->width = c->tgt.width;
    t->height = c->tgt.height;
    t->format = c->tgt.format;
    t->epoch = a3d_state_epoch;
    t->valid = 1;
}

static uint32_t g_pend_put[A3D_MAX_CTX] A3D_R8_BSS;  /* the PENDING word last written, + 1 (0: unknown)       */
static uint32_t g_pend_epoch A3D_R8_BSS;              /* a3d_state_epoch then (a context made or gone since)  */
uint32_t a3d_poll_dirty A3D_R8_BSS;              /* a texture freed / retired: the next poll walks them  */

/* a full poll (a3d_poll) wrote every context's PENDING word: the quick ones compare with these */
void a3d_poll_noted(void)
{
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        const a3d_ctx *c = &a3d_ctxs[i];
        g_pend_put[i] = c->used ? 1u + ((c->fence_sub > c->fence_done) || (a3d_P.open && a3d_P.ctx == i)) : 0u;
    }
    g_pend_epoch = a3d_state_epoch;
}

void a3d_poll_quick(void)
{
    a3d_r8cnt.polls_quick++;
    if (g_pend_epoch != a3d_state_epoch)
    {
        for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
            g_pend_put[i] = 0;
        g_pend_epoch = a3d_state_epoch;
    }
    for (uint32_t i = 0; i < A3D_MAX_CTX; i++)
    {
        a3d_ctx *c = &a3d_ctxs[i];
        if (!c->used)
            continue;
        const uint32_t pend = (c->fence_sub > c->fence_done) || (a3d_P.open && a3d_P.ctx == i);
        if (g_pend_put[i] != pend + 1u)
        {
            a3d_priv_put(c, A3D_P_PENDING, pend);
            g_pend_put[i] = pend + 1u;
        }
    }
}
