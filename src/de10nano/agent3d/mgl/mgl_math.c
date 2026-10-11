/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: sine, cosine, tangent and square root. */

#include "mgl_core.h"

#define MGL_PI      3.14159265358979323846f
#define MGL_HALFPI  1.57079632679489661923f
#define MGL_INV2PI  0.15915494309189533577f
#define MGL_2PI_C1  6.28125f
#define MGL_2PI_C2  1.9353071795864769253e-3f

/* x reduced to [-pi, pi]; |x| beyond 2^23 rad has no fraction left (0) */
static float mgl_reduce(float x)
{
    if (!(x == x))
        return x;                               /* NaN stays NaN */
    if (x > 8388608.0f || x < -8388608.0f)
        return 0.0f;
    const float q = x * MGL_INV2PI;
    const int32_t k = (int32_t)(q >= 0.0f ? q + 0.5f : q - 0.5f);
    const float kf = (float)k;
    return (x - kf * MGL_2PI_C1) - kf * MGL_2PI_C2;
}

/* the odd series on [-pi/2, pi/2]: Taylor to r^13, the first omitted term under 7e-10 there */
static float sin_poly(float r)
{
    const float r2 = r * r;
    float p = -1.6059043836821613e-10f;         /* -1/13! */
    p = p * r2 + 2.5052108385441720e-08f;       /*  1/11! */
    p = p * r2 - 2.7557319223985893e-06f;       /* -1/9!  */
    p = p * r2 + 1.9841269841269841e-04f;       /*  1/7!  */
    p = p * r2 - 8.3333333333333333e-03f;       /* -1/5!  */
    p = p * r2 + 1.6666666666666667e-01f;       /*  1/3!  */
    return r - r * r2 * p;
}

/* the even series on [0, pi/2]: Taylor to r^14, the first omitted term under 1e-10 there */
static float cos_poly(float r)
{
    const float r2 = r * r;
    float p = -1.1470745597729725e-11f;         /* -1/14! */
    p = p * r2 + 2.0876756987868099e-09f;       /*  1/12! */
    p = p * r2 - 2.7557319223985891e-07f;       /* -1/10! */
    p = p * r2 + 2.4801587301587302e-05f;       /*  1/8!  */
    p = p * r2 - 1.3888888888888889e-03f;       /* -1/6!  */
    p = p * r2 + 4.1666666666666667e-02f;       /*  1/4!  */
    p = p * r2 - 0.5f;                          /* -1/2!  */
    return 1.0f + r2 * p;
}

float mgl_sinf(float x)
{
    float r = mgl_reduce(x);
    if (r > MGL_HALFPI)
        r = MGL_PI - r;
    else if (r < -MGL_HALFPI)
        r = -MGL_PI - r;
    return sin_poly(r);
}

float mgl_cosf(float x)
{
    float r = mgl_reduce(x);
    if (r < 0.0f)
        r = -r;                                 /* even */
    if (r > MGL_HALFPI)
        return -cos_poly(MGL_PI - r);
    return cos_poly(r);
}

float mgl_tanf(float x)
{
    const float c = mgl_cosf(x);
    return c != 0.0f ? mgl_sinf(x) / c : 0.0f;
}

float mgl_sqrtf(float x)
{
    return __builtin_sqrtf(x);
}
