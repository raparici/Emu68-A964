/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the GPU's band-list command words. */

#ifndef A3D_RIX_H
#define A3D_RIX_H

#include <stdint.h>
#include <string.h>

#define RIX_OP_NOP              0x00000000u   /* no payload                                        */
#define RIX_OP_RENDER_CONFIG    0x10000000u   /* | register address; 1 payload word                */
#define RIX_OP_FRAMEBUFFER      0x20000000u   /* | flags | size << 8; no payload                   */
#define RIX_OP_TRIANGLE_STREAM  0x30000000u   /* | payload bytes; the triangle descriptor           */
#define RIX_OP_FOG_LUT_STREAM   0x40000000u   /* | 64 << 2; 64 payload words                        */
#define RIX_OP_TEXTURE_STREAM   0x50000000u   /* | tmu << 19 | page count; one word per page         */
#define A3D_OP_TRIANGLE_VERTS   0x60000000u   /* | ST_RAW | payload words (bits 7:0)                 */
#define A3D_TV_ST_RAW           0x00010000u
#define A3D_OP_LINK             0x70000000u
#define A3D_LINK_BYTES_MASK     0x0FFFFFFFu
#define RIX_OP_MASK             0xF0000000u

#define RIX_REG_FEATURE_ENABLE      0x00u
#define RIX_REG_CLEAR_COLOR         0x01u
#define RIX_REG_CLEAR_DEPTH         0x02u
#define RIX_REG_FRAGMENT_PIPELINE   0x03u
#define RIX_REG_STENCIL             0x04u
#define RIX_REG_FOG_COLOR           0x05u
#define RIX_REG_SCISSOR_START       0x06u
#define RIX_REG_SCISSOR_END         0x07u
#define RIX_REG_Y_OFFSET            0x08u   /* per band: the band's first row       */
#define RIX_REG_RENDER_RESOLUTION   0x09u
#define RIX_REG_TEX_ENV(t)          (0x0Au + 3u * (t))
#define RIX_REG_TEX_ENV_COLOR(t)    (0x0Bu + 3u * (t))
#define RIX_REG_TMU_TEXTURE(t)      (0x0Cu + 3u * (t))
#define RIX_REG_COLOR_BUFFER_ADDR   16u     /* per band: where the band's colour is loaded / committed */
#define RIX_REG_DEPTH_BUFFER_ADDR   17u
#define RIX_REG_STENCIL_BUFFER_ADDR 18u

#define RIX_FE_SCISSOR              (1u << 5)
#define RIX_FE_DITHER               (1u << 9)     /* G1: 4x4 ordered dither into the RGB565 band (W3D_DITHERING) */
#define RIX_FE_CLEAR_ON_COMMIT      (1u << 11)    /* G3: a COMMIT clears the band behind its write-out */
#define RIX_FP_DEPTH_MASK           (1u << 14)
#define RIX_FP_COLOR_MASKS          (0xFu << 15)

#define RIX_FB_COMMIT       0x01u
#define RIX_FB_MEMSET       0x02u
#define RIX_FB_SWAP         0x04u
#define RIX_FB_LOAD         0x08u
#define RIX_FB_COLOR        0x10u
#define RIX_FB_DEPTH        0x20u
#define RIX_FB_STENCIL      0x40u
#define RIX_FB_VSYNC        0x80u
#define RIX_FB_SIZE_SHIFT   8
#define RIX_FB_SIZE_MASK    0xFFFFFu
#define RIX_FB_SIZE_ALIGN   4096u           /* the library rounds sizes up to its texture page (pixels) */

#define RIX_FOG_LUT_WORDS   64u
#define RIX_TEX_TMU_SHIFT   19
#define RIX_TEX_TMU_MASK    0x3u
#define RIX_TEX_PAGES_MASK  0x7FFFFu

/* an x/y register value (BaseXYReg): y in the high half, x in the low half */
static inline uint32_t rix_xy(uint32_t x, uint32_t y)
{
    return ((y & 0xFFFFu) << 16) | (x & 0xFFFFu);
}

/* a FRAMEBUFFER command: flags = RIX_FB_*; size_px rounded up as RasterIX's library rounds it */
static inline uint32_t rix_fb_word(uint32_t flags, uint32_t size_px)
{
    uint32_t s = size_px & 0x7FFFFFFFu;
    if (s & (RIX_FB_SIZE_ALIGN - 1u))
        s = (s | (RIX_FB_SIZE_ALIGN - 1u)) + 1u;
    return RIX_OP_FRAMEBUFFER | (flags & 0xFFu) | ((s & RIX_FB_SIZE_MASK) << RIX_FB_SIZE_SHIFT);
}

static inline uint32_t rix_fb_word_exact(uint32_t flags, uint32_t size_px)
{
    return RIX_OP_FRAMEBUFFER | (flags & 0xFFu) | ((size_px & RIX_FB_SIZE_MASK) << RIX_FB_SIZE_SHIFT);
}

static inline uint32_t rix_tex_op(uint32_t tmu, uint32_t pages)
{
    return RIX_OP_TEXTURE_STREAM | ((tmu & RIX_TEX_TMU_MASK) << RIX_TEX_TMU_SHIFT) | (pages & RIX_TEX_PAGES_MASK);
}

static inline uint32_t rix_fog_op(void)
{
    return RIX_OP_FOG_LUT_STREAM | (RIX_FOG_LUT_WORDS << 2);
}

#ifndef RIX_STORE_SWAP
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define RIX_STORE_SWAP 1
#else
#define RIX_STORE_SWAP 0
#endif
#endif

static inline void rix_put32(uint8_t *p, uint32_t v)
{
#if RIX_STORE_SWAP
    v = __builtin_bswap32(v);
#endif
    __builtin_memcpy(p, &v, 4);
}

static inline uint32_t rix_get32(const uint8_t *p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
#if RIX_STORE_SWAP
    v = __builtin_bswap32(v);
#endif
    return v;
}

/* one band's display list: a word-aligned byte buffer the fabric reads as little-endian words */
typedef struct rix_list
{
    uint8_t *mem;
    uint32_t cap;   /* bytes */
    uint32_t pos;   /* bytes written */
    int (*more)(struct rix_list *l, uint32_t need);
    void *owner;
} rix_list;

static inline int rix_emit(rix_list *l, uint32_t op, const uint32_t *payload, uint32_t n)
{
    const uint32_t need = 4u * (1u + n);
    if (need > l->cap - l->pos && (!l->more || !l->more(l, need) || need > l->cap - l->pos))
        return 0;
    uint8_t *p = l->mem + l->pos;
    rix_put32(p, op);
    for (uint32_t i = 0; i < n; i++)
        rix_put32(p + 4u + 4u * i, payload[i]);
    l->pos += need;
    return 1;
}

#endif /* A3D_RIX_H */
