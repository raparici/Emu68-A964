/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: GLU. */

#include <string.h>
#include "mgl_core.h"
#include "mgl_gl5.h"

extern GLcontext mgl_current;                   /* the platform's (src/mgl_lib.c) or the test's */
void mgl_seterr(GLcontext c, GLenum e);

#include "mgl_abi5_protos.h"                  /* the a5_ entries GLU draws through (ABI-5 token numbers) */

#define PI_F 3.14159265358979323846f

const GLubyte *a5_GLUErrorString(GLenum code)
{
    switch (code)
    {
    case GL5_NO_ERROR:                return (const GLubyte *)"no error";
    case GL5_INVALID_ENUM:            return (const GLubyte *)"invalid enumerant";
    case GL5_INVALID_VALUE:           return (const GLubyte *)"invalid value";
    case GL5_INVALID_OPERATION:       return (const GLubyte *)"invalid operation";
    case GL5_STACK_OVERFLOW:          return (const GLubyte *)"stack overflow";
    case GL5_STACK_UNDERFLOW:         return (const GLubyte *)"stack underflow";
    case GL5_OUT_OF_MEMORY:           return (const GLubyte *)"out of memory";
    case GLU5_INVALID_ENUM:           return (const GLubyte *)"invalid enumerant";
    case GLU5_INVALID_VALUE:          return (const GLubyte *)"invalid value";
    case GLU5_OUT_OF_MEMORY:          return (const GLubyte *)"out of memory";
    case GLU5_INCOMPATIBLE_GL_VERSION: return (const GLubyte *)"incompatible gl version";
    case GLU5_INVALID_OPERATION:      return (const GLubyte *)"invalid operation";
    default:                          return 0;
    }
}

void a5_GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    GLcontext c = mgl_current;
    if (!c)
        return;
    const float a = fovy * (PI_F / 360.0f);
    const float top = znear * mgl_tanf(a);
    const float right = top * aspect;
    a5_GLFrustum(c, -right, right, -top, top, znear, zfar);
}

void a5_GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy,
                  GLfloat uz)
{
    GLcontext c = mgl_current;
    if (!c)
        return;
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float l = mgl_sqrtf(fx * fx + fy * fy + fz * fz);
    if (l == 0.0f)
        return;
    fx = fx / l; fy = fy / l; fz = fz / l;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    l = mgl_sqrtf(sx * sx + sy * sy + sz * sz);
    if (l == 0.0f)
        return;
    sx = sx / l; sy = sy / l; sz = sz / l;
    const float vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    const GLfloat m[16] = { sx, vx, -fx, 0.0f,  sy, vy, -fy, 0.0f,  sz, vz, -fz, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f };
    a5_GLMultMatrixf(c, m);
    a5_GLTranslatef(c, -ex, -ey, -ez);
}

static uint32_t comps_of(GLenum fmt5)
{
    switch (fmt5)
    {
    case GL5_RGBA:            return 4;
    case GL5_RGB:             return 3;
    case GL5_LUMINANCE_ALPHA: return 2;
    case GL5_LUMINANCE: case GL5_ALPHA: case GL5_RED: case GL5_GREEN: case GL5_BLUE: case GL5_COLOR_INDEX:
    case GL5_INTENSITY:       return 1;
    default:                  return 0;
    }
}

static uint32_t near_pow2(uint32_t n, uint32_t max)
{
    uint32_t p = 1;
    while (p * 2u <= n)
        p *= 2u;
    if (p < n && n - p > p * 2u - n)            /* the nearer power (GLU: "the closest power of two") */
        p *= 2u;
    return p > max ? max : p;
}

/* box-filter resample (area average) of an n-component byte image */
static void resample(const uint8_t *src, uint32_t sw, uint32_t sh, uint32_t srow, uint8_t *dst, uint32_t dw,
                     uint32_t dh, uint32_t n)
{
    for (uint32_t y = 0; y < dh; y++)
    {
        const uint32_t y0 = y * sh / dh, y1 = ((y + 1u) * sh + dh - 1u) / dh;
        for (uint32_t x = 0; x < dw; x++)
        {
            const uint32_t x0 = x * sw / dw, x1 = ((x + 1u) * sw + dw - 1u) / dw;
            for (uint32_t k = 0; k < n; k++)
            {
                uint32_t sum = 0, cnt = 0;
                for (uint32_t yy = y0; yy < y1; yy++)
                    for (uint32_t xx = x0; xx < x1; xx++)
                    {
                        sum += src[yy * srow + xx * n + k];
                        cnt++;
                    }
                dst[(y * dw + x) * n + k] = (uint8_t)((sum + cnt / 2u) / (cnt ? cnt : 1u));
            }
        }
    }
}

GLint a5_GLUBuild2DMipmaps(GLcontext c, GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, const GLvoid *data)
{
    if (!c)
        c = mgl_current;
    if (!c)
        return (GLint)GLU5_INVALID_OPERATION;
    if (width < 1 || height < 1 || !data)
        return (GLint)GLU5_INVALID_VALUE;
    const uint32_t n = comps_of(format);
    if (!n || type != GL5_UNSIGNED_BYTE)
        return (GLint)GLU5_INVALID_ENUM;
    float mx = 256.0f;
    {
        float v[16];
        if (mgl_get_values(c, GL_MAX_TEXTURE_SIZE, v) == 1 && v[0] >= 1.0f)
            mx = v[0];
    }
    uint32_t w = near_pow2((uint32_t)width, (uint32_t)mx), h = near_pow2((uint32_t)height, (uint32_t)mx);
    /* the source rows as the program laid them out (GL's unpack state) */
    const uint32_t al = c->unpack_align > 0 ? (uint32_t)c->unpack_align : 4u;
    const uint32_t rowpx = c->unpack_row > 0 ? (uint32_t)c->unpack_row : (uint32_t)width;
    const uint32_t srow = (rowpx * n + al - 1u) / al * al;
    uint8_t *a = (uint8_t *)c->sink.alloc(c->sink.ud, w * h * n);
    uint8_t *b = (uint8_t *)c->sink.alloc(c->sink.ud, (w / 2u + 1u) * (h / 2u + 1u) * n);
    if (!a || !b)
    {
        if (a)
            c->sink.free(c->sink.ud, a, w * h * n);
        if (b)
            c->sink.free(c->sink.ud, b, (w / 2u + 1u) * (h / 2u + 1u) * n);
        return (GLint)GLU5_OUT_OF_MEMORY;
    }
    const uint32_t wa = w * h * n, wb = (w / 2u + 1u) * (h / 2u + 1u) * n;
    resample((const uint8_t *)data, (uint32_t)width, (uint32_t)height, srow, a, w, h, n);
    const GLint save_al = c->unpack_align, save_row = c->unpack_row;
    c->unpack_align = 1;
    c->unpack_row = 0;
    for (GLint level = 0;; level++)
    {
        a5_GLTexImage2D(c, target, level, internalFormat, (GLsizei)w, (GLsizei)h, 0, format, type, a);
        if (w == 1u && h == 1u)
            break;
        const uint32_t nw = w > 1u ? w / 2u : 1u, nh = h > 1u ? h / 2u : 1u;
        resample(a, w, h, w * n, b, nw, nh, n);
        memcpy(a, b, nw * nh * n);
        w = nw;
        h = nh;
    }
    c->unpack_align = save_al;
    c->unpack_row = save_row;
    c->sink.free(c->sink.ud, a, wa);
    c->sink.free(c->sink.ud, b, wb);
    return 0;
}

struct mgl_quadric
{
    uint32_t used;
    GLenum normals, style, orient;
    GLboolean tex;
    MGLUfuncptr err;
};
#define MGL_QUADRICS 64u
static struct mgl_quadric g_quad[MGL_QUADRICS] = { { 0 } };

GLUquadricObj *a5_GLUNewQuadric(void)
{
    for (uint32_t i = 0; i < MGL_QUADRICS; i++)
        if (!g_quad[i].used)
        {
            struct mgl_quadric *q = &g_quad[i];
            q->used = 1;
            q->normals = GLU5_SMOOTH;
            q->style = GLU5_FILL;
            q->orient = GLU5_OUTSIDE;
            q->tex = GL_FALSE;
            q->err = 0;
            return q;
        }
    return 0;                                   /* GLU: NULL when there is no memory for one */
}

void a5_GLUDeleteQuadric(GLUquadricObj *q)
{
    if (q >= &g_quad[0] && q < &g_quad[MGL_QUADRICS])
        q->used = 0;
}

static void qerr(GLUquadricObj *q, GLenum e)
{
    if (q && q->err)
        ((void (*)(GLenum))q->err)(e);
}

void a5_GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    if (!q)
        return;
    if (which == GLU5_ERROR)
        q->err = fn;
}

void a5_GLUQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    if (!q)
        return;
    if (normals != GLU5_NONE && normals != GLU5_FLAT && normals != GLU5_SMOOTH)
    {
        qerr(q, GLU5_INVALID_ENUM);
        return;
    }
    q->normals = normals;
}

void a5_GLUQuadricTexture(GLUquadricObj *q, GLboolean tex)
{
    if (q)
        q->tex = tex ? GL_TRUE : GL_FALSE;
}

void a5_GLUQuadricDrawStyle(GLUquadricObj *q, GLenum style)
{
    if (!q)
        return;
    if (style != GLU5_POINT && style != GLU5_LINE && style != GLU5_FILL && style != GLU5_SILHOUETTE)
    {
        qerr(q, GLU5_INVALID_ENUM);
        return;
    }
    q->style = style;
}

void a5_GLUQuadricOrientation(GLUquadricObj *q, GLenum orient)
{
    if (!q)
        return;
    if (orient != GLU5_OUTSIDE && orient != GLU5_INSIDE)
    {
        qerr(q, GLU5_INVALID_ENUM);
        return;
    }
    q->orient = orient;
}

static void qv(GLcontext c, const GLUquadricObj *q, const float *n, float s, float t, float x, float y, float z)
{
    if (q->normals != GLU5_NONE)
    {
        const float sg = q->orient == GLU5_INSIDE ? -1.0f : 1.0f;
        a5_GLNormal3f(c, sg * n[0], sg * n[1], sg * n[2]);
    }
    if (q->tex)
        a5_GLTexCoord2f(c, s, t);
    a5_GLVertex4f(c, x, y, z, 1.0f);
}

void a5_GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    GLcontext c = mgl_current;
    if (!c || !q)
        return;
    if (slices < 2 || stacks < 1 || base < 0.0 || top < 0.0 || height < 0.0)
    {
        qerr(q, GLU5_INVALID_VALUE);
        return;
    }
    const float h = (float)height, b = (float)base, tp = (float)top;
    const float dz = h > 0.0f ? (b - tp) / h : 0.0f;
    const float nl = mgl_sqrtf(1.0f + dz * dz);
    const int inside = q->orient == GLU5_INSIDE;
    if (q->style == GLU5_FILL)
    {
        for (GLint j = 0; j < stacks; j++)
        {
            const float z0 = h * (float)j / (float)stacks, z1 = h * (float)(j + 1) / (float)stacks;
            const float r0 = b + (tp - b) * (float)j / (float)stacks, r1 = b + (tp - b) * (float)(j + 1) / (float)stacks;
            a5_GLBegin(c, GL5_QUAD_STRIP);
            for (GLint i = 0; i <= slices; i++)
            {
                const GLint ii = inside ? slices - i : i;            /* INSIDE: the other way round */
                const float a = 2.0f * PI_F * (float)ii / (float)slices;
                const float sa = mgl_sinf(a), ca = mgl_cosf(a);
                float n[3] = { sa / nl, ca / nl, dz / nl };
                if (q->normals == GLU5_FLAT)
                {
                    const float am = 2.0f * PI_F * ((float)ii + (inside ? 0.5f : -0.5f)) / (float)slices;
                    n[0] = mgl_sinf(am) / nl; n[1] = mgl_cosf(am) / nl;
                }
                const float s = (float)ii / (float)slices;
                qv(c, q, n, s, (float)j / (float)stacks, r0 * sa, r0 * ca, z0);
                qv(c, q, n, s, (float)(j + 1) / (float)stacks, r1 * sa, r1 * ca, z1);
            }
            a5_GLEnd(c);
        }
        return;
    }
    if (q->style == GLU5_POINT)
    {
        a5_GLBegin(c, GL5_POINTS);
        for (GLint j = 0; j <= stacks; j++)
        {
            const float z = h * (float)j / (float)stacks, r = b + (tp - b) * (float)j / (float)stacks;
            for (GLint i = 0; i < slices; i++)
            {
                const float a = 2.0f * PI_F * (float)i / (float)slices;
                const float n[3] = { mgl_sinf(a) / nl, mgl_cosf(a) / nl, dz / nl };
                qv(c, q, n, (float)i / (float)slices, (float)j / (float)stacks, r * n[0] * nl, r * n[1] * nl, z);
            }
        }
        a5_GLEnd(c);
        return;
    }
    for (GLint j = 0; j <= stacks; j++)
    {
        if (q->style == GLU5_SILHOUETTE && j != 0 && j != stacks)
            continue;
        const float z = h * (float)j / (float)stacks, r = b + (tp - b) * (float)j / (float)stacks;
        a5_GLBegin(c, GL5_LINE_LOOP);
        for (GLint i = 0; i < slices; i++)
        {
            const float a = 2.0f * PI_F * (float)i / (float)slices;
            const float n[3] = { mgl_sinf(a) / nl, mgl_cosf(a) / nl, dz / nl };
            qv(c, q, n, (float)i / (float)slices, (float)j / (float)stacks, r * mgl_sinf(a), r * mgl_cosf(a), z);
        }
        a5_GLEnd(c);
    }
    for (GLint i = 0; i < slices; i++)
    {
        const float a = 2.0f * PI_F * (float)i / (float)slices;
        const float sa = mgl_sinf(a), ca = mgl_cosf(a);
        const float n[3] = { sa / nl, ca / nl, dz / nl };
        a5_GLBegin(c, GL5_LINE_STRIP);
        for (GLint j = 0; j <= stacks; j++)
        {
            const float z = h * (float)j / (float)stacks, r = b + (tp - b) * (float)j / (float)stacks;
            qv(c, q, n, (float)i / (float)slices, (float)j / (float)stacks, r * sa, r * ca, z);
        }
        a5_GLEnd(c);
    }
}

void a5_GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    GLcontext c = mgl_current;
    if (!c || !q)
        return;
    if (slices < 2 || stacks < 1 || radius < 0.0)
    {
        qerr(q, GLU5_INVALID_VALUE);
        return;
    }
    const float r = (float)radius;
    const int inside = q->orient == GLU5_INSIDE;
    if (q->style == GLU5_FILL)
    {
        const int caps = stacks >= 2;
        for (GLint j = caps ? 1 : 0; j < (caps ? stacks - 1 : stacks); j++)
        {
            const float p0 = PI_F * (float)j / (float)stacks, p1 = PI_F * (float)(j + 1) / (float)stacks;
            const float s0 = mgl_sinf(p0), c0 = -mgl_cosf(p0), s1 = mgl_sinf(p1), c1 = -mgl_cosf(p1);
            a5_GLBegin(c, GL5_QUAD_STRIP);
            for (GLint i = 0; i <= slices; i++)
            {
                const GLint ii = inside ? slices - i : i;
                const float a = 2.0f * PI_F * (float)ii / (float)slices;
                const float sa = mgl_sinf(a), ca = mgl_cosf(a);
                const float n0[3] = { s0 * sa, s0 * ca, c0 }, n1[3] = { s1 * sa, s1 * ca, c1 };
                const float s = (float)ii / (float)slices;
                qv(c, q, n0, s, (float)j / (float)stacks, r * n0[0], r * n0[1], r * n0[2]);
                qv(c, q, n1, s, (float)(j + 1) / (float)stacks, r * n1[0], r * n1[1], r * n1[2]);
            }
            a5_GLEnd(c);
        }
        if (!caps)
            return;
        for (int top = 0; top < 2; top++)       /* the south cap (z = -r), then the north one (z = +r) */
        {
            const GLint j = top ? stacks - 1 : 1;               /* the ring's latitude */
            const float p = PI_F * (float)j / (float)stacks, sp = mgl_sinf(p), cp = -mgl_cosf(p);
            const float pole[3] = { 0.0f, 0.0f, top ? 1.0f : -1.0f };
            a5_GLBegin(c, GL5_TRIANGLE_FAN);
            qv(c, q, pole, 0.5f, top ? 1.0f : 0.0f, 0.0f, 0.0f, r * pole[2]);
            /* counter-clockwise seen from outside: around the south pole with the angle, the north one against it */
            const int up = top ? inside : !inside;
            for (GLint i = 0; i <= slices; i++)
            {
                const GLint ii = up ? i : slices - i;
                const float a = 2.0f * PI_F * (float)ii / (float)slices;
                const float n[3] = { sp * mgl_sinf(a), sp * mgl_cosf(a), cp };
                qv(c, q, n, (float)ii / (float)slices, (float)j / (float)stacks, r * n[0], r * n[1], r * n[2]);
            }
            a5_GLEnd(c);
        }
        return;
    }
    const GLenum prim = q->style == GLU5_POINT ? GL5_POINTS : GL5_LINE_STRIP;
    if (q->style == GLU5_POINT)
        a5_GLBegin(c, prim);
    for (GLint i = 0; i < slices; i++)          /* the longitudes */
    {
        const float a = 2.0f * PI_F * (float)i / (float)slices;
        const float sa = mgl_sinf(a), ca = mgl_cosf(a);
        if (q->style != GLU5_POINT)
            a5_GLBegin(c, prim);
        for (GLint j = 0; j <= stacks; j++)
        {
            const float p = PI_F * (float)j / (float)stacks;
            const float n[3] = { mgl_sinf(p) * sa, mgl_sinf(p) * ca, -mgl_cosf(p) };
            qv(c, q, n, (float)i / (float)slices, (float)j / (float)stacks, r * n[0], r * n[1], r * n[2]);
        }
        if (q->style != GLU5_POINT)
            a5_GLEnd(c);
    }
    if (q->style == GLU5_LINE)
        for (GLint j = 1; j < stacks; j++)      /* the latitudes */
        {
            const float p = PI_F * (float)j / (float)stacks;
            a5_GLBegin(c, GL5_LINE_LOOP);
            for (GLint i = 0; i < slices; i++)
            {
                const float a = 2.0f * PI_F * (float)i / (float)slices;
                const float n[3] = { mgl_sinf(p) * mgl_sinf(a), mgl_sinf(p) * mgl_cosf(a), -mgl_cosf(p) };
                qv(c, q, n, (float)i / (float)slices, (float)j / (float)stacks, r * n[0], r * n[1], r * n[2]);
            }
            a5_GLEnd(c);
        }
    if (q->style == GLU5_POINT)
        a5_GLEnd(c);
}

void a5_GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    GLcontext c = mgl_current;
    if (!c || !q)
        return;
    if (slices < 2 || loops < 1 || inner < 0.0 || outer < 0.0 || inner > outer)
    {
        qerr(q, GLU5_INVALID_VALUE);
        return;
    }
    const float ri = (float)inner, ro = (float)outer;
    const float nz[3] = { 0.0f, 0.0f, 1.0f };
    const int inside = q->orient == GLU5_INSIDE;
    if (q->style == GLU5_FILL)
    {
        for (GLint k = 0; k < loops; k++)
        {
            const float r0 = ri + (ro - ri) * (float)k / (float)loops, r1 = ri + (ro - ri) * (float)(k + 1) / (float)loops;
            a5_GLBegin(c, GL5_QUAD_STRIP);
            for (GLint i = 0; i <= slices; i++)
            {
                const GLint ii = inside ? slices - i : i;
                const float a = 2.0f * PI_F * (float)ii / (float)slices;
                const float sa = mgl_sinf(a), ca = mgl_cosf(a);
                /* outer first: counter-clockwise seen from +z (OUTSIDE) */
                qv(c, q, nz, 0.5f + r1 * sa / (2.0f * ro), 0.5f + r1 * ca / (2.0f * ro), r1 * sa, r1 * ca, 0.0f);
                qv(c, q, nz, 0.5f + r0 * sa / (2.0f * ro), 0.5f + r0 * ca / (2.0f * ro), r0 * sa, r0 * ca, 0.0f);
            }
            a5_GLEnd(c);
        }
        return;
    }
    for (GLint k = 0; k <= loops; k++)          /* the rings (SILHOUETTE: the inner and the outer edge only) */
    {
        if (q->style == GLU5_SILHOUETTE && k != 0 && k != loops)
            continue;
        const float r = ri + (ro - ri) * (float)k / (float)loops;
        if (r == 0.0f && q->style != GLU5_POINT)
            continue;
        a5_GLBegin(c, q->style == GLU5_POINT ? GL5_POINTS : GL5_LINE_LOOP);
        for (GLint i = 0; i < slices; i++)
        {
            const float a = 2.0f * PI_F * (float)i / (float)slices;
            qv(c, q, nz, 0.5f + r * mgl_sinf(a) / (2.0f * ro), 0.5f + r * mgl_cosf(a) / (2.0f * ro), r * mgl_sinf(a),
               r * mgl_cosf(a), 0.0f);
        }
        a5_GLEnd(c);
    }
    if (q->style == GLU5_LINE)
        for (GLint i = 0; i < slices; i++)      /* the spokes */
        {
            const float a = 2.0f * PI_F * (float)i / (float)slices;
            const float sa = mgl_sinf(a), ca = mgl_cosf(a);
            a5_GLBegin(c, GL5_LINE_STRIP);
            qv(c, q, nz, 0.5f + ri * sa / (2.0f * ro), 0.5f + ri * ca / (2.0f * ro), ri * sa, ri * ca, 0.0f);
            qv(c, q, nz, 0.5f + ro * sa / (2.0f * ro), 0.5f + ro * ca / (2.0f * ro), ro * sa, ro * ca, 0.0f);
            a5_GLEnd(c);
        }
}

void a5_GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    struct mgl_quadric q = { 1, GLU5_SMOOTH, GLU5_FILL, GLU5_OUTSIDE, GL_FALSE, 0 };
    a5_GLUSphere(&q, radius, slices, stacks);
}

void a5_GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    struct mgl_quadric q = { 1, GLU5_SMOOTH, GLU5_FILL, GLU5_OUTSIDE, GL_FALSE, 0 };
    a5_GLUCylinder(&q, base, 0.0, height, slices, stacks);
}

void a5_GLUTSolidCube(GLdouble size)
{
    GLcontext c = mgl_current;
    if (!c)
        return;
    static const float n[6][3] = { { -1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const uint8_t f[6][4] = { { 0, 1, 2, 3 }, { 3, 2, 6, 7 }, { 7, 6, 5, 4 }, { 4, 5, 1, 0 }, { 5, 6, 2, 1 },
                                     { 7, 4, 0, 3 } };
    const float h = (float)size / 2.0f;
    float v[8][3];
    for (int i = 0; i < 8; i++)
    {
        v[i][0] = (i == 0 || i == 1 || i == 2 || i == 3) ? -h : h;
        v[i][1] = (i == 0 || i == 1 || i == 4 || i == 5) ? -h : h;
        v[i][2] = (i == 0 || i == 3 || i == 4 || i == 7) ? -h : h;
    }
    for (int i = 0; i < 6; i++)
    {
        a5_GLBegin(c, GL5_QUADS);
        a5_GLNormal3f(c, n[i][0], n[i][1], n[i][2]);
        for (int k = 0; k < 4; k++)
            a5_GLVertex4f(c, v[f[i][k]][0], v[f[i][k]][1], v[f[i][k]][2], 1.0f);
        a5_GLEnd(c);
    }
}

void a5_GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    GLcontext c = mgl_current;
    if (!c || sides < 3 || rings < 3)
        return;
    const float r = (float)innerRadius, R = (float)outerRadius;
    for (GLint i = 0; i < rings; i++)
    {
        const float t0 = 2.0f * PI_F * (float)i / (float)rings, t1 = 2.0f * PI_F * (float)(i + 1) / (float)rings;
        const float ct0 = mgl_cosf(t0), st0 = mgl_sinf(t0), ct1 = mgl_cosf(t1), st1 = mgl_sinf(t1);
        a5_GLBegin(c, GL5_QUAD_STRIP);
        for (GLint j = sides; j >= 0; j--)      /* counter-clockwise seen from outside */
        {
            const float p = 2.0f * PI_F * (float)j / (float)sides;
            const float cp = mgl_cosf(p), sp = mgl_sinf(p), d = R + r * cp;
            a5_GLNormal3f(c, ct1 * cp, st1 * cp, sp);
            a5_GLVertex4f(c, ct1 * d, st1 * d, r * sp, 1.0f);
            a5_GLNormal3f(c, ct0 * cp, st0 * cp, sp);
            a5_GLVertex4f(c, ct0 * d, st0 * d, r * sp, 1.0f);
        }
        a5_GLEnd(c);
    }
}

void a5_GLUTSolidDodecahedron(void)
{
    GLcontext c = mgl_current;
    if (!c)
        return;
    const float ph = 1.6180339887f, ip = 0.6180339887f;
    float v[20][3];
    int n = 0;
    for (int i = 0; i < 8; i++)
    {
        v[n][0] = (i & 1) ? 1.0f : -1.0f; v[n][1] = (i & 2) ? 1.0f : -1.0f; v[n][2] = (i & 4) ? 1.0f : -1.0f; n++;
    }
    for (int i = 0; i < 4; i++)
    {
        const float a = (i & 1) ? ip : -ip, b = (i & 2) ? ph : -ph;
        v[n][0] = 0.0f; v[n][1] = a; v[n][2] = b; n++;
        v[n][0] = a; v[n][1] = b; v[n][2] = 0.0f; n++;
        v[n][0] = b; v[n][1] = 0.0f; v[n][2] = a; n++;
    }
    for (int fi = 0; fi < 12; fi++)
    {
        const float a = (fi & 1) ? 1.0f : -1.0f, b = (fi & 2) ? ph : -ph;
        float fn[3];
        if (fi < 4) { fn[0] = 0.0f; fn[1] = a; fn[2] = b; }
        else if (fi < 8) { fn[0] = a; fn[1] = b; fn[2] = 0.0f; }
        else { fn[0] = b; fn[1] = 0.0f; fn[2] = a; }
        const float l = mgl_sqrtf(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
        fn[0] /= l; fn[1] /= l; fn[2] /= l;
        /* the five vertices farthest along the face's direction */
        int pick[5];
        float best[5];
        for (int k = 0; k < 5; k++) { pick[k] = -1; best[k] = -1e9f; }
        for (int i = 0; i < 20; i++)
        {
            const float d = v[i][0] * fn[0] + v[i][1] * fn[1] + v[i][2] * fn[2];
            int k = 4;
            if (d <= best[4])
                continue;
            while (k > 0 && d > best[k - 1]) { best[k] = best[k - 1]; pick[k] = pick[k - 1]; k--; }
            best[k] = d; pick[k] = i;
        }
        /* sorted by angle around the face direction: a frame (u, w) in the face's plane */
        float u[3] = { v[pick[0]][0], v[pick[0]][1], v[pick[0]][2] };
        const float du = u[0] * fn[0] + u[1] * fn[1] + u[2] * fn[2];
        u[0] -= du * fn[0]; u[1] -= du * fn[1]; u[2] -= du * fn[2];
        const float w[3] = { fn[1] * u[2] - fn[2] * u[1], fn[2] * u[0] - fn[0] * u[2], fn[0] * u[1] - fn[1] * u[0] };
        float ang[5];
        for (int k = 0; k < 5; k++)
        {
            const float *p = v[pick[k]];
            const float x = p[0] * u[0] + p[1] * u[1] + p[2] * u[2], y = p[0] * w[0] + p[1] * w[1] + p[2] * w[2];
            /* a monotonic angle stand-in (no atan2 in the core): the quadrant and the slope */
            const float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y;
            const float t = ax + ay > 0.0f ? y / (ax + ay) : 0.0f;
            ang[k] = x >= 0.0f ? t : 2.0f - t;
        }
        for (int i = 1; i < 5; i++)
            for (int k = i; k > 0 && ang[k] < ang[k - 1]; k--)
            {
                const float ta = ang[k]; ang[k] = ang[k - 1]; ang[k - 1] = ta;
                const int tp = pick[k]; pick[k] = pick[k - 1]; pick[k - 1] = tp;
            }
        a5_GLBegin(c, GL5_POLYGON);
        a5_GLNormal3f(c, fn[0], fn[1], fn[2]);
        for (int k = 0; k < 5; k++)
            a5_GLVertex4f(c, v[pick[k]][0], v[pick[k]][1], v[pick[k]][2], 1.0f);
        a5_GLEnd(c);
    }
}
