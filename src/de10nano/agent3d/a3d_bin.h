/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the per-band binner. */

#ifndef A3D_BIN_H
#define A3D_BIN_H

#include "a3d_rix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define A3D_MAX_BANDS   64u
#define A3D_MAX_TMUS    2u
#define A3D_MAX_PAGES   64u     /* pages per texture stream: a 256x256 RGB565 mip chain is 43 pages of 4 KB */
#define A3D_ITEMS       32u     /* item numbers: registers 0..15, texture streams 24 + tmu, fog table 28 */
#define A3D_ITEM_TEX(t) (24u + (t))
#define A3D_ITEM_FOG    28u

#define A3D_EAGER       0u
#define A3D_LAZY        1u

typedef struct a3d_tex
{
    uint32_t op;                        /* the TEXTURE_STREAM header (tmu, page count) */
    uint32_t n;
    uint32_t page[A3D_MAX_PAGES];
} a3d_tex;

typedef struct a3d_band
{
    rix_list list;
    /* what this band holds in its current list (lazy mode) */
    uint32_t seen;                      /* the state generation it was last brought up to   */
    uint32_t valid;                     /* items it has received in this list (bit = item)   */
    uint32_t val[16];
    uint32_t fog[RIX_FOG_LUT_WORDS];
    a3d_tex tex[A3D_MAX_TMUS];
    uint32_t opt_pending;               /* TMUs with a stream since the band's last triangle */
    uint8_t *opt_ptr[A3D_MAX_TMUS];
    uint32_t opt_size[A3D_MAX_TMUS];
    uint32_t overflow;                  /* a command did not fit in this list */
} a3d_band;

typedef struct a3d_stats
{
    uint64_t changes;                   /* logical state changes                                     */
    uint64_t checks;                    /* band flush checks (lazy)                                  */
    uint64_t dirty_checks;              /* ... with at least one changed item to compare             */
    uint64_t deltas;                    /* state commands a lazy flush emitted                       */
    uint64_t dropped;
    uint64_t list_full;                 /* commands that did not fit their band list                 */
    uint64_t errors;                    /* rejected input (bad TMU, too many pages, ...)             */
} a3d_stats;

typedef struct a3d_binner
{
    uint32_t mode;                      /* A3D_LAZY or A3D_EAGER                                  */
    uint32_t tmus;                      /* texture units per raster unit (1 in F0c's GPU)         */
    uint32_t max_bands;
    uint32_t band_pixels;               /* pixels a band holds (2^14)                             */
    uint32_t gram;                      /* added to buffer addresses in registers                 */
    uint32_t drop_every;
    /* geometry */
    uint32_t bands;
    uint32_t res_x;
    uint32_t band_lines;
    uint32_t res_y;                     /* product geometry: the target's height (last band may be short) */
    uint32_t lines_recip;
    uint32_t recip_of;
    uint32_t list_pf;
    /* the logical state */
    uint32_t gen;
    uint32_t item_gen[A3D_ITEMS];       /* generation of each item's last change; 0 = never set   */
    uint32_t val[16];
    uint32_t fog[RIX_FOG_LUT_WORDS];
    a3d_tex tex[A3D_MAX_TMUS];
    uint32_t order[16];                 /* register flush order                                   */
    uint32_t norder;
    uint32_t color_addr, depth_addr, stencil_addr;
    uint32_t scissor_on;
    int32_t scissor_y0, scissor_y1;
    a3d_stats stats;
    a3d_band band[A3D_MAX_BANDS];
} a3d_binner;

/* set-up */
void a3d_bin_init(a3d_binner *b, uint32_t mode, uint32_t tmus, uint32_t max_bands, uint32_t band_pixels,
                  uint32_t gram);
void a3d_bin_set_list(a3d_binner *b, uint32_t band, uint8_t *mem, uint32_t cap);
void a3d_bin_set_geometry(a3d_binner *b, uint32_t bands, uint32_t res_x, uint32_t band_lines);
int a3d_bin_set_resolution(a3d_binner *b, uint32_t x, uint32_t y);

/* list boundaries */
void a3d_bin_begin_list(a3d_binner *b);         /* a new list set: every band's shadow is unknown        */
void a3d_bin_reset_lists(a3d_binner *b);        /* the lists were handed to the fabric: empty them       */
int a3d_bin_overflowed(const a3d_binner *b);

/* state */
void a3d_bin_reg(a3d_binner *b, uint32_t addr, uint32_t value);
void a3d_bin_fog(a3d_binner *b, const uint32_t lut[RIX_FOG_LUT_WORDS]);
void a3d_bin_tex(a3d_binner *b, uint32_t op, const uint32_t *pages, uint32_t n);
void a3d_bin_nop(a3d_binner *b);
void a3d_bin_y_offsets(a3d_binner *b);

void a3d_bin_framebuffer(a3d_binner *b, uint32_t fb_flags);

int a3d_bin_geometry_product(a3d_binner *b, uint32_t width, uint32_t height);
uint32_t a3d_bin_pick_lines(uint32_t width, uint32_t height, uint32_t band_pixels, uint32_t max_bands, uint32_t c11);
/* the same as a3d_bin_geometry_product, the band height from a3d_bin_pick_lines */
int a3d_bin_geometry_c11(a3d_binner *b, uint32_t width, uint32_t height, uint32_t c11);
uint32_t a3d_bin_band_rows(const a3d_binner *b, uint32_t band);
uint32_t a3d_bin_band_y0(const a3d_binner *b, uint32_t band);
void a3d_bin_band_header(a3d_binner *b, uint32_t band, uint32_t color_base, uint32_t depth_base);
void a3d_bin_band_fb(a3d_binner *b, uint32_t band, uint32_t fb_flags);

uint32_t a3d_bin_band_of_row(const a3d_binner *b, uint32_t y);
/* triangles: the bands a triangle whose bounding box covers rows y0..y1 touches (0 = none) */
int a3d_bin_tri_bands(const a3d_binner *b, uint32_t y0, uint32_t y1, uint32_t *first, uint32_t *last);
/* one band's copy: flush the band, then append the command (TRIANGLE_STREAM or TRIANGLE_VERTS) */
void a3d_bin_tri_band(a3d_binner *b, uint32_t band, uint32_t op, const uint32_t *payload, uint32_t n);
void a3d_bin_triangle(a3d_binner *b, uint32_t y0, uint32_t y1, uint32_t op, const uint32_t *payload,
                      uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* A3D_BIN_H */
