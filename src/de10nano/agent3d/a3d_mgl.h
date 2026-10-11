/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: MiniGL's deferred geometry batches. */

#ifndef A3D_MGL_H
#define A3D_MGL_H

#include "a3d_agent.h"
#include "mgl/mgl_stage.h"

#define A3D_MGL_MAGIC           0x4D475431u     /* 'MGT1'                                                            */
#define A3D_MGL_BLK_SIZE        128u
#define A3D_MGL_REC_SIZE        40u             /* x y z w s t r g b a, big-endian floats                            */
#define A3D_MGL_MAXTRI          4096u

/* a3d_mgl.c: the ONCALL -- the block checked and read, then a3d_draw_mgl */
int32_t a3d_mgl_tris(a3d_ctx *c, uint32_t a1, uint32_t flags, uint32_t *binned);
uint64_t a3d_oncall_more(a3d_ctx *c, uint32_t id, uint32_t d0, uint32_t a1);
int32_t a3d_draw_mgl(a3d_ctx *c, const mgl_geom *g, const uint8_t *rec, uint32_t ntri, uint32_t *binned);

#endif
