/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: glReadPixels. */

#include <string.h>
#include "mgl_core.h"

void mgl_seterr(GLcontext c, GLenum e);        /* mgl_state.c */

int (*mgl_rb_lock_fn)(GLcontext c, mgl_rb *rb) = 0;     /* the platform's (src/mgl_lib.c sets them at the start) */
void (*mgl_rb_unlock_fn)(GLcontext c) = 0;

static uint32_t ncomp(GLenum f)
{
    switch (f)
    {
    case GL_RED: case GL_GREEN: case GL_BLUE: case GL_ALPHA: case GL_LUMINANCE: return 1;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_RGB: return 3;
    case GL_RGBA: return 4;
    default: return 0;
    }
}

static uint32_t csize(GLenum t)
{
    switch (t)
    {
    case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
    case GL_UNSIGNED_SHORT: case GL_SHORT: return 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return 4;
    default: return 0;
    }
}

/* n / d (n <= d) scaled to [0, m], rounded -- in 32 bits: m = q d + r */
static uint32_t scale(uint32_t n, uint32_t d, uint32_t m)
{
    const uint32_t q = m / d, r = m % d;
    return n * q + (n * r + d / 2u) / d;
}

/* one component n / d into the program's memory as the type says */
static void put(uint8_t *o, GLenum type, uint32_t n, uint32_t d)
{
    switch (type)
    {
    case GL_UNSIGNED_BYTE: *o = (uint8_t)scale(n, d, 255u); break;
    case GL_BYTE:          *(int8_t *)o = (int8_t)scale(n, d, 127u); break;
    case GL_UNSIGNED_SHORT: { const uint16_t v = (uint16_t)scale(n, d, 65535u); memcpy(o, &v, 2); break; }
    case GL_SHORT:          { const int16_t v = (int16_t)scale(n, d, 32767u); memcpy(o, &v, 2); break; }
    case GL_UNSIGNED_INT:   { const uint32_t v = scale(n, d, 0xFFFFFFFFu); memcpy(o, &v, 4); break; }
    case GL_INT:            { const int32_t v = (int32_t)scale(n, d, 0x7FFFFFFFu); memcpy(o, &v, 4); break; }
    default:                { const float v = (float)n / (float)d; memcpy(o, &v, 4); break; }       /* GL_FLOAT */
    }
}

int mgl_readpx_convert(const mgl_rb *rb, int32_t win_h, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                       GLenum type, const mgl_pack *pk, void *pixels)
{
    if (w < 0 || h < 0)
        return GL_INVALID_VALUE;
    if (format == GL_COLOR_INDEX)
        return GL_INVALID_OPERATION;            /* no colour index buffer (stencil: no such format here -- INVALID_ENUM) */
    const int packed = type == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_4_4_4_4;
    if (packed && format != (type == MGL_UNSIGNED_SHORT_5_6_5 ? (GLenum)GL_RGB : (GLenum)GL_RGBA))
        return format == GL_DEPTH_COMPONENT || ncomp(format) ? GL_INVALID_OPERATION : GL_INVALID_ENUM;
    if (format != GL_DEPTH_COMPONENT && !ncomp(format))
        return GL_INVALID_ENUM;
    if (!packed && !csize(type))
        return GL_INVALID_ENUM;
    if (format == GL_DEPTH_COMPONENT || !w || !h || !pixels || !rb || !rb->base)
        return 0;                               /* (the depth buffer is the renderer's: nothing to read) */
    const uint32_t n = packed ? 1u : ncomp(format), s = packed ? 2u : csize(type), es = n * s;
    const uint32_t row = pk->row > 0 ? (uint32_t)pk->row : (uint32_t)w;
    const uint32_t a = pk->align == 1 || pk->align == 2 || pk->align == 8 ? (uint32_t)pk->align : 4u;
    const uint32_t stride = s >= a ? es * row : a * ((es * row + a - 1u) / a);
    uint8_t *dst0 = (uint8_t *)pixels + (uint32_t)(pk->skip_rows > 0 ? pk->skip_rows : 0) * stride +
                    (uint32_t)(pk->skip_pixels > 0 ? pk->skip_pixels : 0) * es;
    uint8_t lut5[32], lut6[64];                 /* the common case's components, ready (UNSIGNED_BYTE) */
    for (uint32_t k = 0; k < 32u; k++)
        lut5[k] = (uint8_t)scale(k, 31u, 255u);
    for (uint32_t k = 0; k < 64u; k++)
        lut6[k] = (uint8_t)scale(k, 63u, 255u);
    const int32_t fbw = (int32_t)rb->width, fbh = (int32_t)(rb->height < (uint32_t)win_h || win_h <= 0 ? rb->height : (uint32_t)win_h);
    for (int32_t j = 0; j < h; j++)
    {
        const int32_t gy = y + j, fy = (win_h > 0 ? win_h : fbh) - 1 - gy;         /* GL's rows bottom-up */
        if (gy < 0 || fy < 0 || fy >= fbh)
            continue;
        const uint8_t *src = rb->base + (uint32_t)fy * rb->pitch;
        uint8_t *o = dst0 + (uint32_t)j * stride;
        for (int32_t i = 0; i < w; i++, o += es)
        {
            const int32_t fx = x + i;
            if (fx < 0 || fx >= fbw)
                continue;
            uint32_t v = ((uint32_t)src[2 * fx] << 8) | src[2 * fx + 1];
            if (rb->swapped)
                v = ((v & 0xFFu) << 8) | (v >> 8);
            const uint32_t r = v >> 11, g = (v >> 5) & 63u, b = v & 31u;
            if (packed)
            {
                const uint16_t p16 = type == MGL_UNSIGNED_SHORT_5_6_5 ? (uint16_t)v
                                   : (uint16_t)((r >> 1) << 12 | (g >> 2) << 8 | (b >> 1) << 4 | 15u);
                memcpy(o, &p16, 2);
                continue;
            }
            if (type == GL_UNSIGNED_BYTE && (format == GL_RGB || format == GL_RGBA))
            {
                o[0] = lut5[r]; o[1] = lut6[g]; o[2] = lut5[b];
                if (format == GL_RGBA)
                    o[3] = 255u;
                continue;
            }
            const uint32_t ln = r * 63u + g * 31u + b * 63u;                  /* R + G + B, over 31 x 63 */
            switch (format)
            {
            case GL_RED:   put(o, type, r, 31u); break;
            case GL_GREEN: put(o, type, g, 63u); break;
            case GL_BLUE:  put(o, type, b, 31u); break;
            case GL_ALPHA: put(o, type, 1u, 1u); break;
            case GL_LUMINANCE: put(o, type, ln < 1953u ? ln : 1953u, 1953u); break;
            case GL_LUMINANCE_ALPHA:
                put(o, type, ln < 1953u ? ln : 1953u, 1953u);
                put(o + s, type, 1u, 1u);
                break;
            default:                            /* GL_RGB / GL_RGBA */
                put(o, type, r, 31u);
                put(o + s, type, g, 63u);
                put(o + 2u * s, type, b, 31u);
                if (format == GL_RGBA)
                    put(o + 3u * s, type, 1u, 1u);
                break;
            }
        }
    }
    return 0;
}

static void read_pixels(GLcontext c, const mgl_pack *pk, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                        GLenum type, GLvoid *pixels)
{
    if (c->in_begin)
    {
        mgl_seterr(c, GL_INVALID_OPERATION);
        return;
    }
    const int e = mgl_readpx_convert(0, 0, x, y, w, h, format, type, pk, 0);   /* the checks alone */
    if (e)
    {
        mgl_seterr(c, (GLenum)e);
        return;
    }
    GLFinish(c);                                /* everything drawn so far, finished */
    mgl_rb rb;
    memset(&rb, 0, sizeof(rb));
    if (!pixels || format == GL_DEPTH_COMPONENT || !mgl_rb_lock_fn || !mgl_rb_lock_fn(c, &rb))
        return;
    (void)mgl_readpx_convert(&rb, c->height, x, y, w, h, format, type, pk, pixels);
    if (mgl_rb_unlock_fn)
        mgl_rb_unlock_fn(c);
}

void mgl3_GLReadPixels(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
    const mgl_pack pk = { 4, 0, 0, 0 };
    read_pixels(c, &pk, x, y, w, h, format, type, pixels);
}

void GLReadPixels_a5(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
    mgl_ctx5 *const x5 = X5(c);
    mgl_mtex_flush(c, MGL_MT_CLEAR);
    const mgl_pack pk = { x5->pack_align, x5->pack_row, x5->pack_skip_rows, x5->pack_skip_pixels };
    read_pixels(c, &pk, x, y, w, h, format, type, pixels);
}

void GLPixelStorei_a5(GLcontext c, GLenum pname, GLint param)
{
    mgl_ctx5 *const x5 = X5(c);
    switch (pname)
    {
    case GL_PACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8)
        {
            mgl_seterr(c, GL_INVALID_VALUE);
            return;
        }
        x5->pack_align = param;
        return;
    case GL_PACK_ROW_LENGTH:
    case GL_PACK_SKIP_ROWS:
    case GL_PACK_SKIP_PIXELS:
        if (param < 0)
        {
            mgl_seterr(c, GL_INVALID_VALUE);
            return;
        }
        if (pname == GL_PACK_ROW_LENGTH)
            x5->pack_row = param;
        else if (pname == GL_PACK_SKIP_ROWS)
            x5->pack_skip_rows = param;
        else
            x5->pack_skip_pixels = param;
        return;
    default:
        GLPixelStorei(c, pname, param);         /* the unpack side: the core's */
    }
}
