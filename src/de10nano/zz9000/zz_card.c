/*
    Copyright (C) 2019-2026, Lucie L. Hartmann, MNT Research GmbH (ZZ9000OS)
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: the card model (register writes, 2D operations, sprite, Ethernet), after MNT's ZZ9000OS. */

#include <string.h>
#include <stddef.h>
#include "zz_card.h"

extern void arm_flush_cache(uintptr_t addr, uint32_t length);
static void zz_flush_rect_b(struct zz_card *c, uint32_t fb_off, uint32_t pitch_b,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t bpp);
#include "gfx.h"
#include "zz_queue.h"

_Static_assert(offsetof(struct zz_gfxdata, rgb)      ==  8, "gfxdata rgb");
_Static_assert(offsetof(struct zz_gfxdata, x)        == 16, "gfxdata x");
_Static_assert(offsetof(struct zz_gfxdata, user)     == 32, "gfxdata user");
_Static_assert(offsetof(struct zz_gfxdata, pitch)    == 40, "gfxdata pitch");
_Static_assert(offsetof(struct zz_gfxdata, u8_user)  == 48, "gfxdata u8");
_Static_assert(offsetof(struct zz_gfxdata, op)       == 56, "gfxdata op");
_Static_assert(offsetof(struct zz_gfxdata, minterm)  == 58, "gfxdata minterm");
_Static_assert(offsetof(struct zz_gfxdata, u32_user) == 60, "gfxdata u32");
_Static_assert(offsetof(struct zz_gfxdata, clut1)    == 92, "gfxdata clut1");
_Static_assert(sizeof(struct zz_gfxdata) == 3164,           "gfxdata size");

/* ---- 1. helpers ---------------------------------------------------------- */

static inline uint16_t zz_rd_be16(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)((b[0] << 8) | b[1]);
}
static inline uint32_t zz_rd_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}
static inline uint32_t zz_rd_le32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline void zz_wr_be32(void *p, uint32_t v)
{
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);  b[3] = (uint8_t)v;
}
/* m68k pen ULONG -> the engine's expected register value on THIS host */
static inline uint32_t zz_pen_val(uint32_t m68k, unsigned cm)
{
#ifdef __ARMEB__
    return (cm == ZZ_CM_8BIT) ? (m68k << 24) : m68k;
#else
    return __builtin_bswap32(m68k);
#endif
}
#define GDB16(f) zz_rd_be16(&(f))
#define GDB32(f) zz_rd_be32(&(f))

static uint32_t zz_bpp(uint32_t cm)
{
    switch (cm) {
    case ZZ_CM_8BIT:  return 1;
    case ZZ_CM_16BIT: case ZZ_CM_15BIT: return 2;
    case ZZ_CM_32BIT: return 4;
    default:          return 0;
    }
}

static int zz_rect_ok(struct zz_card *c, uint32_t membase_off,
                      uint32_t pitch_bytes, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t bpp)
{
    if (!w || !h || !bpp)
        return 0;
    uint64_t start = (uint64_t)ZZ_MEMBASE_OFF + membase_off;
    uint64_t end   = start + (uint64_t)(y + h - 1) * pitch_bytes
                           + ((uint64_t)x + w) * bpp;
    return start < c->backing && end <= c->backing;
}

/* Linear source blob (templates, planar data, YUV): off..off+len must fit. */
static int zz_blob_ok(struct zz_card *c, uint32_t membase_off, uint64_t len)
{
    uint64_t start = (uint64_t)ZZ_MEMBASE_OFF + membase_off;
    return start < c->backing && start + len <= c->backing;
}

static uint8_t *zz_mem(struct zz_card *c, uint32_t membase_off)
{
    return c->vram + ZZ_MEMBASE_OFF + membase_off;
}

static void zz_engine_fb(struct zz_card *c, uint32_t membase_off,
                         uint32_t pitch)
{
    set_fb((uint32_t *)(void *)zz_mem(c, membase_off), pitch);
}

/* ---- 2. mode table (ZZ9000OS video.c:28-49, dims only) ------------------- */

static const struct { uint16_t w, h; } zz_modes[] = {
    { 1280,  720 }, {  800,  600 }, {  640,  480 }, { 1024,  768 },
    { 1280, 1024 }, { 1920, 1080 }, {  720,  576 }, { 1920, 1080 },
    {  720,  480 }, {  640,  512 }, { 1600, 1200 }, { 2560, 1440 },
    {  720,  576 }, {  720,  480 }, {  720,  576 }, {  720,  480 },
    {  640,  400 }, { 1920,  800 }, { 1280,  720 },
};
#define ZZ_MODE_COUNT (sizeof(zz_modes) / sizeof(zz_modes[0]))

/* ---- 3. sprite ----------------------------------------------------------- */

static void zz_sprite_decode(struct zz_card *c, uint32_t src_off,
                             uint32_t width_field, uint32_t height_field)
{
    struct zz_sprite *s = &c->sprite;
    int doubled = (width_field  & 0x100) != 0;
    int hires   = (height_field & 0x100) != 0;
    int width   = (int)(width_field  & 0xff); if (width  > 32) width  = 32;
    int height  = (int)(height_field & 0xff); if (height > 48) height = 48;
    memset(s->pixels, 0, sizeof(s->pixels));
    int wf = (int)(width_field & 0xff); if (wf < 8) wf = 8;
    int full_pitch = (wf / 8) * 2;
    int src_pitch  = (doubled && !hires) ? full_pitch / 2 : full_pitch;
    int scale      = doubled ? 2 : 1;
    int src_w      = doubled ? width / 2 : width;
    int src_h      = doubled ? height / 2 : height;

    if (!zz_blob_ok(c, src_off, (uint64_t)(src_h ? src_h : 1) * src_pitch))
        return;

    for (int row = 0; row < src_h; row++) {
        const uint8_t *p0 = zz_mem(c, src_off + row * src_pitch);
        const uint8_t *p1 = p0 + src_pitch / 2;
        for (int col = 0; col < src_w; col++) {
            int bit = 0x80 >> (col & 7);
            uint8_t pen = (uint8_t)(((p0[col / 8] & bit) ? 1 : 0) |
                                    ((p1[col / 8] & bit) ? 2 : 0));
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) {
                    int ox = col * scale + sx, oy = row * scale + sy;
                    if (ox < 32 && oy < 48)
                        s->pixels[oy * 32 + ox] = pen;
                }
        }
    }
    s->pix_w = (uint8_t)((src_w * scale > 32) ? 32 : src_w * scale);
    s->pix_h = (uint8_t)((src_h * scale > 48) ? 48 : src_h * scale);
    s->doubled = (uint8_t)doubled;
    s->hires   = (uint8_t)hires;
    s->hw_gen++;                 /* image changed: route-A re-pushes overlay */
}

/* Write one native big-endian pixel (amiberry zz_write_native_pixel). */
static void zz_native_pixel(uint8_t *p, uint32_t cm, int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    switch (cm) {
    case ZZ_CM_32BIT: p[0] = (uint8_t)b; p[1] = (uint8_t)g;
                      p[2] = (uint8_t)r; p[3] = 0xff; break;
    case ZZ_CM_16BIT: {
        uint16_t v = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
        p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; break;
    }
    case ZZ_CM_15BIT: {
        uint16_t v = (uint16_t)(((r & 0xf8) << 7) | ((g & 0xf8) << 2) | (b >> 3));
        p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; break;
    }
    default: break;
    }
}

static uint8_t zz_nearest_pen(struct zz_card *c, int r, int g, int b)
{
    uint32_t best = 0xffffffffu; uint8_t best_i = 0;
    for (int i = 0; i < 256; i++) {
        int dr = r - c->palette.rgb[i][0], dg = g - c->palette.rgb[i][1],
            db = b - c->palette.rgb[i][2];
        uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
        if (d < best) { best = d; best_i = (uint8_t)i; }
    }
    return best_i;
}

static void zz_sprite_undraw(struct zz_card *c)
{
    struct zz_sprite *s = &c->sprite;
    if (!s->drawn)
        return;
    uint32_t bpp = zz_bpp(s->drawn_cm);
    for (uint32_t row = 0; row < s->drawn_h; row++) {
        uint8_t *dst = c->vram + s->drawn_fb_off +
                       (uint64_t)(s->drawn_y + row) * s->drawn_pitch +
                       (uint64_t)s->drawn_x * bpp;
        memcpy(dst, s->under + row * s->drawn_w * bpp, s->drawn_w * bpp);
    }
    zz_flush_rect_b(c, s->drawn_fb_off, s->drawn_pitch, s->drawn_x, s->drawn_y,
                    s->drawn_w, s->drawn_h, bpp);
    s->drawn = 0;
}

extern volatile uint32_t g_zz_hw_sprite;

static void zz_sprite_draw(struct zz_card *c)
{
    struct zz_sprite *s = &c->sprite;
    struct zz_scanout *so = &c->scanout;
    if (g_zz_hw_sprite || !c->composite_sprite || !s->showing || !s->pix_w || !s->pix_h)
        return;
    if (!so->enabled || !so->width || !so->height)
        return;
    uint32_t bpp = zz_bpp(so->colormode);
    if (!bpp)
        return;
    /* screen position: pan-offset basis, +1 (amiberry zz_composite_sprite_row) */
    int sx = s->x - s->x_off + 1, sy = s->y - s->y_off + 1;
    int x0 = sx < 0 ? 0 : sx, y0 = sy < 0 ? 0 : sy;
    int x1 = sx + s->pix_w, y1 = sy + s->pix_h;
    if (x1 > (int)so->width)  x1 = so->width;
    if (y1 > (int)so->height) y1 = so->height;
    if (x0 >= x1 || y0 >= y1)
        return;
    uint32_t w = (uint32_t)(x1 - x0), h = (uint32_t)(y1 - y0);
    if (w * bpp > sizeof(s->under) / 48)
        return;                                   /* cannot happen: 32*2*4 max */
    /* bound the touched rect against the backing */
    uint64_t last = (uint64_t)so->fb_off + (uint64_t)(y1 - 1) * so->pitch_bytes
                    + (uint64_t)x1 * bpp;
    if (so->fb_off >= c->backing || last > c->backing)
        return;

    for (uint32_t row = 0; row < h; row++) {
        uint8_t *dst = c->vram + so->fb_off +
                       (uint64_t)(y0 + row) * so->pitch_bytes +
                       (uint64_t)x0 * bpp;
        memcpy(s->under + row * w * bpp, dst, w * bpp);   /* save under */
        for (uint32_t col = 0; col < w; col++) {
            int src_x = (int)(x0 + col) - sx, src_y = (int)(y0 + row) - sy;
            uint8_t pen = s->pixels[src_y * 32 + src_x];
            if (!pen)
                continue;
            uint32_t colr = s->colors[pen & 3];
            int r = (colr >> 16) & 0xff, g = (colr >> 8) & 0xff, b = colr & 0xff;
            uint8_t *px = dst + col * bpp;
            if (so->colormode == ZZ_CM_8BIT)
                *px = zz_nearest_pen(c, r, g, b);
            else
                zz_native_pixel(px, so->colormode, r, g, b);
        }
    }
    zz_flush_rect_b(c, so->fb_off, so->pitch_bytes, (uint32_t)x0, (uint32_t)y0,
                    w, h, bpp);
    s->drawn = 1;
    s->drawn_x = (uint16_t)x0; s->drawn_y = (uint16_t)y0;
    s->drawn_w = (uint16_t)w;  s->drawn_h = (uint16_t)h;
    s->drawn_fb_off = so->fb_off; s->drawn_pitch = so->pitch_bytes;
    s->drawn_cm = so->colormode;
}

void zz_sprite_refresh(struct zz_card *c)
{
    zz_sprite_undraw(c);
    zz_sprite_draw(c);
}

/* ---- 4. DMA_OP dispatch (port of ZZ9000OS dma_rtg.c) --------------------- */

static uint32_t zz_classic_mt(unsigned mt, uint32_t s, uint32_t d)
{
    switch (mt & 15) {
    case 0:  return 0;
    case 1:  return ~(s | d);
    case 2:  return ~s & d;
    case 3:  return ~s;
    case 4:  return s & ~d;
    case 5:  return ~d;
    case 6:  return s ^ d;
    case 7:  return ~(s & d);
    case 8:  return s & d;
    case 9:  return ~(s ^ d);
    case 10: return d;
    case 11: return ~s | d;
    case 12: return s;
    case 13: return s | ~d;
    case 14: return s | d;
    default: return ~0u;
    }
}

static int zz_engine_mt_ok(unsigned mt)
{
    switch (mt & 15) {
    case 2: case 5: case 6: case 9: case 10: case 12: case 14:
        return 1;
    default:
        return 0;
    }
}

static void zz_classic_copy(struct zz_card *c, uint32_t dst_off,
                            uint32_t dpitch_b, uint32_t src_off,
                            uint32_t spitch_b, unsigned cm,
                            uint16_t dx, uint16_t dy, uint16_t sx,
                            uint16_t sy, uint16_t w, uint16_t h, unsigned mt)
{
    uint32_t bpp = zz_bpp(cm);
    int rev = (dst_off == src_off) &&
              (dy > sy || (dy == sy && dx > sx));
#define ZZ_CLASSIC_LOOP(T)                                                    \
    for (uint32_t i = 0; i < h; i++) {                                        \
        uint32_t row = rev ? (uint32_t)(h - 1 - i) : i;                       \
        for (uint32_t j = 0; j < w; j++) {                                    \
            uint32_t col = rev ? (uint32_t)(w - 1 - j) : j;                   \
            const uint8_t *sp = zz_mem(c, src_off) +                          \
                (uint64_t)(sy + row) * spitch_b +                             \
                (uint64_t)(sx + col) * sizeof(T);                             \
            uint8_t *dp = zz_mem(c, dst_off) +                                \
                (uint64_t)(dy + row) * dpitch_b +                             \
                (uint64_t)(dx + col) * sizeof(T);                             \
            T s, d;                                                           \
            __builtin_memcpy(&s, sp, sizeof(T));                              \
            __builtin_memcpy(&d, dp, sizeof(T));                              \
            const T r = (T)zz_classic_mt(mt, s, d);                           \
            __builtin_memcpy(dp, &r, sizeof(T));                              \
        }                                                                     \
    }
    if (bpp == 1)
        ZZ_CLASSIC_LOOP(uint8_t)
    else if (bpp == 2)
        ZZ_CLASSIC_LOOP(uint16_t)
    else if (bpp == 4)
        ZZ_CLASSIC_LOOP(uint32_t)
#undef ZZ_CLASSIC_LOOP
}

static void zz_scanout_update(struct zz_card *c)
{
    struct zz_scanout *so = &c->scanout;
#ifdef EMU68_A3D
    if (c->a3d_fence)
    {
        c->a3d_fence(c->ud, ZZ_MEMBASE_OFF + c->pan_off);
    }
#endif
    so->fb_off      = ZZ_MEMBASE_OFF + c->pan_off;
    so->pitch_bytes = c->pan_width << (c->scanout.colormode == ZZ_CM_15BIT
                                       ? 1 : c->scanout.colormode);
    so->dirty = 1;
}

#define ZZ_BLITQ_ANNOUNCE_REG 0x62u
#define ZZ_BLITQ_ARM_MAGIC    0x42D9u   /* 'B' + D9, the design this finishes */

static struct zz_queue zz_blitq;
#ifdef EMU68_A3D
static uint8_t zz_blitq_in_gate;
static uint8_t zz_blitq_covered;
#define ZZ_TMPL_SNAP_BYTES 4096u
static uint8_t zz_tmpl_snap[ZZ_QUEUE_DEPTH][ZZ_TMPL_SNAP_BYTES];
static uint32_t zz_bq_covers, zz_bq_cover_refused, zz_bq_tmpl;
#endif

/* The snapshot view: first ZZ_QOP_PAYLOAD_BYTES of GFXData, same layout. */
struct zz_gfx_hdr {
    uint32_t offset[2];
    uint32_t rgb[2];
    uint16_t x[4], y[4];
    uint16_t user[4];
    uint16_t pitch[4];
    uint8_t  u8_user[8];
    uint8_t  op, mask, minterm, u8offset;
};
_Static_assert(sizeof(struct zz_gfx_hdr) == ZZ_QOP_PAYLOAD_BYTES, "hdr size");
_Static_assert(offsetof(struct zz_gfx_hdr, pitch) ==
               offsetof(struct zz_gfxdata, pitch), "hdr pitch");
_Static_assert(offsetof(struct zz_gfx_hdr, u8offset) ==
               offsetof(struct zz_gfxdata, u8offset), "hdr u8offset");

static void zz_rect_span(uint32_t membase_off, uint32_t pitch_b, uint32_t x,
                         uint32_t y, uint32_t w, uint32_t h, uint32_t bpp,
                         uint64_t *lo, uint64_t *hi)
{
    uint64_t base = (uint64_t)ZZ_MEMBASE_OFF + membase_off;
    *lo = base + (uint64_t)y * pitch_b + (uint64_t)x * bpp;
    *hi = base + (uint64_t)(y + h - 1) * pitch_b + ((uint64_t)x + w) * bpp;
}

#ifdef EMU68_A3D
static void zz_blitq_settle(struct zz_card *c)
{
    if (zz_blitq_covered && !zz_queue_pending(&zz_blitq)) {
        zz_blitq_covered = 0;
        if (c->a3d_uncover)
            c->a3d_uncover(c->ud);
    }
}

static int zz_qop_ranges(const struct zz_qop *qop, uint32_t lo[2], uint32_t hi[2])
{
    const struct zz_gfx_hdr *d = (const struct zz_gfx_hdr *)(const void *)qop->payload;
    const uint32_t bpp = zz_bpp(d->u8_user[0]);
    uint64_t l, h;
    int n = 0;
    switch (qop->trig) {
    case 2: case 9: case 3: case 4:
        zz_rect_span(GDB32(d->offset[0]), (uint32_t)GDB16(d->pitch[0]) * 4u, GDB16(d->x[0]), GDB16(d->y[0]),
                     GDB16(d->x[1]), qop->rows_total, bpp, &l, &h);
        lo[n] = (uint32_t)l; hi[n] = (uint32_t)h; n++;
        if (qop->trig == 3 || qop->trig == 4) {
            const uint32_t soff = qop->trig == 3 ? GDB32(d->offset[0]) : GDB32(d->offset[1]);
            const uint32_t sp = (uint32_t)(qop->trig == 3 ? GDB16(d->pitch[0]) : GDB16(d->pitch[1])) * 4u;
            zz_rect_span(soff, sp, GDB16(d->x[2]), GDB16(d->y[2]), GDB16(d->x[1]), qop->rows_total, bpp, &l, &h);
            lo[n] = (uint32_t)l; hi[n] = (uint32_t)h; n++;
        }
        return n;
    case 5: case 6:     /* the destination pitch is BYTES for these two; the template is a snapshot */
        zz_rect_span(GDB32(d->offset[0]), GDB16(d->pitch[0]), GDB16(d->x[0]), GDB16(d->y[0]), GDB16(d->x[1]),
                     qop->rows_total, bpp, &l, &h);
        lo[n] = (uint32_t)l; hi[n] = (uint32_t)h; n++;
        return n;
    case 16:
        return 0;
    default:
        return -1;
    }
}

#define ZZ_COVER_MAX 4u     /* the protection's queue keys (agent3d/a3d_fw_prot.c AP_QKEYS) */

static void zz_pan_shows(const struct zz_card *c, const struct zz_qop *qop, uint32_t *lo, uint32_t *hi)
{
    const struct zz_gfx_hdr *d = (const struct zz_gfx_hdr *)(const void *)qop->payload;
    const uint32_t width = GDB16(d->x[1]);
    const uint32_t off = GDB32(d->offset[0]) + (uint32_t)(((int16_t)GDB16(d->x[0])) << d->u8_user[0])
                       + (uint32_t)(((int16_t)GDB16(d->y[0])) * (int32_t)(width << d->u8_user[0]));
    const uint32_t pitch = width << (c->scanout.colormode == ZZ_CM_15BIT ? 1 : c->scanout.colormode);
    *lo = ZZ_MEMBASE_OFF + off;
    *hi = *lo + pitch * c->scanout.height;
}

static void zz_scanout_shows(const struct zz_card *c, uint32_t *lo, uint32_t *hi)
{
    *lo = c->scanout.fb_off;
    *hi = c->scanout.fb_off + c->scanout.pitch_bytes * c->scanout.height;
}

static int zz_blitq_try_cover(struct zz_card *c)
{
    if (!c->a3d_cover || !c->a3d_wb_early || !(zz_blitq_covered || zz_queue_head_gated(&zz_blitq))
        || !c->a3d_wb_early(c->ud))
        return 0;
    uint32_t lo[2u * ZZ_QUEUE_DEPTH], hi[2u * ZZ_QUEUE_DEPTH], fence = 0;
    unsigned n = 0;
    uint32_t disp_lo, disp_hi;
    zz_scanout_shows(c, &disp_lo, &disp_hi);
    for (uint32_t i = 0; i < zz_blitq.count; i++) {
        const struct zz_qop *op = zz_queue_op(&zz_blitq, i);
        if (op->trig == 10) {
            if (disp_hi <= disp_lo)
                return 0;                       /* no display to cover (no mode yet): the flip waits as before */
            lo[n] = disp_lo;
            hi[n] = disp_hi;
            n++;
            zz_pan_shows(c, op, &disp_lo, &disp_hi);
        } else {
            const int k = zz_qop_ranges(op, lo + n, hi + n);
            if (k < 0)
                return 0;
            n += (unsigned)k;
        }
        if (op->rows_done == 0 && op->dep[0] != ZZ_QDEP_NONE && op->dep[0] > fence)
            fence = op->dep[0];
    }
    for (unsigned i = 1; i < n; i++)
        for (unsigned j = i; j > 0 && lo[j] < lo[j - 1]; j--) {
            const uint32_t tl = lo[j], th = hi[j];
            lo[j] = lo[j - 1]; hi[j] = hi[j - 1];
            lo[j - 1] = tl; hi[j - 1] = th;
        }
    unsigned m = 0;
    for (unsigned i = 0; i < n; i++) {
        if (m && lo[i] <= ((hi[m - 1] + 0xFFFu) & ~0xFFFu)) {
            if (hi[i] > hi[m - 1])
                hi[m - 1] = hi[i];
        } else {
            lo[m] = lo[i];
            hi[m] = hi[i];
            m++;
        }
    }
    while (m > ZZ_COVER_MAX) {
        unsigned best = 0;
        for (unsigned i = 1; i + 1 < m; i++)
            if (lo[i + 1] - hi[i] < lo[best + 1] - hi[best])
                best = i;
        if (hi[best + 1] > hi[best])
            hi[best] = hi[best + 1];
        for (unsigned i = best + 1; i + 1 < m; i++) {
            lo[i] = lo[i + 1];
            hi[i] = hi[i + 1];
        }
        m--;
    }
    if (fence && c->a3d_fence_kick) {
        zz_blitq_in_gate = 1;
        const int done = c->a3d_fence_kick(c->ud, fence);
        zz_blitq_in_gate = 0;
        if (done)
            return 0;
    }
    if (m == 0 || !c->a3d_cover(c->ud, lo, hi, m, fence)) {
        zz_bq_cover_refused++;
        return 0;
    }
    zz_blitq_covered = 1;
    zz_bq_covers++;
    return 1;
}
#endif

#if   defined(__arm__) && !defined(__aarch64__)
#define ZZ_FLUSH_ALL_BYTES  (256u << 10)
static void zz_nc_barrier(void) { __asm__ volatile("dsb" ::: "memory"); }
static void zz_dcache_flush_all(void)
{
    uint32_t ccsidr;
    __asm__ volatile("mcr p15, 2, %0, c0, c0, 0\n\tisb" :: "r"(0u) : "memory");
    __asm__ volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));                     /* CCSIDR                    */
    const uint32_t ways = (ccsidr >> 3) & 0x3FFu, sets = (ccsidr >> 13) & 0x7FFFu;
    const uint32_t la = ways ? (uint32_t)__builtin_clz(ways) : 0u, lb = (ccsidr & 7u) + 4u;
    __asm__ volatile("dsb" ::: "memory");
    for (uint32_t wy = 0; wy <= ways; wy++)
        for (uint32_t st = 0; st <= sets; st++)
            __asm__ volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((wy << la) | (st << lb)) : "memory");  /* DCCISW */
    __asm__ volatile("dsb" ::: "memory");
}
#endif

static void zz_flush_rect_b(struct zz_card *c, uint32_t fb_off, uint32_t pitch_b,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t bpp)
{
    if (!w || !h || !bpp)
        return;
    uint64_t off = (uint64_t)fb_off + (uint64_t)y * pitch_b + (uint64_t)x * bpp;
    uint64_t end = off + (uint64_t)(h - 1) * pitch_b + (uint64_t)w * bpp;
    if (off >= (uint64_t)c->backing)
        return;
    if (end > (uint64_t)c->backing)
        end = c->backing;
    if (end <= off)
        return;
#ifdef ZZ_FLUSH_ALL_BYTES
    if (end - off >= ZZ_FLUSH_ALL_BYTES)
    {
        zz_dcache_flush_all();
    }
    else
#endif
        arm_flush_cache((uintptr_t)c->vram + off, (uint32_t)(end - off));
}

static void zz_card_blitq_exec(void *ud, const struct zz_qop *qop,
                               uint32_t row0, uint32_t nrows)
{
    struct zz_card *c = ud;
    const struct zz_gfx_hdr *d =
        (const struct zz_gfx_hdr *)(const void *)qop->payload;
    unsigned cm = d->u8_user[0];

    if (row0 == 0)
        zz_sprite_undraw(c);

    switch (qop->trig) {
    case 2: { /* OP_FILLRECT */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]);
        uint16_t y0 = (uint16_t)(GDB16(d->y[0]) + row0);
        uint32_t pen = zz_pen_val(GDB32(d->rgb[0]), cm);
        zz_engine_fb(c, GDB32(d->offset[0]), GDB16(d->pitch[0]));
        if (d->mask == 0xFF)
            fill_rect_solid(x0, y0, w, (uint16_t)nrows, pen, cm);
        else
            fill_rect(x0, y0, w, (uint16_t)nrows, pen, cm, d->mask);
        break;
    }
    case 9: { /* OP_INVERTRECT */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]);
        uint16_t y0 = (uint16_t)(GDB16(d->y[0]) + row0);
        zz_engine_fb(c, GDB32(d->offset[0]), GDB16(d->pitch[0]));
        invert_rect(x0, y0, w, (uint16_t)nrows, d->mask, cm);
        break;
    }
    case 3: {
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), sx = GDB16(d->x[2]);
        uint16_t y0 = (uint16_t)(GDB16(d->y[0]) + row0);
        uint16_t sy = (uint16_t)(GDB16(d->y[2]) + row0);
        uint16_t dpitch = GDB16(d->pitch[0]);
        uint32_t doff = GDB32(d->offset[0]);
        zz_engine_fb(c, doff, dpitch);
        if (d->mask == 0xFF || (d->mask != 0xFF && cm != ZZ_CM_8BIT))
            copy_rect_nomask(x0, y0, w, (uint16_t)nrows, sx, sy, cm,
                             (uint32_t *)(void *)zz_mem(c, doff),
                             dpitch, MINTERM_SRC);
        else
            copy_rect(x0, y0, w, (uint16_t)nrows, sx, sy, cm,
                      (uint32_t *)(void *)zz_mem(c, doff),
                      dpitch, d->mask);
        break;
    }
    case 4: {
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), sx = GDB16(d->x[2]);
        uint16_t y0 = (uint16_t)(GDB16(d->y[0]) + row0);
        uint16_t sy = (uint16_t)(GDB16(d->y[2]) + row0);
        uint16_t dpitch = GDB16(d->pitch[0]), spitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), soff = GDB32(d->offset[1]);
        if (zz_engine_mt_ok(d->minterm)) {
#ifdef ZZ_FLUSH_ALL_BYTES
            uint64_t s_lo, s_hi;
            zz_rect_span(doff, (uint32_t)dpitch * 4u, x0, GDB16(d->y[0]), w, qop->rows_total, zz_bpp(cm), &s_lo, &s_hi);
            if (c->vram_nc && d->minterm == MINTERM_SRC && s_hi - s_lo >= ZZ_FLUSH_ALL_BYTES) {
                if (row0 == 0) {
                    zz_dcache_flush_all();
                }
                set_fb_limit(c->vram_nc + c->backing);
                set_fb((uint32_t *)(void *)(c->vram_nc + ZZ_MEMBASE_OFF + doff), dpitch);
                copy_rect_nomask(x0, y0, w, (uint16_t)nrows, sx, sy, cm,
                                 (uint32_t *)(void *)zz_mem(c, soff),
                                 spitch, d->minterm);
                set_fb_limit(c->vram + c->backing);
                zz_nc_barrier();                        /* the non-cacheable stores have reached memory */
                break;
            }
#endif
            zz_engine_fb(c, doff, dpitch);
            copy_rect_nomask(x0, y0, w, (uint16_t)nrows, sx, sy, cm,
                             (uint32_t *)(void *)zz_mem(c, soff),
                             spitch, d->minterm);
        } else {
            zz_classic_copy(c, doff, (uint32_t)dpitch * 4,
                            soff, (uint32_t)spitch * 4,
                            cm, x0, y0, sx, sy, w, (uint16_t)nrows,
                            d->minterm);
        }
        break;
    }
#ifdef EMU68_A3D
    case 5:
    case 6: {
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), xo = GDB16(d->x[2]);
        uint16_t y0 = GDB16(d->y[0]), h = GDB16(d->y[1]), yo = GDB16(d->y[2]);
        uint16_t dpitch = GDB16(d->pitch[0]), tpitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]);
        uint32_t fg = zz_pen_val(GDB32(d->rgb[0]), cm);
        uint32_t bg = zz_pen_val(GDB32(d->rgb[1]), cm);
        uint8_t *tmpl = zz_tmpl_snap[qop - zz_blitq.ops];
        (void)row0;
        (void)nrows;
        zz_engine_fb(c, doff, dpitch);          /* dst pitch BYTES for these two ops, as zz_dma_op */
        if (qop->trig == 6)
            pattern_fill_rect(cm, x0, y0, w, h, d->u8_user[1], d->mask,
                              fg, bg, xo, yo, tmpl, 16, GDB16(d->user[0]));
        else
            template_fill_rect(cm, x0, y0, w, h, d->u8_user[1], d->mask,
                               fg, bg, xo, yo, tmpl, tpitch);
        break;
    }
#endif
    case 10: {
        uint32_t off = GDB32(d->offset[0]);
        uint16_t xo = GDB16(d->x[0]), yo = GDB16(d->y[0]);
        uint16_t width = GDB16(d->x[1]);
        c->sprite.x_off = (int16_t)xo;
        c->sprite.y_off = (int16_t)yo;
        c->pan_width = width;
        c->pan_off = off
                   + (uint32_t)(((int16_t)xo) << d->u8_user[0])
                   + (uint32_t)(((int16_t)yo) *
                                (int32_t)(c->pan_width << d->u8_user[0]));
        zz_scanout_update(c);
        break;
    }
    case 16:
        c->bgbuf_off = GDB32(d->offset[0]);
        c->split_pos = GDB16(d->y[0]);
        break;
    default: /* cannot happen: capture only queues the cases above */
        break;
    }

    if (row0 + nrows >= qop->rows_total) {
        switch (qop->trig) {
        case 2: case 3: case 4: case 9:
            zz_flush_rect_b(c, ZZ_MEMBASE_OFF + GDB32(d->offset[0]),
                            (uint32_t)GDB16(d->pitch[0]) * 4u,
                            GDB16(d->x[0]), GDB16(d->y[0]), GDB16(d->x[1]),
                            qop->rows_total, zz_bpp(cm));
            break;
        default:
            break;
        }
        zz_sprite_draw(c);
    }
}

static int zz_blitq_capture(struct zz_card *c, uint16_t op, uint32_t dep0, uint32_t dep1)
{
    struct zz_gfxdata *d = (struct zz_gfxdata *)(void *)
        (c->vram + ZZ_GFXDATA_OFF);
    uint32_t row_cost;
    uint16_t h;

    if (ZZ_GFXDATA_OFF + sizeof(*d) > c->backing)
        return 0;

    switch (op) {
    case 2:   /* OP_FILLRECT */
    case 9: { /* OP_INVERTRECT */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]);
        uint16_t y0 = GDB16(d->y[0]);
        uint32_t bpp = zz_bpp(d->u8_user[0]);
        h = GDB16(d->y[1]);
        if (!zz_rect_ok(c, GDB32(d->offset[0]),
                        (uint32_t)GDB16(d->pitch[0]) * 4, x0, y0, w, h, bpp))
            return 0;
        row_cost = (uint32_t)w * bpp;
        break;
    }
    case 3:   /* OP_COPYRECT - src surface IS the dst surface */
    case 4: { /* OP_COPYRECT_NOMASK */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), sx = GDB16(d->x[2]);
        uint16_t y0 = GDB16(d->y[0]), sy = GDB16(d->y[2]);
        uint16_t dpitch = GDB16(d->pitch[0]), spitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), soff = GDB32(d->offset[1]);
        uint32_t bpp = zz_bpp(d->u8_user[0]);
        uint64_t d_lo, d_hi, s_lo, s_hi;
        h = GDB16(d->y[1]);
        if (op == 3) {
            soff = doff;
            spitch = dpitch;
        }
        if (!zz_rect_ok(c, doff, (uint32_t)dpitch * 4, x0, y0, w, h, bpp) ||
            !zz_rect_ok(c, soff, (uint32_t)spitch * 4, sx, sy, w, h, bpp))
            return 0;
        zz_rect_span(doff, (uint32_t)dpitch * 4, x0, y0, w, h, bpp,
                     &d_lo, &d_hi);
        zz_rect_span(soff, (uint32_t)spitch * 4, sx, sy, w, h, bpp,
                     &s_lo, &s_hi);
        if (d_lo < s_hi && s_lo < d_hi)
            return 0;
        row_cost = 2u * (uint32_t)w * bpp;      /* src read + dst write */
        break;
    }
#ifdef EMU68_A3D
    case 5:   /* OP_RECT_TEMPLATE */
    case 6: { /* OP_RECT_PATTERN */
        if (!(zz_blitq_covered || (zz_queue_head_gated(&zz_blitq) && c->a3d_wb_early && c->a3d_wb_early(c->ud))))
            return 0;
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), xo = GDB16(d->x[2]);
        uint16_t y0 = GDB16(d->y[0]), tpitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), toff = GDB32(d->offset[1]);
        uint64_t bytes;
        h = GDB16(d->y[1]);
        if (!h || !zz_rect_ok(c, doff, GDB16(d->pitch[0]), x0, y0, w, h, zz_bpp(d->u8_user[0])))
            return 0;
        if (op == 6) {
            const uint16_t loops = GDB16(d->user[0]);
            bytes = (uint64_t)16 * loops;
            if (!loops)
                return 0;
        } else {
            const uint64_t rows = tpitch ? (uint64_t)h : 1;
            bytes = (rows - 1) * tpitch + ((uint64_t)xo + w + 7) / 8 + 2;
        }
        if (bytes > ZZ_TMPL_SNAP_BYTES || !zz_blob_ok(c, toff, bytes))
            return 0;
        zz_queue_push_dep(&zz_blitq, op, d, h, h, dep0, dep1);
        /* the op's slot: the newest (a full queue finished its oldest inside the push, so this is free) */
        memcpy(zz_tmpl_snap[(zz_blitq.head + zz_blitq.count - 1u) % ZZ_QUEUE_DEPTH], zz_mem(c, toff),
               (uint32_t)bytes);
        zz_bq_tmpl++;
        return 1;
    }
#endif
    case 10:
        zz_queue_push_dep(&zz_blitq, op, d, 1, 1, dep0, dep1);
        return 1;

    case 16:
        zz_queue_push_dep(&zz_blitq, op, d, 1, 1, dep0, dep1);
        return 1;

    default:
        return 0;   /* everything else keeps today's synchronous dispatch */
    }

    zz_queue_push_dep(&zz_blitq, op, d, h, ZZ_BLITQ_SLICE_BYTES / row_cost, dep0, dep1);
    return 1;
}

int zz_blitq_pending(void)
{
    return zz_queue_pending(&zz_blitq);
}

void zz_blitq_drain(void)
{
    zz_queue_drain(&zz_blitq);
#ifdef EMU68_A3D
    if (zz_blitq.ud)
        zz_blitq_settle((struct zz_card *)zz_blitq.ud);
#endif
}

#ifdef EMU68_A3D
void zz_blitq_run_ready(void);
uint32_t zz_blitq_flip_fence(uint32_t lo, uint32_t hi)
{
    struct zz_card *c = (struct zz_card *)zz_blitq.ud;
    if (!c || !zz_queue_pending(&zz_blitq))
        return 0;
    zz_blitq_run_ready();
    uint32_t disp_lo, disp_hi, fence = 0;
    zz_scanout_shows(c, &disp_lo, &disp_hi);
    for (uint32_t i = 0; i < zz_blitq.count; i++) {
        const struct zz_qop *op = zz_queue_op(&zz_blitq, i);
        for (uint32_t k = 0; k < 2u; k++)
            if (op->rows_done == 0 && op->dep[k] != ZZ_QDEP_NONE && op->dep[k] > fence)
                fence = op->dep[k];
        if (op->trig != 10)
            continue;
        if (lo < disp_hi && hi > disp_lo)
            return fence ? fence : 1u;
        zz_pan_shows(c, op, &disp_lo, &disp_hi);
    }
    return 0;
}
#endif

int zz_blitq_is_covered(void)
{
#ifdef EMU68_A3D
    return zz_blitq_covered;
#else
    return 0;
#endif
}

#ifdef EMU68_A3D
#if defined(__arm__) && !defined(__APPLE__)
#define ZZ_AP_TEXT __attribute__((section(".a3d_textend"), noinline))
#else
#define ZZ_AP_TEXT __attribute__((noinline))
#endif

ZZ_AP_TEXT uint32_t zz_blitq_newest_flip(uint32_t *nflips, uint32_t shows[2], uint32_t replaces[2])
{
    const struct zz_card *c = (const struct zz_card *)zz_blitq.ud;
    uint32_t n = 0, last = 0;
    for (uint32_t i = 0; i < zz_blitq.count; i++)
        if (zz_queue_op(&zz_blitq, i)->trig == 10) {
            n++;
            last = i;
        }
    *nflips = n;
    if (!n || !c)
        return 0;
    const struct zz_gfx_hdr *d = (const struct zz_gfx_hdr *)(const void *)zz_queue_op(&zz_blitq, last)->payload;
    const uint32_t width = GDB16(d->x[1]);
    const uint32_t off = GDB32(d->offset[0]) + (uint32_t)(((int16_t)GDB16(d->x[0])) << d->u8_user[0])
                       + (uint32_t)(((int16_t)GDB16(d->y[0])) * (int32_t)(width << d->u8_user[0]));
    const uint32_t pitch = width << (c->scanout.colormode == ZZ_CM_15BIT ? 1 : c->scanout.colormode);
    shows[0] = ZZ_MEMBASE_OFF + off;
    shows[1] = shows[0] + pitch * c->scanout.height;
    replaces[0] = c->scanout.fb_off;
    replaces[1] = c->scanout.fb_off + c->scanout.pitch_bytes * c->scanout.height;
    return zz_blitq.n_enq - zz_blitq.count + last + 1u;
}

ZZ_AP_TEXT int zz_blitq_recover(void)
{
    struct zz_card *c = (struct zz_card *)zz_blitq.ud;
    return c && zz_queue_pending(&zz_blitq) && zz_blitq_try_cover(c);
}

#if defined(__arm__) && !defined(__APPLE__)
#define ZZ_R9_TEXT __attribute__((section(".r9_tail_text"), noinline))
#else
#define ZZ_R9_TEXT __attribute__((noinline))
#endif
#endif

void zz_blitq_run_ready(void)
{
#ifdef EMU68_A3D
    if (zz_blitq_in_gate)
        return;
#endif
    zz_queue_run_ready(&zz_blitq);
#ifdef EMU68_A3D
    if (zz_blitq.ud)
        zz_blitq_settle((struct zz_card *)zz_blitq.ud);
#endif
}

void zz_blitq_gate_stats(uint32_t out[6])
{
    out[0] = zz_blitq.n_gated;
    out[1] = zz_blitq.n_gate_waits;
    out[2] = zz_blitq.n_gate_blocks;
#ifdef EMU68_A3D
    out[3] = zz_bq_covers;
    out[4] = zz_bq_cover_refused;
    out[5] = zz_bq_tmpl;
#else
    out[3] = out[4] = out[5] = 0;
#endif
}

void zz_blitq_tick(void)
{
    zz_queue_tick(&zz_blitq);
#ifdef EMU68_A3D
    if (zz_blitq.ud)
        zz_blitq_settle((struct zz_card *)zz_blitq.ud);
#endif
}

static void zz_dma_op(struct zz_card *c, uint16_t op)
{
    struct zz_gfxdata *d = (struct zz_gfxdata *)(void *)
        (c->vram + ZZ_GFXDATA_OFF);
    if (ZZ_GFXDATA_OFF + sizeof(*d) > c->backing)
        return;

    zz_sprite_undraw(c);

    switch (op) {
    case 1: { /* OP_DRAWLINE */
        uint16_t x0 = GDB16(d->x[0]), x1 = GDB16(d->x[1]);
        uint16_t y0 = GDB16(d->y[0]), y1 = GDB16(d->y[1]);
        uint16_t len = GDB16(d->user[0]), pat = GDB16(d->user[1]);
        uint16_t err = GDB16(d->user[3]);
        uint16_t pitch = GDB16(d->pitch[0]);
        uint32_t off = GDB32(d->offset[0]);
        unsigned cm = d->u8_user[0];
        if (!zz_rect_ok(c, off, (uint32_t)pitch * 4, 0, 0,
                        (uint32_t)x0 + x1 + 2, (uint32_t)y0 + y1 + 2,
                        zz_bpp(cm)))
            break;
        zz_engine_fb(c, off, pitch);
        if (line_uses_solid_path(pat, d->mask, d->u8_user[1]))
            draw_line_solid((int16_t)x0, (int16_t)y0, (int16_t)x1,
                            (int16_t)y1, len, (int16_t)err,
                            zz_pen_val(GDB32(d->rgb[0]), cm), cm);
        else
            draw_line((int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1,
                      len, (int16_t)err, pat, d->u8_user[2],
                      zz_pen_val(GDB32(d->rgb[0]), cm),
                      zz_pen_val(GDB32(d->rgb[1]), cm),
                      cm, d->mask, d->u8_user[1]);
        break;
    }

    case 2: { /* OP_FILLRECT */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]);
        uint16_t y0 = GDB16(d->y[0]), h = GDB16(d->y[1]);
        uint16_t pitch = GDB16(d->pitch[0]);
        uint32_t off = GDB32(d->offset[0]);
        unsigned cm = d->u8_user[0];
        uint32_t pen = zz_pen_val(GDB32(d->rgb[0]), cm);
        if (!zz_rect_ok(c, off, (uint32_t)pitch * 4, x0, y0, w, h,
                        zz_bpp(cm)))
            break;
        zz_engine_fb(c, off, pitch);
        if (d->mask == 0xFF)
            fill_rect_solid(x0, y0, w, h, pen, cm);
        else
            fill_rect(x0, y0, w, h, pen, cm, d->mask);
        zz_flush_rect_b(c, ZZ_MEMBASE_OFF + off, (uint32_t)pitch * 4u,
                        x0, y0, w, h, zz_bpp(cm));
        break;
    }

    case 3: /* OP_COPYRECT */
    case 4: { /* OP_COPYRECT_NOMASK */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), sx = GDB16(d->x[2]);
        uint16_t y0 = GDB16(d->y[0]), h = GDB16(d->y[1]), sy = GDB16(d->y[2]);
        uint16_t dpitch = GDB16(d->pitch[0]), spitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), soff = GDB32(d->offset[1]);
        unsigned cm = d->u8_user[0];
        if (op == 3) {
            if (!zz_rect_ok(c, doff, (uint32_t)dpitch * 4, x0, y0, w, h,
                            zz_bpp(cm)) ||
                !zz_rect_ok(c, doff, (uint32_t)dpitch * 4, sx, sy, w, h,
                            zz_bpp(cm)))
                break;
            zz_engine_fb(c, doff, dpitch);
            if (d->mask == 0xFF || (d->mask != 0xFF && cm != ZZ_CM_8BIT))
                copy_rect_nomask(x0, y0, w, h, sx, sy, cm,
                                 (uint32_t *)(void *)zz_mem(c, doff),
                                 dpitch, MINTERM_SRC);
            else
                copy_rect(x0, y0, w, h, sx, sy, cm,
                          (uint32_t *)(void *)zz_mem(c, doff),
                          dpitch, d->mask);
        } else {
            if (!zz_rect_ok(c, doff, (uint32_t)dpitch * 4, x0, y0, w, h,
                            zz_bpp(cm)) ||
                !zz_rect_ok(c, soff, (uint32_t)spitch * 4, sx, sy, w, h,
                            zz_bpp(cm)))
                break;
            if (zz_engine_mt_ok(d->minterm)) {
                zz_engine_fb(c, doff, dpitch);
                copy_rect_nomask(x0, y0, w, h, sx, sy, cm,
                                 (uint32_t *)(void *)zz_mem(c, soff),
                                 spitch, d->minterm);
            } else {
                zz_classic_copy(c, doff, (uint32_t)dpitch * 4,
                                soff, (uint32_t)spitch * 4,
                                cm, x0, y0, sx, sy, w, h, d->minterm);
            }
        }
        zz_flush_rect_b(c, ZZ_MEMBASE_OFF + doff, (uint32_t)dpitch * 4u,
                        x0, y0, w, h, zz_bpp(cm));
        break;
    }

    case 5: /* OP_RECT_TEMPLATE */
    case 6: { /* OP_RECT_PATTERN */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]), xo = GDB16(d->x[2]);
        uint16_t y0 = GDB16(d->y[0]), h = GDB16(d->y[1]), yo = GDB16(d->y[2]);
        uint16_t dpitch = GDB16(d->pitch[0]), tpitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), toff = GDB32(d->offset[1]);
        unsigned cm = d->u8_user[0];
        uint32_t fg = zz_pen_val(GDB32(d->rgb[0]), cm);
        uint32_t bg = zz_pen_val(GDB32(d->rgb[1]), cm);
        /* dst pitch is BYTES for these two ops */
        if (!zz_rect_ok(c, doff, dpitch, x0, y0, w, h, zz_bpp(cm)))
            break;
        uint8_t *tmpl = zz_mem(c, toff);
        if (op == 6) {
            uint16_t loops = GDB16(d->user[0]);
            if (!loops || !zz_blob_ok(c, toff, (uint64_t)16 * loops))
                break;
            zz_engine_fb(c, doff, dpitch);
            pattern_fill_rect(cm, x0, y0, w, h, d->u8_user[1], d->mask,
                              fg, bg, xo, yo, tmpl, 16, loops);
        } else {
            /* template: pitch may legally be 0 (one row repeats) */
            uint64_t rows = tpitch ? (uint64_t)h : 1;
            uint64_t row_bytes = ((uint64_t)xo + w + 7) / 8 + 2;
            if (!zz_blob_ok(c, toff, (rows - 1) * tpitch + row_bytes))
                break;
            zz_engine_fb(c, doff, dpitch);
            template_fill_rect(cm, x0, y0, w, h, d->u8_user[1], d->mask,
                               fg, bg, xo, yo, tmpl, tpitch);
        }
        break;
    }

    case 7: /* OP_P2C */
    case 8: { /* OP_P2D */
        uint16_t x0 = GDB16(d->x[0]), dx = GDB16(d->x[1]), w = GDB16(d->x[2]);
        uint16_t dy = GDB16(d->y[1]), h = GDB16(d->y[2]);
        uint16_t dpitch = GDB16(d->pitch[0]), spitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), soff = GDB32(d->offset[1]);
        uint16_t layer = GDB16(d->user[0]), planes16 = GDB16(d->user[1]);
        unsigned cm = d->u8_user[0];
        uint32_t planes = planes16 & 0xffu;
        if (planes > 8) planes = 8;
        if (!zz_rect_ok(c, doff, (uint32_t)dpitch * 4, dx, dy, w, h,
                        op == 7 ? 1 : zz_bpp(cm)) ||
            !zz_blob_ok(c, soff,
                        (op == 8 ? 1024u : 0u)
                        + (uint64_t)spitch * h * (planes ? planes : 1)
                        + spitch))
            break;
        zz_engine_fb(c, doff, dpitch);
        if (op == 7)
            p2c_rect((int16_t)x0, 0, (int16_t)dx, (int16_t)dy,
                     (int16_t)w, (int16_t)h, d->minterm,
                     (uint8_t)planes16, d->mask, (uint8_t)layer,
                     spitch, zz_mem(c, soff));
        else
            p2d_rect((int16_t)x0, 0, (int16_t)dx, (int16_t)dy,
                     (int16_t)w, (int16_t)h, d->minterm,
                     (uint8_t)planes16, d->mask, (uint8_t)layer,
                     zz_pen_val(GDB32(d->rgb[0]), cm),
                     spitch, zz_mem(c, soff), cm);
        break;
    }

    case 18: { /* OP_WRITE_YUV */
        uint16_t phase = GDB16(d->x[0]), dx = GDB16(d->x[1]), w = GDB16(d->x[2]);
        uint16_t dy = GDB16(d->y[1]), h = GDB16(d->y[2]);
        uint16_t dpitch = GDB16(d->pitch[0]), spitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), soff = GDB32(d->offset[1]);
        unsigned cm = d->u8_user[0];
        if (!zz_rect_ok(c, doff, (uint32_t)dpitch * 4, dx, dy, w, h,
                        zz_bpp(cm)) ||
            !zz_blob_ok(c, soff, (uint64_t)spitch * (h ? h : 1)))
            break;
        zz_engine_fb(c, doff, dpitch);
        yuv422_to_rgb_rect((int16_t)phase, (int16_t)dx, (int16_t)dy,
                           (int16_t)w, (int16_t)h, d->u8_user[4], cm,
                           spitch, zz_mem(c, soff));
        break;
    }

    case 9: { /* OP_INVERTRECT */
        uint16_t x0 = GDB16(d->x[0]), w = GDB16(d->x[1]);
        uint16_t y0 = GDB16(d->y[0]), h = GDB16(d->y[1]);
        uint16_t pitch = GDB16(d->pitch[0]);
        uint32_t off = GDB32(d->offset[0]);
        unsigned cm = d->u8_user[0];
        if (!zz_rect_ok(c, off, (uint32_t)pitch * 4, x0, y0, w, h,
                        zz_bpp(cm)))
            break;
        zz_engine_fb(c, off, pitch);
        invert_rect(x0, y0, w, h, d->mask, cm);
        zz_flush_rect_b(c, ZZ_MEMBASE_OFF + off, (uint32_t)pitch * 4u,
                        x0, y0, w, h, zz_bpp(cm));
        break;
    }

    case 11: /* OP_SPRITE_XY */
        if (!c->sprite.showing)
            break;
        c->sprite.x = (int16_t)GDB16(d->x[0]);
        c->sprite.y = (int16_t)GDB16(d->y[0]);
        break;

    case 13: /* OP_SPRITE_BITMAP */
        c->sprite.x_off = (int16_t)GDB16(d->x[0]);
        c->sprite.y_off = (int16_t)GDB16(d->y[0]);
        c->sprite.width  = GDB16(d->x[1]);
        c->sprite.height = GDB16(d->y[1]);
        zz_sprite_decode(c, GDB32(d->offset[1]),
                         c->sprite.width, c->sprite.height);
        break;

    case 12:
        if (d->u8offset < 4) {
            c->sprite.colors[d->u8offset] = zz_rd_le32(&d->rgb[0]);
            if (d->u8offset != 0 && c->sprite.colors[d->u8offset] == 0xff00ffu)
                c->sprite.colors[d->u8offset] = 0xfe00feu;
            c->sprite.hw_gen++;  /* colour changed: route-A re-pushes overlay */
        }
        break;

    case 10: { /* OP_PAN */
        uint32_t off = GDB32(d->offset[0]);
        uint16_t xo = GDB16(d->x[0]), yo = GDB16(d->y[0]);
        uint16_t width = GDB16(d->x[1]);
        c->sprite.x_off = (int16_t)xo;
        c->sprite.y_off = (int16_t)yo;
        c->pan_width = width;
        c->pan_off = off
                   + (uint32_t)(((int16_t)xo) << d->u8_user[0])
                   + (uint32_t)(((int16_t)yo) *
                                (int32_t)(c->pan_width << d->u8_user[0]));
        zz_scanout_update(c);
        break;
    }

    case 16: /* OP_SET_SPLIT_POS */
        c->bgbuf_off = GDB32(d->offset[0]);
        c->split_pos = GDB16(d->y[0]);
        break;

    case 17: { /* OP_SET_PALETTE */
        uint16_t start = GDB16(d->user[0]), count = GDB16(d->user[1]);
        uint8_t secondary = d->u8_user[0] ? 1 : 0;
        if (count > 256) count = 256;
        for (uint16_t i = 0; i < count; i++) {
            uint32_t idx = (uint32_t)(start + i) & 0xFF;
            uint8_t *dst = secondary ? c->palette.rgb2[idx]
                                     : c->palette.rgb[idx];
            dst[0] = d->clut1[i * 3];
            dst[1] = d->clut1[i * 3 + 1];
            dst[2] = d->clut1[i * 3 + 2];
        }
        c->palette.dirty = 1;
        break;
    }

    case 14: /* OP_SPRITE_CLUT_BITMAP - SDK path, not issued by the driver */
    case 15: /* OP_ETH_USB_OFFSETS   - subsystem out of scope              */
    case 19: /* OP_VIDEO_OVERLAY     - gated off by FW_VERSION 0x0204      */
    default:
        break;
    }

    zz_sprite_draw(c);
}

/* ---- 5. ACC_OP dispatch (port of ZZ9000OS dma_acc.c, driver subset) ------ */

static uint32_t zz_surface_alloc(struct zz_card *c, uint32_t size)
{
    size = (size + 255u) & ~255u;
    if (!size || c->surface_count >= ZZ_MAX_SURFACES)
        return 0;
    uint32_t heap_start = ZZ_HEAP_START_OFF - ZZ_MEMBASE_OFF;
    uint32_t heap_end   = c->heap_end - ZZ_MEMBASE_OFF;
    uint32_t cursor = heap_start;
    int at = c->surface_count;
    for (int i = 0; i < c->surface_count; i++) {
        if (c->surfaces[i].off - cursor >= size) { at = i; break; }
        cursor = c->surfaces[i].off + c->surfaces[i].size;
    }
    if (at == c->surface_count && heap_end - cursor < size)
        return 0;
    memmove(&c->surfaces[at + 1], &c->surfaces[at],
            (size_t)(c->surface_count - at) * sizeof(c->surfaces[0]));
    c->surfaces[at].off = cursor;
    c->surfaces[at].size = size;
    c->surface_count++;
    memset(zz_mem(c, cursor), 0, size);
    return cursor;
}

static int zz_surface_free_at(struct zz_card *c, uint32_t off)
{
    for (int i = 0; i < c->surface_count; i++)
        if (c->surfaces[i].off == off) {
            memmove(&c->surfaces[i], &c->surfaces[i + 1],
                    (size_t)(c->surface_count - i - 1) * sizeof(c->surfaces[0]));
            c->surface_count--;
            return 0;
        }
    return -1;
}

static void zz_acc_op(struct zz_card *c, uint16_t op)
{
    struct zz_gfxdata *d = (struct zz_gfxdata *)(void *)
        (c->vram + ZZ_GFXDATA_OFF);
    if (ZZ_GFXDATA_OFF + sizeof(*d) > c->backing)
        return;

    switch (op) {
    case 4: { /* ACC_OP_ALLOC_SURFACE */
        uint32_t size;
        if (d->u8_user[1] == 1)
            size = GDB32(d->offset[1]);
        else
            size = (uint32_t)GDB16(d->x[0]) * d->u8_user[0] * GDB16(d->y[0]);
        uint32_t off = size ? zz_surface_alloc(c, size) : 0;
        zz_wr_be32(&d->offset[0], off);       /* result stored big-endian */
        break;
    }
    case 5: /* ACC_OP_FREE_SURFACE */
        zz_surface_free_at(c, GDB32(d->offset[0]));
        zz_wr_be32(&d->offset[0], 0);
        break;

    case 2: { /* ACC_OP_BUFFER_CLEAR - offsets are BOARD-relative (ADDR_ADJ) */
        uint16_t w = GDB16(d->x[0]), h = GDB16(d->y[0]);
        uint16_t pitch = GDB16(d->pitch[0]);
        uint32_t off = GDB32(d->offset[0]);
        unsigned bpp = d->u8_user[0];
        unsigned cm  = (bpp == 4) ? ZZ_CM_32BIT
                     : (bpp == 2) ? ZZ_CM_16BIT : ZZ_CM_8BIT;
        zz_sprite_undraw(c);
        if ((bpp == 1 || bpp == 2 || bpp == 4) &&
            off >= ZZ_MEMBASE_OFF &&
            zz_rect_ok(c, off - ZZ_MEMBASE_OFF, (uint32_t)pitch * bpp, 0, 0,
                       w, h ? h : 1, bpp)) {
            acc_clear_buffer((uintptr_t)(c->vram + off), w, h, pitch,
                             zz_pen_val(GDB32(d->rgb[0]), cm), bpp);
            zz_flush_rect_b(c, off, (uint32_t)pitch * bpp, 0, 0,
                            w, h ? h : 1, bpp);
        }
        zz_sprite_draw(c);
        break;
    }

    case 3: { /* ACC_OP_BLIT_RECT */
        uint16_t x0 = GDB16(d->x[0]), y0 = GDB16(d->y[0]);
        uint16_t w = GDB16(d->x[1]), h = GDB16(d->y[1]);
        uint16_t dpitch = GDB16(d->pitch[0]), spitch = GDB16(d->pitch[1]);
        uint32_t doff = GDB32(d->offset[0]), soff = GDB32(d->offset[1]);
        zz_sprite_undraw(c);
        if (d->u8_user[0] == d->u8_user[1] &&
            doff >= ZZ_MEMBASE_OFF && soff >= ZZ_MEMBASE_OFF &&
            zz_rect_ok(c, doff - ZZ_MEMBASE_OFF, dpitch, 0, 0,
                       (uint32_t)x0 + w * d->u8_user[0],
                       (uint32_t)y0 + h, 1) &&
            zz_blob_ok(c, soff - ZZ_MEMBASE_OFF, (uint64_t)spitch * h)) {
            acc_blit_rect((uintptr_t)(c->vram + doff),
                          (uintptr_t)(c->vram + soff),
                          x0, y0, (uint16_t)(w * d->u8_user[0]), h,
                          dpitch, spitch, d->u8_user[2], d->u8offset);
            zz_flush_rect_b(c, soff, spitch, x0, y0,
                            (uint32_t)w * d->u8_user[0], h, 1);
        }
        zz_sprite_draw(c);
        break;
    }

    default: /* FLIP / conversion / primitives / codecs: SDK-facing, out */
        break;
    }
}

/* ---- 6. register write / read ------------------------------------------- */

#ifdef EMU68_A3D
static int zz_a3d_op_surfaces(struct zz_card *c, uint32_t off, uint16_t v, uint32_t *ps0, uint32_t *ps1)
{
    uint32_t s0 = 0xFFFFFFFFu, s1 = 0xFFFFFFFFu;
    if (off == 0x5a || off == 0x5c) {
        if (ZZ_GFXDATA_OFF + sizeof(struct zz_gfxdata) > c->backing)
            return 0;
        const struct zz_gfxdata *d = (const struct zz_gfxdata *)(const void *)(c->vram + ZZ_GFXDATA_OFF);
        if (off == 0x5a) {
            switch (v) {
            case 4: case 7: case 8: case 18:        /* COPYRECT_NOMASK, P2C, P2D, WRITE_YUV: + the source */
                s1 = ZZ_MEMBASE_OFF + GDB32(d->offset[1]);
                /* fall through */
            case 1: case 2: case 3: case 5: case 6: case 9: case 10:
                s0 = ZZ_MEMBASE_OFF + GDB32(d->offset[0]);
                break;
            default:
                return 0;
            }
        } else {
            switch (v) {
            case 3:                                 /* ACC_OP_BLIT_RECT */
                s1 = GDB32(d->offset[1]);
                /* fall through */
            case 2:                                 /* ACC_OP_BUFFER_CLEAR */
                s0 = GDB32(d->offset[0]);
                break;
            default:
                return 0;
            }
        }
    } else {
        switch (off) {
        case 0x24: case 0x38: case 0x3c:            /* COPYRECT, P2C, P2D: + the source latch */
            s1 = ZZ_MEMBASE_OFF + c->src_off;
            /* fall through */
        case 0x22: case 0x26: case 0x3a: case 0x3e:
            s0 = ZZ_MEMBASE_OFF + c->dst_off;
            break;
        default:
            return 0;
        }
    }
    *ps0 = s0;
    *ps1 = s1 != s0 ? s1 : 0xFFFFFFFFu;
    return 1;
}

static void zz_a3d_fence_surfaces(struct zz_card *c, uint32_t s0, uint32_t s1)
{
    if (!c->a3d_fence)
        return;
    if (s0 != 0xFFFFFFFFu)
        c->a3d_fence(c->ud, s0);
    if (s1 != 0xFFFFFFFFu && s1 != s0)
    {
        c->a3d_fence(c->ud, s1);
    }
}

static int zz_blitq_gate(void *ud, const struct zz_qop *op, int wait)
{
    struct zz_card *c = ud;
    const uint32_t f = op->dep[0];
    int ok = 1;
    if (f == ZZ_QDEP_NONE)
        return 1;
    zz_blitq_in_gate = 1;
    if (wait) {
        if (c->a3d_fence_wait)
            c->a3d_fence_wait(c->ud, f);
    } else {
        ok = c->a3d_fence_try ? c->a3d_fence_try(c->ud, f) : 0;    /* disarmed: only a blocking path clears it */
    }
    zz_blitq_in_gate = 0;
    return ok;
}

static uint32_t zz_a3d_op_fence(struct zz_card *c, uint32_t s0, uint32_t s1)
{
    uint32_t f = 0;
    if (s0 != 0xFFFFFFFFu) {
        const uint32_t f0 = c->a3d_fence_of(c->ud, s0);
        if (f0 > f) f = f0;
    }
    if (s1 != 0xFFFFFFFFu) {
        const uint32_t f1 = c->a3d_fence_of(c->ud, s1);
        if (f1 > f) f = f1;
    }
    return f ? f : ZZ_QDEP_NONE;
}
#endif /* EMU68_A3D */

static void zz_set_mode_reg(struct zz_card *c, uint16_t value)
{
    unsigned mode = value & 0xff;
    unsigned cm = (value >> 8) & 0x0f;
    unsigned scale = (value >> 12) & 0x0f;
    if (mode >= ZZ_MODE_COUNT || !zz_bpp(cm))
        return;
    c->mode_id = (uint16_t)mode;
    c->scale_mode = (uint8_t)scale;
    c->scanout.colormode = (uint8_t)cm;
    c->scanout.width  = (scale & 1) ? zz_modes[mode].w / 2 : zz_modes[mode].w;
    c->scanout.height = (scale & 2) ? zz_modes[mode].h / 2 : zz_modes[mode].h;
    /* ZZ9000OS preserves the virtual stride across SetGC; only seed it */
    if (!c->pan_width)
        c->pan_width = c->scanout.width;
    if (c->capture_mode) {
        c->capture_mode = 0;
        c->scanout.enabled = 1;
    }
    zz_scanout_update(c);
    zz_sprite_refresh(c);
}

static void zz_palette_z2_commit(struct zz_card *c, uint16_t op)
{
    if (op != 3 && op != 19)
        return;
    uint32_t idx = (c->pal_data >> 24) & 0xff;
    uint8_t *dst = (op == 19) ? c->palette.rgb2[idx] : c->palette.rgb[idx];
    dst[0] = (uint8_t)(c->pal_data >> 16);
    dst[1] = (uint8_t)(c->pal_data >> 8);
    dst[2] = (uint8_t)c->pal_data;
    c->palette.dirty = 1;
}

volatile uint32_t g_zznet_active = 0;

static void zz_eth_pub_mac(struct zz_card *c)
{
    if (c->eth_ctrl)
        memcpy((uint8_t *)c->eth_ctrl + ZZNET_CTRL_MAC, c->eth_mac, 6);
}

void zz_eth_set_mac(struct zz_card *c, const uint8_t *mac)
{
    int i;
    for (i = 0; i < 6; i++)
        c->eth_mac[i] = mac[i];
    zz_eth_pub_mac(c);
}

static void zz_eth_stage(struct zz_card *c)
{
    uint32_t prod, size;
    const uint8_t *slot;
    uint8_t *win;

    if (!c->eth_ctrl || !c->eth_win || c->eth_win_full)
        return;
    prod = c->eth_ctrl[ZZNET_CTRL_RX_PROD / 4];
    if (prod == c->eth_rx_cons)
        return;
    slot = c->eth_rxring + (c->eth_rx_cons % ZZNET_SLOTS) * ZZNET_SLOT_SIZE;
    size = *(const volatile uint32_t *)(const void *)slot;
    if (size > ZZNET_SLOT_SIZE - 4u) size = ZZNET_SLOT_SIZE - 4u;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);      /* index before the bytes    */
    win = c->eth_win + 0x2000;
    c->eth_rx_serial++;
    if (c->eth_rx_serial < 2) c->eth_rx_serial = 2;
    *(volatile uint16_t *)(void *)(win + 0) = (uint16_t)size;
    *(volatile uint16_t *)(void *)(win + 2) = c->eth_rx_serial;
    memcpy(win + 4, slot + 4, size);
    c->eth_win_full    = 1;
    c->eth_int_pending = 1;
    c->eth_inject_req  = 1;
}

void zz_eth_pump(struct zz_card *c)
{
    if (!c->int_enabled_eth || !c->eth_ctrl)
        return;
    if (!c->eth_win_full &&
        c->eth_ctrl[ZZNET_CTRL_RX_PROD / 4] != c->eth_rx_cons)
        zz_eth_stage(c);
    if (c->eth_inject_req && c->eth_inject) {
        c->eth_inject_req = 0;
        c->eth_inject(c->ud);
    }
}

void zz_card_reg_write16(struct zz_card *c, uint32_t off, uint16_t v)
{
    int captured = 0;
#ifdef EMU68_A3D
    uint32_t s0 = 0xFFFFFFFFu, s1 = 0xFFFFFFFFu;
    const int fence = c->a3d_fence && zz_a3d_op_surfaces(c, off, v, &s0, &s1);
    const int defer = fence && c->a3d_defer && c->a3d_fence_of && c->a3d_fence_try && c->a3d_fence_wait &&
                      off == 0x5a && zz_blitq.armed;
    if (fence && !defer)
        zz_a3d_fence_surfaces(c, s0, s1);
    if (off == 0x5a && zz_blitq.armed)
        captured = zz_blitq_capture(c, v, defer ? zz_a3d_op_fence(c, s0, s1) : ZZ_QDEP_NONE, ZZ_QDEP_NONE);
    if (defer && !captured)
        zz_a3d_fence_surfaces(c, s0, s1);           /* not queueable after all: fenced synchronously */
#else
    if (off == 0x5a && zz_blitq.armed)
        captured = zz_blitq_capture(c, v, ZZ_QDEP_NONE, ZZ_QDEP_NONE);
#endif
    const int sprite_pos_free = off == 0x5a && v == 11u && g_zz_hw_sprite && !c->sprite.drawn;
    if (!captured && !sprite_pos_free && zz_queue_pending(&zz_blitq))
        zz_queue_drain(&zz_blitq);
#ifdef EMU68_A3D
    zz_blitq_settle(c);
#endif

    if (off / 2 < sizeof(c->regs) / sizeof(c->regs[0]))
        c->regs[off / 2] = v;
    if (captured)
        return;

    switch (off) {
    case 0x02: zz_set_mode_reg(c, v); break;

    case 0x04:
        if (v & 8) {
            if (v & 16)
                c->eth_int_pending = 0;
        } else if (v & 0x80) {
            c->int_enabled_vblank = (v >> 1) & 1;
        } else {
            c->int_enabled_eth = v & 1;
            if (!c->int_enabled_eth)
                c->eth_int_pending = 0;    /* enable 0 also clears (main.c) */
        }
        g_zznet_active = (c->int_enabled_eth && c->eth_ctrl) ? 1u : 0u;
        break;

    case REG_ZZ_ETH_TX: {  /* +0x80 write size = send the frame in the TX window */
        if (!c->eth_ctrl || !c->eth_win) { c->eth_tx_status = 0xFFFF; break; }
        uint32_t prod = c->eth_ctrl[ZZNET_CTRL_TX_PROD / 4];
        uint32_t cons = c->eth_ctrl[ZZNET_CTRL_TX_CONS / 4];
        uint32_t size = v;
        if (size > ZZNET_SLOT_SIZE - 4u) size = ZZNET_SLOT_SIZE - 4u;
        if (prod - cons >= ZZNET_SLOTS) { c->eth_tx_status = 2; break; } /* full */
        {
            uint8_t *slot = c->eth_txring + (prod % ZZNET_SLOTS) * ZZNET_SLOT_SIZE;
            *(volatile uint32_t *)(void *)slot = size;
            memcpy(slot + 4, c->eth_win + 0x8000, size);
        }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);   /* bytes before the index */
        c->eth_ctrl[ZZNET_CTRL_TX_PROD / 4] = prod + 1;
        c->eth_tx_status = 0;
        break; }

    case REG_ZZ_ETH_RX:
        if (!c->eth_ctrl || !c->eth_win_full) { break; }
        if (v != 1u && v != c->eth_rx_serial) { break; }
        if (c->eth_ctrl[ZZNET_CTRL_RX_PROD / 4] != c->eth_rx_cons) {
            c->eth_rx_cons++;
            c->eth_ctrl[ZZNET_CTRL_RX_CONS / 4] = c->eth_rx_cons;  /* diagnostic */
        }
        c->eth_win_full = 0;
        zz_eth_stage(c);
        break;

    case REG_ZZ_ETH_MAC_HI:
        c->eth_mac[0] = (uint8_t)(v >> 8); c->eth_mac[1] = (uint8_t)v;
        zz_eth_pub_mac(c); break;
    case REG_ZZ_ETH_MAC_HI2:
        c->eth_mac[2] = (uint8_t)(v >> 8); c->eth_mac[3] = (uint8_t)v;
        zz_eth_pub_mac(c); break;
    case REG_ZZ_ETH_MAC_LO:
        c->eth_mac[4] = (uint8_t)(v >> 8); c->eth_mac[5] = (uint8_t)v;
        zz_eth_pub_mac(c); break;

    case 0x10: c->x[0] = v; break;
    case 0x12: c->y[0] = v; break;
    case 0x14: c->x[1] = v; break;
    case 0x16: c->y[1] = v; break;
    case 0x1a: c->x[2] = v; break;
    case 0x1c: c->y[2] = v; break;
    case 0x40: c->user[0] = v; break;
    case 0x42: c->user[1] = v; break;
    case 0x44: c->user[2] = v; break;
    case 0x46: c->user[3] = v; break;
    case 0x18: c->row_pitch = v; break;
    case 0x32: c->src_pitch = v; break;
    case 0x28: c->src_off = (c->src_off & 0xffffu) | ((uint32_t)v << 16); break;
    case 0x2a: c->src_off = (c->src_off & 0xffff0000u) | v; break;
    case 0x2c: c->dst_off = (c->dst_off & 0xffffu) | ((uint32_t)v << 16); break;
    case 0x2e: c->dst_off = (c->dst_off & 0xffff0000u) | v; break;
    case 0x30: c->colormode = (uint8_t)(v & 0x0f);
               c->fg_minterm = (uint8_t)(v >> 8); break;
    case 0x1e: c->fg = (c->fg & 0x0000ffffu) | ((uint32_t)v << 16); break;
    case 0x20: c->fg = (c->fg & 0xffff0000u) | v; break;
    case 0x34: c->bg = (c->bg & 0x0000ffffu) | ((uint32_t)v << 16); break;
    case 0x36: c->bg = (c->bg & 0xffff0000u) | v; break;

    case 0x0a: c->pan_off = (c->pan_off & 0xffffu) | ((uint32_t)v << 16); break;
    case 0x0c: {
        /* legacy pan commit: base|lo + rect_x1/rect_y1 fold (main.c:594) */
        uint32_t base = (c->pan_off & 0xffff0000u) | v;
        c->pan_width = c->x[1];
        c->pan_off = base + (uint32_t)(((int16_t)c->x[0]) << c->colormode)
                   + (uint32_t)(((int16_t)c->y[0]) *
                                (int32_t)(c->pan_width << c->colormode));
        c->sprite.x_off = (int16_t)c->x[0];
        c->sprite.y_off = (int16_t)c->y[0];
        zz_scanout_update(c);
        zz_sprite_refresh(c);
        break;
    }

    case 0x22: /* FILLRECT */
        zz_sprite_undraw(c);
        if (zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4,
                       c->x[0], c->y[0], c->x[1], c->y[1],
                       zz_bpp(c->colormode))) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            if ((v & 0xff) == 0xFF)
                fill_rect_solid(c->x[0], c->y[0], c->x[1], c->y[1],
                                zz_pen_val(c->fg, c->colormode), c->colormode);
            else
                fill_rect(c->x[0], c->y[0], c->x[1], c->y[1],
                          zz_pen_val(c->fg, c->colormode),
                          c->colormode, (uint8_t)v);
        }
        zz_sprite_draw(c);
        break;

    case 0x24:
        zz_sprite_undraw(c);
        if (v == 1 &&
            zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4,
                       c->x[0], c->y[0], c->x[1], c->y[1],
                       zz_bpp(c->colormode)) &&
            zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4,
                       c->x[2], c->y[2], c->x[1], c->y[1],
                       zz_bpp(c->colormode))) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            if (c->fg_minterm == 0xff || c->colormode != ZZ_CM_8BIT)
                copy_rect_nomask(c->x[0], c->y[0], c->x[1], c->y[1],
                                 c->x[2], c->y[2], c->colormode,
                                 (uint32_t *)(void *)zz_mem(c, c->dst_off),
                                 c->row_pitch, MINTERM_SRC);
            else
                copy_rect(c->x[0], c->y[0], c->x[1], c->y[1],
                          c->x[2], c->y[2], c->colormode,
                          (uint32_t *)(void *)zz_mem(c, c->dst_off),
                          c->row_pitch, c->fg_minterm);
        } else if (v == 2 &&
            zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4,
                       c->x[0], c->y[0], c->x[1], c->y[1],
                       zz_bpp(c->colormode)) &&
            zz_rect_ok(c, c->src_off, (uint32_t)c->src_pitch * 4,
                       c->x[2], c->y[2], c->x[1], c->y[1],
                       zz_bpp(c->colormode))) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            copy_rect_nomask(c->x[0], c->y[0], c->x[1], c->y[1],
                             c->x[2], c->y[2], c->colormode,
                             (uint32_t *)(void *)zz_mem(c, c->src_off),
                             c->src_pitch, c->fg_minterm);
        }
        zz_sprite_draw(c);
        break;

    case 0x26:  {
        int pattern = (v & 0x8000) != 0;
        uint16_t tmpl_pitch = pattern ? 2 : c->src_pitch;
        uint16_t loops = pattern ? (uint16_t)(v & 0xff) : 0;
        uint8_t  mask = pattern ? (uint8_t)(c->user[0] & 0xff)
                                : (uint8_t)(v & 0xff);
        zz_sprite_undraw(c);
        if (zz_rect_ok(c, c->dst_off, c->row_pitch,
                       c->x[0], c->y[0], c->x[1], c->y[1],
                       zz_bpp(c->colormode)) &&
            zz_blob_ok(c, c->src_off,
                       (uint64_t)(pattern ? 2u * (loops ? loops : 1)
                                          : (c->y[1] ? c->y[1] : 1) * tmpl_pitch)
                       + 16)) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            if (pattern)
                pattern_fill_rect(c->colormode, c->x[0], c->y[0],
                                  c->x[1], c->y[1], c->fg_minterm, mask,
                                  zz_pen_val(c->fg, c->colormode),
                                  zz_pen_val(c->bg, c->colormode),
                                  c->x[2], c->y[2],
                                  zz_mem(c, c->src_off), tmpl_pitch, loops);
            else
                template_fill_rect(c->colormode, c->x[0], c->y[0],
                                   c->x[1], c->y[1], c->fg_minterm, mask,
                                   zz_pen_val(c->fg, c->colormode),
                                   zz_pen_val(c->bg, c->colormode),
                                   c->x[2], c->y[2],
                                   zz_mem(c, c->src_off), tmpl_pitch);
        }
        zz_sprite_draw(c);
        break;
    }

    case 0x3a: /* DRAWLINE */
        zz_sprite_undraw(c);
        if (zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4, 0, 0,
                       (uint32_t)c->x[0] + c->x[1] + 2,
                       (uint32_t)c->y[0] + c->y[1] + 2,
                       zz_bpp(c->colormode))) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            if (line_uses_solid_path(c->x[2], (uint8_t)(v & 0xff),
                                     c->fg_minterm))
                draw_line_solid((int16_t)c->x[0], (int16_t)c->y[0],
                                (int16_t)c->x[1], (int16_t)c->y[1],
                                c->user[0], (int16_t)c->user[2],
                                zz_pen_val(c->fg, c->colormode), c->colormode);
            else
                draw_line((int16_t)c->x[0], (int16_t)c->y[0],
                          (int16_t)c->x[1], (int16_t)c->y[1],
                          c->user[0], (int16_t)c->user[2], c->x[2],
                          (uint16_t)(c->y[2] & 15),
                          zz_pen_val(c->fg, c->colormode),
                          zz_pen_val(c->bg, c->colormode),
                          c->colormode, (uint8_t)(v & 0xff), c->fg_minterm);
        }
        zz_sprite_draw(c);
        break;

    case 0x38: /* P2C */
    case 0x3c: /* P2D */ {
        uint32_t planes = (v >> 8) & 0xff;
        if (planes > 8) planes = 8;
        zz_sprite_undraw(c);
        if (zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4,
                       c->x[1], c->y[1], c->x[2], c->y[2],
                       off == 0x38 ? 1 : zz_bpp(c->colormode)) &&
            zz_blob_ok(c, c->src_off,
                       (uint64_t)c->src_pitch * c->y[2] *
                       (planes ? planes : 1) + c->src_pitch)) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            if (off == 0x38)
                p2c_rect((int16_t)c->x[0], 0, (int16_t)c->x[1],
                         (int16_t)c->y[1], (int16_t)c->x[2],
                         (int16_t)c->y[2], c->fg_minterm,
                         (uint8_t)(v >> 8), (uint8_t)(v & 0xff),
                         (uint8_t)(c->user[1] & 0xff), c->src_pitch,
                         zz_mem(c, c->src_off));
            else
                p2d_rect((int16_t)c->x[0], 0, (int16_t)c->x[1],
                         (int16_t)c->y[1], (int16_t)c->x[2],
                         (int16_t)c->y[2], c->fg_minterm,
                         (uint8_t)(v >> 8), (uint8_t)(v & 0xff),
                         (uint8_t)(c->user[1] & 0xff),
                         zz_pen_val(c->fg, c->colormode),
                         c->src_pitch, zz_mem(c, c->src_off), c->colormode);
        }
        zz_sprite_draw(c);
        break;
    }

    case 0x3e: /* INVERTRECT */
        zz_sprite_undraw(c);
        if (zz_rect_ok(c, c->dst_off, (uint32_t)c->row_pitch * 4,
                       c->x[0], c->y[0], c->x[1], c->y[1],
                       zz_bpp(c->colormode))) {
            zz_engine_fb(c, c->dst_off, c->row_pitch);
            invert_rect(c->x[0], c->y[0], c->x[1], c->y[1],
                        (uint8_t)(v & 0xff), c->colormode);
        }
        zz_sprite_draw(c);
        break;

    case 0x5a: zz_dma_op(c, v); break;
    case 0x5c: zz_acc_op(c, v); break;

    case 0x48: /* SPRITE_BITMAP: 1 = show, 2 = hide, else legacy upload */
        if (v == 1) {
            c->sprite.showing = 1;
        } else if (v == 2) {
            c->sprite.showing = 0;
        } else {
            c->sprite.x_off = (int16_t)c->x[0];
            c->sprite.y_off = (int16_t)c->y[0];
            c->sprite.width  = c->x[1];
            c->sprite.height = c->y[1];
            zz_sprite_decode(c, c->src_off, c->x[1], c->y[1]);
        }
        zz_sprite_refresh(c);
        break;

    case 0x08: /* SPRITE_Y: position commit from X1/Y1 latches */
        if (c->sprite.showing) {
            c->sprite.x = (int16_t)c->x[0];
            c->sprite.y = (int16_t)c->y[0];
            zz_sprite_refresh(c);
        }
        break;

    case 0x4a: /* SPRITE_COLORS: index = value, color = USER1<<16|USER2 */
        if (v < 4) {
            c->sprite.colors[v] = ((uint32_t)c->user[0] << 16) | c->user[1];
            if (v && c->sprite.colors[v] == 0x00ff00ffu)
                c->sprite.colors[v] = 0x00fe00feu;
            c->sprite.hw_gen++;  /* colour changed: route-A re-pushes overlay */
            zz_sprite_refresh(c);
        }
        break;

    case 0x5e:
        c->split_pos = (uint32_t)(int16_t)v;
        c->bgbuf_off = c->src_off;
        break;

    case 0x60: /* SET_FEATURE: id in USER1 */
        if (c->user[0] == 1)      c->feat_secondary_pal = v != 0;
        else if (c->user[0] == 3) c->feat_overlay = v != 0;
        else if (c->user[0] == 4) c->feat_dpms = v != 0;
        break;

    case ZZ_BLITQ_ANNOUNCE_REG:
        zz_blitq.armed = (v == ZZ_BLITQ_ARM_MAGIC);
        break;

    case 0xe8: c->config_key = v; break;

    case 0x1000: c->pal_data = (c->pal_data & 0xffffu) |
                               ((uint32_t)v << 16); break;
    case 0x1002: c->pal_data = (c->pal_data & 0xffff0000u) | v; break;
    case 0x1004: zz_palette_z2_commit(c, v); break;
    case 0x1006: /* SetSwitch: 1 = native passthrough, 0 = RTG */
        c->capture_mode = (v != 0);
        c->scanout.enabled = !c->capture_mode &&
                             c->scanout.width && c->scanout.height;
        c->scanout.dirty = 1;
        zz_sprite_refresh(c);
        break;

    default:
        break;
    }
}

uint16_t zz_card_reg_read16(struct zz_card *c, uint32_t off)
{
    if (off == ZZ_BLITQ_ANNOUNCE_REG) {
        if (zz_queue_pending(&zz_blitq))
            zz_queue_tick(&zz_blitq);       /* one bounded row-band slice */
#ifdef EMU68_A3D
        zz_blitq_settle(c);
        if (zz_queue_pending(&zz_blitq) && zz_blitq_try_cover(c))
            return 0;
#endif
        return (uint16_t)zz_blitq.count;    /* 0 = idle/complete (1b)        */
    }
#ifdef EMU68_A3D
    if (zz_queue_pending(&zz_blitq) && !(off == 0x4c && (zz_blitq_covered || zz_blitq_try_cover(c))))
        zz_queue_drain(&zz_blitq);
    zz_blitq_settle(c);
#else
    if (zz_queue_pending(&zz_blitq))
        zz_queue_drain(&zz_blitq);
#endif

    switch (off) {
    case 0xc0: return ZZ_FW_VERSION;
    case 0x4c: return c->vblank_read ? (uint16_t)c->vblank_read(c->ud) : 0;
    case 0x04: return c->eth_int_pending ? 1 : 0;
    case REG_ZZ_ETH_TX: return c->eth_tx_status;    /* last send result, 0=OK */
    case REG_ZZ_ETH_MAC_HI:  return (uint16_t)((c->eth_mac[0] << 8) | c->eth_mac[1]);
    case REG_ZZ_ETH_MAC_HI2: return (uint16_t)((c->eth_mac[2] << 8) | c->eth_mac[3]);
    case REG_ZZ_ETH_MAC_LO:  return (uint16_t)((c->eth_mac[4] << 8) | c->eth_mac[5]);
    case 0xe8: return c->config_key == 9 ? 1 : 0;   /* OFFSCREEN_BITMAPS on  */
    case 0xea: return c->config_key == 9 ? 1 : 0;
    default:
        if (off / 2 < sizeof(c->regs) / sizeof(c->regs[0]))
            return c->regs[off / 2];
        return 0;
    }
}

/* ---- 7. size-splitting MMIO wrappers ------------------------------------- */

void zz_card_mmio_write(struct zz_card *c, uint32_t off, uint32_t val,
                        unsigned size)
{
    if (size == 4) {                    /* ZZ9000OS main.c:572-584 folding   */
        zz_card_reg_write16(c, off, (uint16_t)(val >> 16));
        zz_card_reg_write16(c, off + 2, (uint16_t)val);
    } else if (size == 2) {
        zz_card_reg_write16(c, off, (uint16_t)val);
    } else {
        uint32_t base = off & ~1u;
        uint16_t cur = (base / 2 < sizeof(c->regs) / sizeof(c->regs[0]))
                       ? c->regs[base / 2] : 0;
        uint16_t merged = (off & 1)
                          ? (uint16_t)((cur & 0xff00u) | (val & 0xffu))
                          : (uint16_t)(((val & 0xffu) << 8) | (cur & 0xffu));
        zz_card_reg_write16(c, base, merged);
    }
}

uint32_t zz_card_mmio_read(struct zz_card *c, uint32_t off, unsigned size)
{
    if (size == 4)
        return ((uint32_t)zz_card_reg_read16(c, off) << 16) |
               zz_card_reg_read16(c, off + 2);
    if (size == 2)
        return zz_card_reg_read16(c, off);
    {
        uint16_t w = zz_card_reg_read16(c, off & ~1u);
        return (off & 1) ? (w & 0xffu) : (w >> 8);
    }
}

/* ---- init ---------------------------------------------------------------- */

void zz_card_init(struct zz_card *c, uint8_t *vram, uint32_t backing,
                  uint32_t heap_end_off)
{
    memset(c, 0, sizeof(*c));
    zz_queue_init(&zz_blitq, zz_card_blitq_exec, c);
#ifdef EMU68_A3D
    zz_queue_set_gate(&zz_blitq, zz_blitq_gate);
    zz_blitq_covered = 0;
#endif
    c->vram = vram;
    c->backing = backing;
    c->heap_end = heap_end_off;
    c->colormode = ZZ_CM_32BIT;        /* driver blitter-cache seed value    */
    c->sprite.colors[0] = 0x00ff00ffu; /* transparent key                    */
    c->composite_sprite = 1;
    set_fb_limit(vram + backing);
    set_fb((uint32_t *)(void *)(vram + ZZ_MEMBASE_OFF), 256);
}
