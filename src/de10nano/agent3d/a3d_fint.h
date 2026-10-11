/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: clamps and compares on the bits of IEEE floats. */

#ifndef A3D_FINT_H
#define A3D_FINT_H

#include <stdint.h>

static inline uint32_t a3d_fb(float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, 4);
    return u;
}

/* the order key: the magnitude bits, negated for a negative sign (-0 -> 0) */
static inline int32_t a3d_fkey(float f)
{
    const int32_t i = (int32_t)a3d_fb(f);
    const int32_t m = i >> 31;
    return ((i & 0x7FFFFFFF) ^ m) - m;
}

/* (uint32_t)(a3d_clampf(v, 0, 1) * 255 + 0.5): NaN and v <= 0 -> 0, v >= 1 (+Inf too) -> 255 */
static inline uint32_t a3d_u8i(float v)
{
    const uint32_t u = a3d_fb(v);
    if ((int32_t)u <= 0 || u > 0x7F800000u)     /* +0, the sign bit (-0, negatives, negative NaNs), positive NaNs */
        return 0u;
    if (u >= 0x3F800000u)                       /* 1.0f and above, +Inf */
        return 255u;
    return (uint32_t)(v * 255.0f + 0.5f);
}

static inline int32_t a3d_sn4i(float v)
{
    const float p = v * 16.0f;
    const uint32_t u = a3d_fb(p);
    if ((u & 0x7FFFFFFFu) > 0x7F800000u)        /* NaN: every compare false -> the low end */
        return -32768;
    if ((int32_t)u >= 0x46FFFE00)               /* 32767.0f and above, +Inf */
        return 32767;
    if (u >= 0xC7000000u)                       /* -32768.0f and below, -Inf (NaNs are gone) */
        return -32768;
    return (int32_t)((int32_t)u < 0 ? p - 0.5f : p + 0.5f);    /* -0 takes p - 0.5 = -0.5 -> 0, as + 0.5 did */
}

/* the depth's 2.30: zc = a3d_clampf(z, 0, 1); zc >= 1 ? 1 << 30 : (uint32_t)(zc * 2^30) */
static inline uint32_t a3d_z30i(float z)
{
    const uint32_t u = a3d_fb(z);
    if ((int32_t)u <= 0 || u > 0x7F800000u)
        return 0u;
    if (u >= 0x3F800000u)
        return 0x40000000u;
    return (uint32_t)(z * 1073741824.0f);
}

/* w > 0 && w < 1e30f (NaN: false) -- a3d_finish_vtx's "a usable w" */
static inline int a3d_wok(float w)
{
    const uint32_t u = a3d_fb(w);
    return (int32_t)u > 0 && u < 0x7149F2CAu;   /* 0x7149F2CA = 1e30f; positive floats order as their bits */
}

/* f == 0.0f (either zero; NaN: false) */
static inline int a3d_fzero(float f)
{
    return (a3d_fb(f) & 0x7FFFFFFFu) == 0u;
}

/* f < 0.0f (NaN and -0: false) */
static inline int a3d_fneg(float f)
{
    const uint32_t u = a3d_fb(f);
    return (int32_t)u < 0 && (u & 0x7FFFFFFFu) - 1u < 0x7F800000u;
}

#endif
