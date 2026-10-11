/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: fixed-function lighting. */

#include <string.h>
#include "mgl_core.h"

static void prepare(GLcontext c, mgl_ctx5 *x5)  /* the eye-space data, under the current modelview */
{
    const float *m = c->mv[c->mv_sp].m;
    memcpy(x5->lt_mv, m, 64);
    /* the upper 3x3 A (column-major: A[r][k] = m[4k + r]); n_eye = (A^-1)^T n = cofactor(A) n / det(A) */
    const float a00 = m[0], a01 = m[4], a02 = m[8], a10 = m[1], a11 = m[5], a12 = m[9], a20 = m[2], a21 = m[6], a22 = m[10];
    const float c00 = a11 * a22 - a12 * a21, c01 = a12 * a20 - a10 * a22, c02 = a10 * a21 - a11 * a20;
    const float c10 = a02 * a21 - a01 * a22, c11 = a00 * a22 - a02 * a20, c12 = a01 * a20 - a00 * a21;
    const float c20 = a01 * a12 - a02 * a11, c21 = a02 * a10 - a00 * a12, c22 = a00 * a11 - a01 * a10;
    const float det = a00 * c00 + a01 * c01 + a02 * c02;
    if (det != 0.0f)
    {
        const float id = 1.0f / det;            /* (A^-1)^T = cofactor matrix / det, row r = (c_r0, c_r1, c_r2) */
        x5->lt_nm[0] = c00 * id; x5->lt_nm[1] = c01 * id; x5->lt_nm[2] = c02 * id;
        x5->lt_nm[3] = c10 * id; x5->lt_nm[4] = c11 * id; x5->lt_nm[5] = c12 * id;
        x5->lt_nm[6] = c20 * id; x5->lt_nm[7] = c21 * id; x5->lt_nm[8] = c22 * id;
    }
    else                                        /* a singular modelview: its own 3x3 (the normals are meaningless anyway) */
    {
        x5->lt_nm[0] = a00; x5->lt_nm[1] = a01; x5->lt_nm[2] = a02;
        x5->lt_nm[3] = a10; x5->lt_nm[4] = a11; x5->lt_nm[5] = a12;
        x5->lt_nm[6] = a20; x5->lt_nm[7] = a21; x5->lt_nm[8] = a22;
    }
    for (uint32_t i = 0; i < MGL_MAX_LIGHTS; i++)
    {
        const mgl_light *l = &x5->light[i];
        float d = mgl_sqrtf(l->dir[0] * l->dir[0] + l->dir[1] * l->dir[1] + l->dir[2] * l->dir[2]);
        d = d > 0.0f ? 1.0f / d : 0.0f;
        x5->lt_sdir[i][0] = l->dir[0] * d; x5->lt_sdir[i][1] = l->dir[1] * d; x5->lt_sdir[i][2] = l->dir[2] * d;
        x5->lt_scos[i] = mgl_cosf(l->cut * (3.14159265358979323846f / 180.0f));
        float p = mgl_sqrtf(l->pos[0] * l->pos[0] + l->pos[1] * l->pos[1] + l->pos[2] * l->pos[2]);
        p = p > 0.0f ? 1.0f / p : 0.0f;
        x5->lt_l[i][0] = l->pos[0] * p; x5->lt_l[i][1] = l->pos[1] * p; x5->lt_l[i][2] = l->pos[2] * p;
        float hx = x5->lt_l[i][0], hy = x5->lt_l[i][1], hz = x5->lt_l[i][2] + 1.0f;
        float h = mgl_sqrtf(hx * hx + hy * hy + hz * hz);
        h = h > 0.0f ? 1.0f / h : 0.0f;
        x5->lt_h[i][0] = hx * h; x5->lt_h[i][1] = hy * h; x5->lt_h[i][2] = hz * h;
    }
    x5->lt_ready = 1;
}

static float log2_m(float m)                    /* m in [1, 2) */
{
    const float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;
    float p = 1.0f / 15.0f;
    p = p * s2 + 1.0f / 13.0f;
    p = p * s2 + 1.0f / 11.0f;
    p = p * s2 + 1.0f / 9.0f;
    p = p * s2 + 1.0f / 7.0f;
    p = p * s2 + 1.0f / 5.0f;
    p = p * s2 + 1.0f / 3.0f;
    p = p * s2 + 1.0f;
    return 2.0f * s * p * 1.44269504088896340736f;          /* 2 atanh(s) / ln 2 */
}

static float exp2_f(float f)                    /* f in [0, 1) */
{
    const float t = f * 0.69314718055994530942f;
    float p = 1.0f / 3628800.0f;                /* 1/10! */
    p = p * t + 1.0f / 362880.0f;
    p = p * t + 1.0f / 40320.0f;
    p = p * t + 1.0f / 5040.0f;
    p = p * t + 1.0f / 720.0f;
    p = p * t + 1.0f / 120.0f;
    p = p * t + 1.0f / 24.0f;
    p = p * t + 1.0f / 6.0f;
    p = p * t + 0.5f;
    p = p * t + 1.0f;
    return p * t + 1.0f;
}

float mgl_powf(float x, float y)
{
    if (y == 0.0f)
        return 1.0f;
    if (!(x > 0.0f))
        return 0.0f;                            /* 0^y, y > 0 (and a negative or NaN base: none in lighting) */
    uint32_t b;
    memcpy(&b, &x, 4);
    int32_t e = (int32_t)((b >> 23) & 0xFFu) - 127;
    if (e == -127)                              /* a denormal: scaled up first */
    {
        x *= 8388608.0f;
        memcpy(&b, &x, 4);
        e = (int32_t)((b >> 23) & 0xFFu) - 127 - 23;
    }
    b = (b & 0x007FFFFFu) | 0x3F800000u;        /* the mantissa in [1, 2) */
    float m;
    memcpy(&m, &b, 4);
    const float t = y * ((float)e + log2_m(m));
    if (t < -126.0f)
        return 0.0f;
    if (t >= 128.0f)
        return 3.4028234663852886e38f;
    int32_t n = (int32_t)t;
    if ((float)n > t)
        n--;                                    /* floor */
    float r = exp2_f(t - (float)n);
    uint32_t s = (uint32_t)(n + 127) << 23;     /* 2^n, n in [-126, 127] */
    float sc;
    memcpy(&sc, &s, 4);
    return r * sc;
}

static int same_m4(const float *a, const float *b)     /* (no memcmp: the core builds for the firmware too, armhf) */
{
    for (uint32_t k = 0; k < 16u; k++)
        if (a[k] != b[k])
            return 0;
    return 1;
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

void mgl_light_rgba(GLcontext c, float x, float y, float z, float w, float *rgba)
{
    mgl_ctx5 *const x5 = X5(c);
    const float *m = c->mv[c->mv_sp].m;
    if (!x5->lt_ready || !same_m4(x5->lt_mv, m))
        prepare(c, x5);
    mgl_material *mt = &x5->mat[0];
    if (x5->en_color_material && (x5->cm_face == GL_FRONT || x5->cm_face == GL_FRONT_AND_BACK))
    {
        const float *cc = c->cur_color;         /* GL: the material follows the current colour while enabled */
        switch (x5->cm_mode)
        {
        case GL_EMISSION: memcpy(mt->emi, cc, 16); break;
        case GL_AMBIENT:  memcpy(mt->amb, cc, 16); break;
        case GL_DIFFUSE:  memcpy(mt->dif, cc, 16); break;
        case GL_SPECULAR: memcpy(mt->spe, cc, 16); break;
        default:          memcpy(mt->amb, cc, 16); memcpy(mt->dif, cc, 16); break;   /* GL_AMBIENT_AND_DIFFUSE */
        }
    }
    /* the vertex in eye coordinates */
    float ex = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    float ey = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    float ez = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    const float ew = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
    if (ew != 0.0f && ew != 1.0f)
    {
        const float iw = 1.0f / ew;
        ex *= iw; ey *= iw; ez *= iw;
    }
    /* the normal */
    const float *n0 = c->cur_normal, *nm = x5->lt_nm;
    float nx = nm[0] * n0[0] + nm[1] * n0[1] + nm[2] * n0[2];
    float ny = nm[3] * n0[0] + nm[4] * n0[1] + nm[5] * n0[2];
    float nz = nm[6] * n0[0] + nm[7] * n0[1] + nm[8] * n0[2];
    if (x5->en_normalize)
    {
        float l = mgl_sqrtf(nx * nx + ny * ny + nz * nz);
        if (l > 0.0f)
        {
            l = 1.0f / l;
            nx *= l; ny *= l; nz *= l;
        }
    }
    float r = mt->emi[0] + mt->amb[0] * x5->lm_ambient[0];
    float g = mt->emi[1] + mt->amb[1] * x5->lm_ambient[1];
    float b = mt->emi[2] + mt->amb[2] * x5->lm_ambient[2];
    for (uint32_t i = 0; i < MGL_MAX_LIGHTS; i++)
    {
        if (!x5->en_light[i])
            continue;
        const mgl_light *l = &x5->light[i];
        float lx, ly, lz, att = 1.0f;
        if (l->pos[3] != 0.0f)                  /* positional: from the vertex to the light, its distance */
        {
            const float iw = 1.0f / l->pos[3];
            lx = l->pos[0] * iw - ex; ly = l->pos[1] * iw - ey; lz = l->pos[2] * iw - ez;
            const float d = mgl_sqrtf(lx * lx + ly * ly + lz * lz);
            if (d > 0.0f)
            {
                const float id = 1.0f / d;
                lx *= id; ly *= id; lz *= id;
            }
            const float k = l->att[0] + l->att[1] * d + l->att[2] * d * d;
            att = k > 0.0f ? 1.0f / k : 1.0f;
        }
        else
        {
            lx = x5->lt_l[i][0]; ly = x5->lt_l[i][1]; lz = x5->lt_l[i][2];
        }
        float spot = 1.0f;
        if (l->cut != 180.0f)
        {
            const float sc = -(lx * x5->lt_sdir[i][0] + ly * x5->lt_sdir[i][1] + lz * x5->lt_sdir[i][2]);
            if (sc < x5->lt_scos[i])
                continue;                       /* outside the cone: no light at all */
            spot = l->exp != 0.0f ? mgl_powf(sc > 0.0f ? sc : 0.0f, l->exp) : 1.0f;
        }
        const float k = att * spot;
        float ar = mt->amb[0] * l->amb[0], ag = mt->amb[1] * l->amb[1], ab = mt->amb[2] * l->amb[2];
        const float nl = nx * lx + ny * ly + nz * lz;
        if (nl > 0.0f)
        {
            ar += nl * mt->dif[0] * l->dif[0];
            ag += nl * mt->dif[1] * l->dif[1];
            ab += nl * mt->dif[2] * l->dif[2];
            float hx, hy, hz;
            if (x5->lm_local_viewer || l->pos[3] != 0.0f)
            {
                hx = lx; hy = ly; hz = lz;
                if (x5->lm_local_viewer)         /* + the unit vector from the vertex to the eye (the origin) */
                {
                    float vl = mgl_sqrtf(ex * ex + ey * ey + ez * ez);
                    vl = vl > 0.0f ? 1.0f / vl : 0.0f;
                    hx -= ex * vl; hy -= ey * vl; hz -= ez * vl;
                }
                else
                    hz += 1.0f;
                float hl = mgl_sqrtf(hx * hx + hy * hy + hz * hz);
                hl = hl > 0.0f ? 1.0f / hl : 0.0f;
                hx *= hl; hy *= hl; hz *= hl;
            }
            else
            {
                hx = x5->lt_h[i][0]; hy = x5->lt_h[i][1]; hz = x5->lt_h[i][2];
            }
            const float nh = nx * hx + ny * hy + nz * hz;
            if (nh > 0.0f)
            {
                const float sp = mgl_powf(nh, mt->shi);
                ar += sp * mt->spe[0] * l->spe[0];
                ag += sp * mt->spe[1] * l->spe[1];
                ab += sp * mt->spe[2] * l->spe[2];
            }
        }
        r += k * ar;
        g += k * ag;
        b += k * ab;
    }
    rgba[0] = clamp01(r);
    rgba[1] = clamp01(g);
    rgba[2] = clamp01(b);
    rgba[3] = clamp01(mt->dif[3]);
}
