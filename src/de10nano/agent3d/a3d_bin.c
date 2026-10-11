/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the per-band binner. */

#include "a3d_bin.h"

/* the eight non-TMU state registers, in the order a band list starts with (RasterIX's RenderState) */
static const uint32_t a3d_order0[8] = {
    RIX_REG_CLEAR_COLOR, RIX_REG_CLEAR_DEPTH, RIX_REG_FEATURE_ENABLE, RIX_REG_FOG_COLOR,
    RIX_REG_FRAGMENT_PIPELINE, RIX_REG_SCISSOR_END, RIX_REG_SCISSOR_START, RIX_REG_STENCIL,
};

static int a3d_tracked(uint32_t addr)
{
    return addr <= 15u && addr != RIX_REG_Y_OFFSET && addr != RIX_REG_RENDER_RESOLUTION;
}

void a3d_bin_init(a3d_binner *b, uint32_t mode, uint32_t tmus, uint32_t max_bands, uint32_t band_pixels,
                  uint32_t gram)
{
    memset(b, 0, sizeof(*b));
    b->mode = mode;
    b->tmus = tmus > A3D_MAX_TMUS ? A3D_MAX_TMUS : tmus;
    b->max_bands = max_bands > A3D_MAX_BANDS ? A3D_MAX_BANDS : max_bands;
    b->band_pixels = band_pixels;
    b->gram = gram;
    b->bands = b->max_bands;
    for (uint32_t k = 0; k < 8; k++)
        b->order[b->norder++] = a3d_order0[k];
    for (uint32_t t = 0; t < b->tmus; t++)
    {
        b->order[b->norder++] = RIX_REG_TEX_ENV_COLOR(t);
        b->order[b->norder++] = RIX_REG_TEX_ENV(t);
        b->order[b->norder++] = RIX_REG_TMU_TEXTURE(t);
    }
    b->gen = 1;
}

void a3d_bin_set_list(a3d_binner *b, uint32_t band, uint8_t *mem, uint32_t cap)
{
    if (band >= A3D_MAX_BANDS)
        return;
    b->band[band].list.mem = mem;
    b->band[band].list.cap = cap & ~3u;
    b->band[band].list.pos = 0;
    b->band[band].list.more = 0;    /* one buffer, all or nothing (the replay's mode) */
    b->band[band].list.owner = 0;
}

static void a3d_bin_lines(a3d_binner *b, uint32_t l)
{
    b->band_lines = l;
    b->lines_recip = l >= 2u ? 0xFFFFFFFFu / l + 1u : 0u;
    b->recip_of = l;
}

static inline uint32_t a3d_row_band(const a3d_binner *b, uint32_t y)
{
    const uint32_t l = b->band_lines;
    if (l == b->recip_of && l >= 2u && (y | l) < 0x10000u)
        return (uint32_t)(((uint64_t)y * b->lines_recip) >> 32);
    return y / l;
}

uint32_t a3d_bin_band_of_row(const a3d_binner *b, uint32_t y)
{
    return a3d_row_band(b, y);
}

void a3d_bin_set_geometry(a3d_binner *b, uint32_t bands, uint32_t res_x, uint32_t band_lines)
{
    b->bands = bands > b->max_bands ? b->max_bands : bands;
    b->res_x = res_x;
    a3d_bin_lines(b, band_lines);
}

/* ---- the band writer ------------------------------------------------------------------------------- */

static int a3d_add(a3d_binner *b, uint32_t i, uint32_t op, const uint32_t *payload, uint32_t n)
{
    a3d_band *d = &b->band[i];
    uint32_t kind = op & RIX_OP_MASK;
    /* RasterIX's TextureLoadOptimizer runs before the add, as in DisplayListAssembler::addCommand() */
    if (kind == RIX_OP_TRIANGLE_STREAM || kind == A3D_OP_TRIANGLE_VERTS)
    {
        d->opt_pending = 0;
    }
    else if (kind == RIX_OP_TEXTURE_STREAM)
    {
        const uint32_t t = (op >> RIX_TEX_TMU_SHIFT) & RIX_TEX_TMU_MASK;
        if (t < b->tmus)
        {
            if (d->opt_pending & (1u << t))
                memset(d->opt_ptr[t], 0, d->opt_size[t]);   /* NOP words */
            d->opt_pending &= ~(1u << t);                 /* re-armed below, once the stream is stored */
        }
        else
            kind = 0;   /* a stream for a TMU the build lacks: not tracked */
    }
    if (!rix_emit(&d->list, op, payload, n))
    {
        d->overflow = 1;
        b->stats.list_full++;
        return 0;
    }
    if (kind == RIX_OP_TRIANGLE_STREAM || kind == A3D_OP_TRIANGLE_VERTS)
    {
        const uint32_t sz = 4u * (1u + n);
        if (b->list_pf && sz <= d->list.cap - d->list.pos)
        {
            const uintptr_t q = (uintptr_t)(d->list.mem + d->list.pos);
            for (uintptr_t a = q & ~(uintptr_t)31u; a < q + sz; a += 32u)
                __builtin_prefetch((const void *)a, 1);
        }
    }
    if (kind == RIX_OP_TEXTURE_STREAM)
    {
        /* recorded AFTER the store: with staging chunks the command may have landed in a fresh chunk */
        const uint32_t t = (op >> RIX_TEX_TMU_SHIFT) & RIX_TEX_TMU_MASK;
        const uint32_t sz = 4u * (1u + n);
        d->opt_pending |= 1u << t;
        d->opt_ptr[t] = d->list.mem + d->list.pos - sz;
        d->opt_size[t] = sz;
    }
    return 1;
}

static void a3d_broadcast(a3d_binner *b, uint32_t op, const uint32_t *payload, uint32_t n)
{
    for (uint32_t i = 0; i < b->bands; i++)
        a3d_add(b, i, op, payload, n);
}

/* ---- lazy state ------------------------------------------------------------------------------------ */

static void a3d_mark(a3d_binner *b, uint32_t item)
{
    b->stats.changes++;
    b->item_gen[item] = ++b->gen;
}

static void a3d_delta(a3d_binner *b, uint32_t i, uint32_t op, const uint32_t *payload, uint32_t n)
{
    b->stats.deltas++;
    if (b->drop_every && ((uint32_t)b->stats.deltas % b->drop_every) == 0)
    {
        b->stats.dropped++;     /* the band's shadow already says it has it: a lost state change */
        return;
    }
    a3d_add(b, i, op, payload, n);
}

static int a3d_words_eq(const uint32_t *x, const uint32_t *y, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static int a3d_tex_same(const a3d_tex *x, const a3d_tex *y)
{
    return x->op == y->op && x->n == y->n && a3d_words_eq(x->page, y->page, x->n);
}

/* bring band i up to the logical state: only the items its next command could read differently */
static void a3d_flush(a3d_binner *b, uint32_t i)
{
    if (b->mode != A3D_LAZY || i >= b->max_bands)
        return;
    a3d_band *d = &b->band[i];
    b->stats.checks++;
    const uint32_t seen = d->seen;
    if (seen == b->gen)
        return;                 /* nothing changed since this band's last flush */
    d->seen = b->gen;
    int dirty = 0;
    for (uint32_t k = 0; k < b->norder; k++)
    {
        const uint32_t a = b->order[k];
        if (b->item_gen[a] <= seen)
            continue;
        dirty = 1;
        if ((d->valid & (1u << a)) && d->val[a] == b->val[a])
            continue;
        d->val[a] = b->val[a];
        d->valid |= 1u << a;
        a3d_delta(b, i, RIX_OP_RENDER_CONFIG | a, &b->val[a], 1);
    }
    if (b->item_gen[A3D_ITEM_FOG] > seen)
    {
        dirty = 1;
        if (!(d->valid & (1u << A3D_ITEM_FOG)) || !a3d_words_eq(d->fog, b->fog, RIX_FOG_LUT_WORDS))
        {
            memcpy(d->fog, b->fog, sizeof(b->fog));
            d->valid |= 1u << A3D_ITEM_FOG;
            a3d_delta(b, i, rix_fog_op(), b->fog, RIX_FOG_LUT_WORDS);
        }
    }
    for (uint32_t t = 0; t < b->tmus; t++)
    {
        const uint32_t item = A3D_ITEM_TEX(t);
        if (b->item_gen[item] <= seen)
            continue;
        dirty = 1;
        if (!(d->valid & (1u << item)) || !a3d_tex_same(&d->tex[t], &b->tex[t]))
        {
            d->tex[t] = b->tex[t];
            d->valid |= 1u << item;
            a3d_delta(b, i, b->tex[t].op, b->tex[t].page, b->tex[t].n);
        }
    }
    if (dirty)
        b->stats.dirty_checks++;
}

/* ---- list boundaries ------------------------------------------------------------------------------- */

void a3d_bin_begin_list(a3d_binner *b)
{
    if (b->mode != A3D_LAZY)
        return;
    /* renormalise the generations so they only grow within one list */
    b->gen = 1;
    for (uint32_t a = 0; a < A3D_ITEMS; a++)
        b->item_gen[a] = b->item_gen[a] ? 1u : 0u;
    for (uint32_t i = 0; i < A3D_MAX_BANDS; i++)
    {
        b->band[i].seen = 0;
        b->band[i].valid = 0;
    }
}

void a3d_bin_reset_lists(a3d_binner *b)
{
    for (uint32_t i = 0; i < b->bands; i++)
    {
        b->band[i].list.pos = 0;
        b->band[i].opt_pending = 0;
        b->band[i].overflow = 0;
    }
}

int a3d_bin_overflowed(const a3d_binner *b)
{
    for (uint32_t i = 0; i < b->bands; i++)
        if (b->band[i].overflow)
            return 1;
    return 0;
}

void a3d_bin_reg(a3d_binner *b, uint32_t addr, uint32_t value)
{
    /* the binner's own copies, which the per-band framebuffer commands need */
    switch (addr)
    {
    case RIX_REG_COLOR_BUFFER_ADDR:   b->color_addr = value - b->gram; break;
    case RIX_REG_DEPTH_BUFFER_ADDR:   b->depth_addr = value - b->gram; break;
    case RIX_REG_STENCIL_BUFFER_ADDR: b->stencil_addr = value - b->gram; break;
    case RIX_REG_FEATURE_ENABLE:      b->scissor_on = (value & RIX_FE_SCISSOR) != 0; break;
    case RIX_REG_SCISSOR_START:       b->scissor_y0 = (int32_t)(value >> 16); break;
    case RIX_REG_SCISSOR_END:         b->scissor_y1 = (int32_t)(value >> 16); break;
    default: break;
    }
    if (b->mode == A3D_LAZY && a3d_tracked(addr))
    {
        b->val[addr] = value;
        a3d_mark(b, addr);
        return;
    }
    a3d_broadcast(b, RIX_OP_RENDER_CONFIG | addr, &value, 1);
}

void a3d_bin_fog(a3d_binner *b, const uint32_t lut[RIX_FOG_LUT_WORDS])
{
    if (b->mode == A3D_LAZY)
    {
        memcpy(b->fog, lut, sizeof(b->fog));
        a3d_mark(b, A3D_ITEM_FOG);
        return;
    }
    a3d_broadcast(b, rix_fog_op(), lut, RIX_FOG_LUT_WORDS);
}

void a3d_bin_tex(a3d_binner *b, uint32_t op, const uint32_t *pages, uint32_t n)
{
    const uint32_t t = (op >> RIX_TEX_TMU_SHIFT) & RIX_TEX_TMU_MASK;
    if (b->mode == A3D_LAZY)
    {
        if (t >= b->tmus)
            return;                     /* as G4: a stream for a TMU the build lacks is ignored */
        if (n > A3D_MAX_PAGES)
        {
            b->stats.errors++;
            return;
        }
        b->tex[t].op = op;
        b->tex[t].n = n;
        memcpy(b->tex[t].page, pages, 4u * n);
        a3d_mark(b, A3D_ITEM_TEX(t));
        return;
    }
    a3d_broadcast(b, op, pages, n);
}

void a3d_bin_nop(a3d_binner *b)
{
    a3d_broadcast(b, RIX_OP_NOP, 0, 0);
}

void a3d_bin_y_offsets(a3d_binner *b)
{
    for (uint32_t i = 0; i < b->bands; i++)
    {
        const uint32_t v = ((i * b->band_lines) & 0x7FFu) << 16;
        a3d_add(b, i, RIX_OP_RENDER_CONFIG | RIX_REG_Y_OFFSET, &v, 1);
    }
}

int a3d_bin_set_resolution(a3d_binner *b, uint32_t x, uint32_t y)
{
    const uint32_t px = x * y;
    const uint32_t bands = px / b->band_pixels + ((px % b->band_pixels) ? 1u : 0u);
    if (bands == 0 || bands > b->max_bands)
        return 0;
    b->bands = bands;
    b->res_x = x;
    a3d_bin_lines(b, y / bands);
    const uint32_t v = ((b->band_lines & 0x7FFu) << 16) | (x & 0xFFFFu);
    a3d_broadcast(b, RIX_OP_RENDER_CONFIG | RIX_REG_RENDER_RESOLUTION, &v, 1);
    return 1;
}

static void a3d_band_addresses(a3d_binner *b, uint32_t reg, uint32_t base)
{
    const uint32_t bytes = b->band_lines * b->res_x * 2u;
    for (uint32_t i = 0; i < b->bands; i++)
    {
        const uint32_t v = base + bytes * (b->bands - i - 1u) + b->gram;
        a3d_add(b, i, RIX_OP_RENDER_CONFIG | reg, &v, 1);
    }
}

void a3d_bin_framebuffer(a3d_binner *b, uint32_t fb_flags)
{
    const uint32_t sel = fb_flags & (RIX_FB_COLOR | RIX_FB_DEPTH | RIX_FB_STENCIL);
    const uint32_t px = b->res_x * b->band_lines;
    if (fb_flags & RIX_FB_SWAP)
    {
        /* RasterIX's "last line" is list 0 (the lists run bottom-up for its stream display) */
        const uint32_t v = b->color_addr + b->gram;
        a3d_add(b, 0, RIX_OP_RENDER_CONFIG | RIX_REG_COLOR_BUFFER_ADDR, &v, 1);
        a3d_flush(b, 0);
        a3d_add(b, 0, rix_fb_word(sel | RIX_FB_SWAP | (fb_flags & RIX_FB_VSYNC), px * b->bands), 0, 0);
        return;
    }
    a3d_band_addresses(b, RIX_REG_COLOR_BUFFER_ADDR, b->color_addr);
    a3d_band_addresses(b, RIX_REG_DEPTH_BUFFER_ADDR, b->depth_addr);
    a3d_band_addresses(b, RIX_REG_STENCIL_BUFFER_ADDR, b->stencil_addr);
    uint32_t action;
    if (fb_flags & RIX_FB_MEMSET)
        action = RIX_FB_MEMSET;
    else if (fb_flags & RIX_FB_COMMIT)
        action = RIX_FB_COMMIT;
    else if (fb_flags & RIX_FB_LOAD)
        action = RIX_FB_LOAD;
    else
    {
        b->stats.errors++;
        return;
    }
    const uint32_t w = rix_fb_word(sel | action, px);
    for (uint32_t i = 0; i < b->bands; i++)
    {
        if (action == RIX_FB_MEMSET && b->scissor_on)
        {
            const int32_t start = (int32_t)(i * b->band_lines);
            const int32_t end = (int32_t)((i + 1u) * b->band_lines);
            if (!(end >= b->scissor_y0 && start < b->scissor_y1))
                continue;               /* the band lies outside the scissor rows: no clear */
        }
        a3d_flush(b, i);
        a3d_add(b, i, w, 0, 0);
    }
}

int a3d_bin_tri_bands(const a3d_binner *b, uint32_t y0, uint32_t y1, uint32_t *first, uint32_t *last)
{
    /* RasterIX: band i (rows i*L .. i*L+L-1) holds the triangle when y1 >= i*L and y0 < (i+1)*L */
    if (b->band_lines == 0 || b->bands == 0)
        return 0;
    const uint32_t f = a3d_row_band(b, y0);
    uint32_t l = a3d_row_band(b, y1);
    if (l >= b->bands)
        l = b->bands - 1u;
    if (f > l)
        return 0;
    *first = f;
    *last = l;
    return 1;
}

void a3d_bin_tri_band(a3d_binner *b, uint32_t band, uint32_t op, const uint32_t *payload, uint32_t n)
{
    if (band >= b->bands)
        return;
    a3d_flush(b, band);
    a3d_add(b, band, op, payload, n);
}

void a3d_bin_triangle(a3d_binner *b, uint32_t y0, uint32_t y1, uint32_t op, const uint32_t *payload,
                      uint32_t n)
{
    uint32_t f, l;
    if (!a3d_bin_tri_bands(b, y0, y1, &f, &l))
        return;
    for (uint32_t i = f; i <= l; i++)
        a3d_bin_tri_band(b, i, op, payload, n);
}

static uint32_t a3d_product_lines(uint32_t width, uint32_t height, uint32_t band_pixels)
{
    const uint32_t lmax = band_pixels / width;
    uint32_t l = lmax < height ? lmax : height;
    while (l > 1 && (height % l) != 0)
        l--;
    if (l * 2u < (lmax < height ? lmax : height))
        l = lmax < height ? lmax : height;      /* no good divisor: full bands and a short last one */
    return l;
}

/* C11: every band of l rows, and the last one, a whole number of 64-pixel bursts */
static int a3d_c11_lines_ok(uint32_t width, uint32_t height, uint32_t l)
{
    const uint32_t bands = (height + l - 1u) / l;
    const uint32_t last = height - (bands - 1u) * l;
    return (width * l) % 64u == 0u && (width * last) % 64u == 0u;
}

uint32_t a3d_bin_pick_lines(uint32_t width, uint32_t height, uint32_t band_pixels, uint32_t max_bands, uint32_t c11)
{
    if (width == 0 || height == 0 || width > 0x7FFu || height > 0x7FFu || band_pixels < width)
        return 0;
    uint32_t l = a3d_product_lines(width, height, band_pixels);
    if (c11 && (width % 4u != 0u || !a3d_c11_lines_ok(width, height, l)))
    {
        if (width % 4u != 0u)
            return 0;                           /* the band width itself breaks C11: no height helps */
        const uint32_t lmax = band_pixels / width < height ? band_pixels / width : height;
        l = 0;
        for (uint32_t k = lmax; k >= 1u; k--)
            if (a3d_c11_lines_ok(width, height, k))
            {
                l = k;
                break;
            }
        if (l == 0)
            return 0;
    }
    const uint32_t bands = (height + l - 1u) / l;
    if (bands == 0 || bands > max_bands)
        return 0;
    return l;
}

int a3d_bin_geometry_c11(a3d_binner *b, uint32_t width, uint32_t height, uint32_t c11)
{
    const uint32_t l = a3d_bin_pick_lines(width, height, b->band_pixels, b->max_bands, c11);
    if (l == 0)
        return 0;
    b->bands = (height + l - 1u) / l;
    b->res_x = width;
    b->res_y = height;
    a3d_bin_lines(b, l);
    return 1;
}

int a3d_bin_geometry_product(a3d_binner *b, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > 0x7FFu || height > 0x7FFu || b->band_pixels < width)
        return 0;
    const uint32_t lmax = b->band_pixels / width;
    uint32_t l = lmax < height ? lmax : height;
    while (l > 1 && (height % l) != 0)
        l--;
    if (l * 2u < (lmax < height ? lmax : height))
        l = lmax < height ? lmax : height;      /* no good divisor: full bands and a short last one */
    const uint32_t bands = (height + l - 1u) / l;
    if (bands == 0 || bands > b->max_bands)
        return 0;
    b->bands = bands;
    b->res_x = width;
    b->res_y = height;
    a3d_bin_lines(b, l);
    return 1;
}

uint32_t a3d_bin_band_y0(const a3d_binner *b, uint32_t band)
{
    return band * b->band_lines;
}

uint32_t a3d_bin_band_rows(const a3d_binner *b, uint32_t band)
{
    const uint32_t y0 = band * b->band_lines;
    if (b->res_y == 0 || band + 1u < b->bands)
        return b->band_lines;
    return y0 < b->res_y ? b->res_y - y0 : 0u;
}

void a3d_bin_band_header(a3d_binner *b, uint32_t band, uint32_t color_base, uint32_t depth_base)
{
    if (band >= b->bands)
        return;
    const uint32_t y0 = a3d_bin_band_y0(b, band);
    const uint32_t rows = a3d_bin_band_rows(b, band);
    const uint32_t off = y0 * b->res_x * 2u;
    uint32_t v;
    v = ((rows & 0x7FFu) << 16) | (b->res_x & 0x7FFu);
    a3d_add(b, band, RIX_OP_RENDER_CONFIG | RIX_REG_RENDER_RESOLUTION, &v, 1);
    v = ((b->res_y - y0 - rows) & 0x7FFu) << 16;
    a3d_add(b, band, RIX_OP_RENDER_CONFIG | RIX_REG_Y_OFFSET, &v, 1);
    v = color_base + off;
    a3d_add(b, band, RIX_OP_RENDER_CONFIG | RIX_REG_COLOR_BUFFER_ADDR, &v, 1);
    v = depth_base + off;
    a3d_add(b, band, RIX_OP_RENDER_CONFIG | RIX_REG_DEPTH_BUFFER_ADDR, &v, 1);
}

void a3d_bin_band_fb(a3d_binner *b, uint32_t band, uint32_t fb_flags)
{
    if (band >= b->bands)
        return;
    a3d_flush(b, band);
    a3d_add(b, band, rix_fb_word_exact(fb_flags, a3d_bin_band_rows(b, band) * b->res_x), 0, 0);
}
