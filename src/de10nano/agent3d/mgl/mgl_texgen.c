/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: texture coordinate generation. */

#include <string.h>
#include "mgl_core.h"

void mgl_seterr(GLcontext c, GLenum e);        /* mgl_state.c */

static void v4(float *d, float a, float b, float c2, float e) { d[0] = a; d[1] = b; d[2] = c2; d[3] = e; }

void mgl_texgen_defaults(mgl_ctx5 *x5)          /* GL 1.1 table 6.13 */
{
    x5->tg_mode[0] = x5->tg_mode[1] = GL_EYE_LINEAR;
    v4(x5->tg_obj[0], 1, 0, 0, 0);
    v4(x5->tg_obj[1], 0, 1, 0, 0);
    v4(x5->tg_eye[0], 1, 0, 0, 0);
    v4(x5->tg_eye[1], 0, 1, 0, 0);
}

static int coord_of(GLenum coord)
{
    return coord == GL_S ? 0 : coord == GL_T ? 1 : -1;
}

/* the inverse of a column-major 4 x 4 (cofactors); 0 = singular */
static int inv4(const float *m, float *o)
{
    float t[16];
    t[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    t[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    t[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    t[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    t[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    t[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    t[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    t[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    t[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    t[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    t[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    t[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    t[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    t[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    t[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    t[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * t[0] + m[1] * t[4] + m[2] * t[8] + m[3] * t[12];
    if (det == 0.0f)
        return 0;
    const float id = 1.0f / det;
    for (int i = 0; i < 16; i++)
        o[i] = t[i] * id;
    return 1;
}

void GLTexGeni_a5(GLcontext c, GLenum coord, GLenum pname, GLenum param)
{
    mgl_ctx5 *const x5 = X5(c);
    const int k = coord_of(coord);
    if (k < 0 || pname != GL_TEXTURE_GEN_MODE ||
        (param != GL_OBJECT_LINEAR && param != GL_EYE_LINEAR && param != GL_SPHERE_MAP))
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    x5->tg_mode[k] = param;
}

void GLTexGenfv_a5(GLcontext c, GLenum coord, GLenum pname, const GLfloat *p)
{
    mgl_ctx5 *const x5 = X5(c);
    const int k = coord_of(coord);
    if (!p)
        return;
    if (pname == GL_TEXTURE_GEN_MODE)
    {
        GLTexGeni_a5(c, coord, pname, (GLenum)p[0]);
        return;
    }
    if (k < 0 || (pname != GL_OBJECT_PLANE && pname != GL_EYE_PLANE))
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (pname == GL_OBJECT_PLANE)
    {
        memcpy(x5->tg_obj[k], p, 16);
        return;
    }
    float mi[16];
    if (!inv4(c->mv[c->mv_sp].m, mi))
    {
        memcpy(x5->tg_eye[k], p, 16);           /* (a singular modelview: kept as given) */
        return;
    }
    for (int j = 0; j < 4; j++)
        x5->tg_eye[k][j] = p[0] * mi[4 * j] + p[1] * mi[4 * j + 1] + p[2] * mi[4 * j + 2] + p[3] * mi[4 * j + 3];
}

static void normal_matrix(GLcontext c, mgl_ctx5 *x5)
{
    const float *m = c->mv[c->mv_sp].m;
    if (x5->tg_ready)
    {
        int same = 1;
        for (int i = 0; i < 16 && same; i++)
            same = x5->tg_mv[i] == m[i];
        if (same)
            return;
    }
    memcpy(x5->tg_mv, m, 64);
    float mi[16];
    if (!inv4(m, mi))
        memset(mi, 0, sizeof(mi));
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            x5->tg_nm[3 * i + j] = mi[4 * i + j];
    x5->tg_ready = 1;
}

void mgl_texgen(GLcontext c, mgl_ctx5 *x5, float x, float y, float z, float w, float tc[4])
{
    const float *m = c->mv[c->mv_sp].m;
    const float xe = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    const float ye = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    const float ze = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    const float we = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
    float rx = 0.0f, ry = 0.0f, rm = 0.0f;
    int sphere = 0;
    for (int k = 0; k < 2; k++)
    {
        if (!x5->tg_en[k])
            continue;
        if (x5->tg_mode[k] == GL_OBJECT_LINEAR)
        {
            const float *pl = x5->tg_obj[k];
            tc[k] = pl[0] * x + pl[1] * y + pl[2] * z + pl[3] * w;
        }
        else if (x5->tg_mode[k] == GL_EYE_LINEAR)
        {
            const float *pl = x5->tg_eye[k];
            tc[k] = pl[0] * xe + pl[1] * ye + pl[2] * ze + pl[3] * we;
        }
        else                                    /* GL_SPHERE_MAP */
        {
            if (!sphere)
            {
                normal_matrix(c, x5);
                const float *nm = x5->tg_nm, *n = c->cur_normal;
                float nx = nm[0] * n[0] + nm[1] * n[1] + nm[2] * n[2];
                float ny = nm[3] * n[0] + nm[4] * n[1] + nm[5] * n[2];
                float nz = nm[6] * n[0] + nm[7] * n[1] + nm[8] * n[2];
                if (x5->en_normalize)
                {
                    const float l = mgl_sqrtf(nx * nx + ny * ny + nz * nz);
                    if (l > 0.0f)
                    {
                        nx /= l; ny /= l; nz /= l;
                    }
                }
                const float ul = mgl_sqrtf(xe * xe + ye * ye + ze * ze);
                const float ux = ul > 0.0f ? xe / ul : 0.0f, uy = ul > 0.0f ? ye / ul : 0.0f,
                            uz = ul > 0.0f ? ze / ul : 0.0f;
                const float nu = nx * ux + ny * uy + nz * uz;
                rx = ux - 2.0f * nx * nu;
                ry = uy - 2.0f * ny * nu;
                const float rz = uz - 2.0f * nz * nu;
                rm = 2.0f * mgl_sqrtf(rx * rx + ry * ry + (rz + 1.0f) * (rz + 1.0f));
                sphere = 1;
            }
            tc[k] = rm > 0.0f ? (k == 0 ? rx : ry) / rm + 0.5f : 0.5f;
        }
    }
}
